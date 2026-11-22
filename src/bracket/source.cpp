/*
HyperDrive Companion
Copyright (C) 2026 Voidscape Development

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/
#include "source.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <obs-module.h>
#include <util/platform.h>

#include "bracket/paint.hpp"
#include "companion.hpp"
#include "core/state.hpp"
#include "plugin-support.h"

namespace {

using namespace hd::bracket;
using json = nlohmann::json;

constexpr const char *kId = "hyperdrive_bracket_focus";
// The layout's: sets come in over STAGGER_TIME at most, a content change
// fades over HD.set()'s time
constexpr double kStaggerTime = 1.5, kSetIn = 0.4, kContentFade = 0.5, kMaxTexture = 4096;

// Draws a premultiplied picture, faded by color (its opacity in each channel)
constexpr const char *kEffect = R"(
uniform float4x4 ViewProj;
uniform texture2d image;
uniform float4 color;
sampler_state textureSampler { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };
struct VertInOut { float4 pos : POSITION; float2 uv : TEXCOORD0; };
VertInOut VSDefault(VertInOut vert_in)
{
	VertInOut vert_out;
	vert_out.pos = mul(float4(vert_in.pos.xyz, 1.0), ViewProj);
	vert_out.uv = vert_in.uv;
	return vert_out;
}
float4 PSDraw(VertInOut vert_in) : TARGET { return image.Sample(textureSampler, vert_in.uv) * color; }
technique Draw { pass { vertex_shader = VSDefault(vert_in); pixel_shader = PSDraw(vert_in); } }
)";

double now()
{
	return os_gettime_ns() / 1e9;
}

size_t hashOf(const std::string &s)
{
	return std::hash<std::string>()(s);
}

// What's drawn, bottom to top
enum Kind { kLine, kRound, kByes, kStandings, kSet, kGlowLine, kBar, kRing, kTitle, kLabel };

/** What the source's thread made of the state: drawn until the next one */
struct Model {
	Scene scene;
	Placement placed;
	Focus focus;
	Box world;
	double maxZoom = 2;
	/** Changes when the bracket must be drawn again from scratch */
	size_t signature = 0;
	/** Changes when the camera, the dimming or the outlines change */
	size_t focusKey = 0;
	json title;
	uint32_t width = 1920, height = 1080;
	bool showTitle = true;
	/** What each piece shows, by key */
	std::map<std::string, size_t> content;
};

struct Options {
	std::string channel = "main";
	bool title = true, ignoreFocus = false;
	int maxZoom = 0;
	uint32_t width = 1920, height = 1080;
};

/** A value fading from one to another */
struct Anim {
	double from = 1, to = 1, start = 0, duration = 0;
	double at(double t) const
	{
		if (duration <= 0 || t >= start + duration)
			return to;
		return t <= start ? from : from + (to - from) * easeSine((t - start) / duration);
	}
	void go(double target, double t, double time)
	{
		from = at(t);
		to = target;
		start = t;
		duration = time;
	}
};

struct Texture {
	gs_texture_t *tex = nullptr;
	Box at;
	double scale = 0;
	size_t content = 0;
	bool incomplete = false;
	uint64_t assets = 0;
	void release()
	{
		gs_texture_destroy(tex);
		tex = nullptr;
	}
};

struct Element {
	Kind kind = kSet;
	std::string id;
	/** In world pixels (page pixels for the title and caption) */
	Box box;
	int line = -1;
	size_t content = 0;
	Texture tex, old;
	double fadeStart = 0;
	Anim opacity;
	double introAt = -1;
	/** An outline fading out, then removed */
	bool gone = false;
};

struct Job {
	std::shared_ptr<const Model> model;
	std::string key;
	Kind kind;
	std::string id;
	int line;
	Box box;
	double scale;
	size_t content;
	uint64_t assets;
};

struct Result {
	std::string key;
	size_t content;
	double scale;
	uint64_t assets;
	Sprite sprite;
};

std::string keyOf(Kind kind, const std::string &id)
{
	return std::to_string(kind) + ":" + id;
}

class Source;
std::mutex sourcesMutex;
std::set<Source *> sources;

class Source {
public:
	explicit Source(obs_source_t *self) : self_(self)
	{
		obs_enter_graphics();
		char *error = nullptr;
		effect_ = gs_effect_create(kEffect, "hyperdrive-bracket.effect", &error);
		if (!effect_)
			obs_log(LOG_ERROR, "Bracket focus effect: %s", error ? error : "?");
		imageParam_ = effect_ ? gs_effect_get_param_by_name(effect_, "image") : nullptr;
		colorParam_ = effect_ ? gs_effect_get_param_by_name(effect_, "color") : nullptr;
		bfree(error);
		world_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		obs_leave_graphics();
		std::lock_guard<std::mutex> lock(sourcesMutex);
		sources.insert(this);
		thread_ = std::thread([this]() { run(); });
	}

	~Source()
	{
		stop();
		{
			std::lock_guard<std::mutex> lock(sourcesMutex);
			sources.erase(this);
		}
		obs_enter_graphics();
		for (auto &[key, e] : elements_) {
			e.tex.release();
			e.old.release();
		}
		gs_texrender_destroy(world_);
		gs_effect_destroy(effect_);
		obs_leave_graphics();
	}

	void stop()
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			stopping_ = true;
		}
		wake_.notify_all();
		if (thread_.joinable())
			thread_.join();
	}

	void update(obs_data_t *settings)
	{
		Options o;
		// As HyperDrive keeps it (HDBracket.ChannelName()): no dots or extra spaces
		std::string name = obs_data_get_string(settings, "channel");
		std::replace(name.begin(), name.end(), '.', ' ');
		std::istringstream words(name);
		std::string channel;
		for (std::string word; words >> word;)
			channel += (channel.empty() ? "" : " ") + word;
		channel = channel.substr(0, 40);
		while (!channel.empty() && channel.back() == ' ')
			channel.pop_back();
		o.channel = channel.empty() ? "main" : channel;
		o.title = obs_data_get_bool(settings, "title");
		o.ignoreFocus = obs_data_get_bool(settings, "ignore_focus");
		o.maxZoom = static_cast<int>(obs_data_get_int(settings, "max_zoom"));
		o.width = static_cast<uint32_t>(std::max<long long>(1, obs_data_get_int(settings, "width")));
		o.height = static_cast<uint32_t>(std::max<long long>(1, obs_data_get_int(settings, "height")));
		{
			std::lock_guard<std::mutex> lock(mutex_);
			options_ = o;
			dirty_ = true;
		}
		width_ = o.width;
		height_ = o.height;
		wake_.notify_all();
	}

	uint32_t width() const { return width_; }
	uint32_t height() const { return height_; }

	// -----------------------------------------------------------------
	// The source's thread: the model from the state, and the pictures

	void run()
	{
		uint64_t seenState = 0;
		while (true) {
			std::optional<Job> job;
			bool dirty;
			Options options;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				wake_.wait_for(lock, std::chrono::milliseconds(50),
					       [this]() { return stopping_ || dirty_ || !jobs_.empty(); });
				if (stopping_)
					return;
				if (!jobs_.empty()) {
					job = jobs_.front();
					jobs_.pop_front();
					busy_ = *job;
				}
				dirty = dirty_;
				dirty_ = false;
				options = options_;
			}
			Companion *companion = Companion::instance();
			uint64_t state = companion ? companion->stateVersion() : 0;
			if (companion && (dirty || state != seenState)) {
				seenState = state;
				build(*companion, options);
			}
			if (job) {
				Result result{job->key, job->content, job->scale, job->assets, paintJob(*job)};
				std::lock_guard<std::mutex> lock(mutex_);
				results_.push_back(std::move(result));
				busy_ = {};
			}
		}
	}

	/** The model, when what it's made from changed */
	void build(Companion &companion, const Options &options)
	{
		json input = {{"options",
			       {options.channel, options.title, options.ignoreFocus, options.maxZoom, options.width,
				options.height}}};
		companion.client().withState([&](const json &state) {
			const json *channel = hd::get(state, "bracket.channels");
			channel = channel && channel->is_object() && channel->contains(options.channel)
					  ? &(*channel)[options.channel]
					  : nullptr;
			input["channel"] = channel ? *channel : json();
			const json *focus = hd::get(state, "bracket.focus");
			input["focus"] = focus && focus->is_object() && focus->contains(options.channel)
						 ? (*focus)[options.channel]
						 : json();
			std::string path = channel ? hd::text(hd::get(*channel, "playersPath")) : "";
			const json *players = path.empty() ? nullptr : hd::get(state, path + ".slot");
			input["players"] = players ? *players : json::object();
			const json *theme = hd::get(state, "layout_theme");
			input["theme"] = theme ? *theme : json();
			const json *info = hd::get(state, "tournamentInfo");
			input["info"] = info ? *info : json();
		});
		if (input == lastInput_)
			return;
		lastInput_ = input;

		auto model = std::make_shared<Model>();
		const json &channel = input["channel"];
		std::optional<Placement> placed = parsePlacement(hd::get(channel, "placement"));
		if (!placed && !channel.is_null() && !warned_) {
			warned_ = true;
			obs_log(LOG_WARNING, "\"%s\": this HyperDrive doesn't place the bracket for OBS: update it",
				obs_source_get_name(self_));
		}
		model->width = options.width;
		model->height = options.height;
		model->showTitle = options.title;
		model->scene.bracket = channel.contains("bracket") ? channel["bracket"] : json::object();
		model->scene.players = input["players"];
		model->scene.theme = parseTheme(&input["theme"]);
		const json &info = input["info"];
		std::string phase = hd::text(hd::get(channel, "phase"));
		model->title = {{"tournament", hd::text(hd::get(info, "tournamentName"))},
				{"event", hd::text(hd::get(info, "eventName"))},
				{"phase", phase.empty() ? hd::text(hd::get(model->scene.bracket, "name")) : phase},
				{"group", hd::text(hd::get(channel, "phaseGroup"))}};
		const Theme &theme = model->scene.theme;
		size_t themeHash = hashOf(input["theme"].dump());
		model->content[keyOf(kTitle, "")] = hashOf(model->title.dump()) ^ themeHash ^ options.width;
		if (placed) {
			model->placed = *placed;
			model->world = {0, 0, placed->width, placed->height};
			if (placed->standings) {
				Sprite standings = paintStandings(model->scene, *placed->standings, 0.05);
				model->world = unite(model->world, {standings.at.x, standings.at.y, standings.at.w + 32,
								    standings.at.h + 32});
			}
		}
		model->focus = computeFocus(options.ignoreFocus ? nullptr : &input["focus"], &model->scene.bracket,
					    model->placed, theme, model->world);
		const Focus &focus = model->focus;
		model->maxZoom = options.maxZoom > 0 ? options.maxZoom / 100.0 : theme.maxZoom;
		if (focus.mode == "player")
			model->scene.focusPlayer = focus.player;
		model->scene.focusColor = focus.color;

		// HDBracket.Signature(): the shape, not the scores or the players
		model->signature = hashOf(hd::get(channel, "placement") ? channel["placement"].dump() : "") ^
				   hashOf(phase + "|" + hd::text(hd::get(channel, "phaseGroup"))) ^
				   input["players"].size();
		model->focusKey = hashOf(input["focus"].dump()) ^ hashOf(std::to_string(theme.focusDim)) ^
				  hashOf(std::to_string(model->maxZoom)) ^ (theme.glows(focus.mode) << 1) ^
				  options.ignoreFocus ^ hashOf(std::to_string(focus.color));
		model->content[keyOf(kLabel, "")] = hashOf(focus.label) ^ themeHash ^ focus.color;

		const json &sets = model->scene.bracket.contains("sets") ? model->scene.bracket["sets"] : json();
		const json &players = model->scene.players;
		for (const auto &[id, box] : model->placed.sets) {
			std::string what = themeHash ? std::to_string(themeHash) : "";
			const json &set = sets.is_object() && sets.contains(id) ? sets[id] : json();
			what += set.dump();
			if (set.contains("players") && set["players"].is_array())
				for (const json &slot : set["players"]) {
					std::string slotId = hd::text(hd::get(slot, "id"));
					if (players.is_object() && players.contains(slotId))
						what += players[slotId].dump();
					if (!model->scene.focusPlayer.empty() && slotId == model->scene.focusPlayer)
						what += "focus" + std::to_string(focus.color);
				}
			model->content[keyOf(kSet, id)] = hashOf(what);
			model->content[keyOf(kRing, id)] = themeHash ^ focus.color;
		}
		const json &sides = model->scene.bracket.contains("sides") ? model->scene.bracket["sides"] : json();
		for (const auto &[key, box] : model->placed.headers) {
			std::string name;
			for (const json &columns : sides)
				for (const json &column : columns)
					if (hd::text(hd::get(column, "key")) == key)
						name = hd::text(hd::get(column, "name"));
			model->content[keyOf(kRound, key)] = hashOf(name) ^ themeHash;
			model->content[keyOf(kBar, key)] = themeHash ^ focus.color;
		}
		const json &byes = model->scene.bracket.contains("byes") ? model->scene.bracket["byes"] : json();
		for (const auto &[key, box] : model->placed.byes)
			model->content[keyOf(kByes, key)] =
				hashOf(byes.is_object() && byes.contains(key) ? byes[key].dump() : "") ^ themeHash;
		if (model->placed.standings)
			model->content[keyOf(kStandings, "")] =
				hashOf(hd::get(model->scene.bracket, "standings")
					       ? model->scene.bracket["standings"].dump()
					       : "") ^
				hashOf(players.dump()) ^ themeHash;
		for (size_t i = 0; i < model->placed.lines.size(); i++) {
			model->content[keyOf(kLine, std::to_string(i))] = themeHash;
			model->content[keyOf(kGlowLine, std::to_string(i))] = themeHash ^ focus.color;
		}

		std::lock_guard<std::mutex> lock(mutex_);
		pending_ = model;
	}

	static Sprite paintJob(const Job &job)
	{
		const Model &m = *job.model;
		const Theme &theme = m.scene.theme;
		switch (job.kind) {
		case kSet:
			return paintSet(m.scene, job.id, job.box, job.scale);
		case kRound:
			return paintHeader(m.scene, job.id, job.box, job.scale);
		case kByes:
			return paintByes(m.scene, job.id, job.box, job.scale);
		case kStandings:
			return paintStandings(m.scene, *m.placed.standings, job.scale);
		case kLine:
			return paintLine(m.placed.lines[job.line], theme, job.scale);
		case kGlowLine:
			return paintLine(m.placed.lines[job.line], theme, job.scale, m.focus.color);
		case kRing:
			return paintRing(m.placed.sets.at(job.id), theme, m.focus.color, job.scale, false);
		case kBar:
			return paintRing(m.placed.headers.at(job.id), theme, m.focus.color, job.scale, true);
		case kTitle:
			return paintTitle(m.title, theme, m.width, job.scale);
		case kLabel:
			return paintLabel(m.focus.label, theme, m.focus.color, job.scale);
		}
		return {};
	}

	// -----------------------------------------------------------------
	// OBS's graphics thread: the camera, the fades, and the pictures to ask for

	void tick()
	{
		double t = now();
		std::shared_ptr<const Model> model;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			model.swap(pending_);
		}
		if (model)
			apply(model, t);
		if (!model_)
			return;
		// Before asking for more, so what was painted counts
		obs_enter_graphics();
		upload(t);
		for (auto &[key, e] : elements_)
			if (e.old.tex && t - e.fadeStart >= time(kContentFade) && (e.kind != kLabel || t >= labelSwap_))
				e.old.release();
		obs_leave_graphics();
		// The sets come in once they're painted (or after a second)
		if (introPending_ &&
		    (t - builtAt_ > 1 || std::all_of(elements_.begin(), elements_.end(), [](const auto &e) {
			     return e.second.tex.tex || e.second.kind >= kTitle;
		     })))
			startIntro(t);
		request(t);
		if (orderDirty_) {
			order_.clear();
			for (auto &[key, e] : elements_)
				order_.push_back(&e);
			std::stable_sort(order_.begin(), order_.end(),
					 [](const Element *a, const Element *b) { return a->kind < b->kind; });
			orderDirty_ = false;
		}
		frame_ = t;
		worldDrawn_ = false;
	}

	double speed() const { return model_ ? model_->scene.theme.speed : 1; }
	double time(double seconds) const { return speed() > 0 ? seconds / speed() : 0; }

	double viewportHeight() const { return std::max(1.0, model_->height - (model_->showTitle ? kTitleHeight : 0)); }

	View viewNow(double t) const
	{
		if (!moving_ || t >= moveStart_ + moveTime_)
			return view_;
		double k = std::max(0.0, (t - moveStart_) / moveTime_);
		return between(moveFrom_, view_, moveEaseOut_ ? easeOut(k) : easeInOut(k));
	}

	/** The view at rest, on whole pixels so the pictures are sharp */
	static View snapped(View v)
	{
		v.ax += std::round(v.left()) - v.left();
		v.ay += std::round(v.top()) - v.top();
		return v;
	}

	Element &add(Kind kind, const std::string &id, const Box &box, int line = -1)
	{
		Element &e = elements_[keyOf(kind, id)];
		e.kind = kind;
		e.id = id;
		e.box = box;
		e.line = line;
		orderDirty_ = true;
		return e;
	}

	void clearElements()
	{
		obs_enter_graphics();
		for (auto &[key, e] : elements_) {
			e.tex.release();
			e.old.release();
		}
		obs_leave_graphics();
		elements_.clear();
		orderDirty_ = true;
		std::lock_guard<std::mutex> lock(mutex_);
		jobs_.clear();
	}

	void apply(const std::shared_ptr<const Model> &model, double t)
	{
		bool rebuild = !model_ || model->signature != model_->signature;
		bool resized = model_ && (model->width != model_->width || model->height != model_->height ||
					  model->showTitle != model_->showTitle);
		bool refocus = rebuild || resized || model->focusKey != model_->focusKey;
		bool animate = model_ && !rebuild && !resized;
		model_ = model;
		if (rebuild) {
			clearElements();
			const Placement &p = model->placed;
			for (size_t i = 0; i < p.lines.size(); i++)
				add(kLine, std::to_string(i), grow(p.lines[i].bounds(), 12), static_cast<int>(i));
			for (const auto &[key, box] : p.headers)
				add(kRound, key, box);
			for (const auto &[key, box] : p.byes)
				add(kByes, key, {box.x, box.y, box.w, 30});
			if (p.standings)
				add(kStandings, "",
				    {p.standings->x, p.standings->y, p.standings->w, model->world.h - p.standings->y});
			for (const auto &[id, box] : p.sets)
				add(kSet, id, box);
			introPending_ = true;
			builtAt_ = t;
		}
		add(kTitle, "", {0, 0, static_cast<double>(model->width), kTitleHeight});
		add(kLabel, "", {});
		for (auto &[key, e] : elements_) {
			auto it = model->content.find(key);
			e.content = it == model->content.end() ? 0 : it->second;
		}
		if (refocus)
			applyFocus(t, animate);
	}

	static Box grow(const Box &b, double by) { return {b.x - by, b.y - by, b.w + by * 2, b.h + by * 2}; }

	void applyFocus(double t, bool animate)
	{
		const Model &m = *model_;
		const Focus &f = m.focus;
		const Theme &theme = m.scene.theme;
		double fade = animate ? time(kFadeTime) : 0;

		// Out of focus, dimmed while sets are in focus
		std::set<std::string> outlined;
		for (auto &[key, e] : elements_) {
			bool focused = false;
			if (e.kind == kSet)
				focused = f.sets.count(e.id);
			else if (e.kind == kLine)
				focused = f.lineFocused(m.placed.lines[e.line]);
			else if (e.kind == kRound)
				focused = f.rounds.count(e.id);
			else if (e.kind != kByes && e.kind != kStandings)
				continue;
			e.opacity.go(!f.active || focused ? 1 : theme.focusDim, t, fade);
			if (focused && f.glow && (f.active || e.kind == kRound))
				outlined.insert(keyOf(e.kind == kSet    ? kRing
						      : e.kind == kLine ? kGlowLine
									: kBar,
						      e.id));
		}
		// The outlines: coming and going together
		for (auto &[key, e] : elements_) {
			if ((e.kind == kRing || e.kind == kGlowLine || e.kind == kBar) && !outlined.count(key)) {
				e.opacity.go(0, t, fade);
				e.gone = true;
			}
		}
		for (const std::string &key : outlined) {
			bool exists = elements_.count(key) > 0;
			Kind kind = static_cast<Kind>(std::stoi(key));
			std::string id = key.substr(key.find(':') + 1);
			Element &e = exists ? elements_[key]
					    : add(kind, id,
						  kind == kGlowLine ? grow(m.placed.lines[std::stoi(id)].bounds(), 8)
						  : kind == kRing   ? grow(m.placed.sets.at(id), 36)
								    : m.placed.headers.at(id),
						  kind == kGlowLine ? std::stoi(id) : -1);
			if (!exists) {
				e.opacity = Anim{0, 0, t, 0};
				e.content = m.content.count(key) ? m.content.at(key) : 0;
			}
			e.gone = false;
			e.opacity.go(1, t, fade);
		}

		// The caption: faded out, filled in, faded in as the camera gets there
		std::string label = f.active ? f.label : "";
		bool shown = !label.empty() && theme.focusLabel;
		if (label != shownLabel_ || shown != labelShown_) {
			double out = animate && labelShown_ ? time(0.25) : 0;
			labelOut_ = Anim{labelAlpha(t), 0, t, out};
			labelIn_ = Anim{0, shown ? 1.0 : 0.0,
					t + std::max(out, animate ? time(theme.focusTime * 0.6) : 0),
					animate ? time(0.4) : 0};
			labelSwap_ = t + out;
			shownLabel_ = label;
			labelShown_ = shown;
		}

		// The camera: from wherever it is, even halfway through a move
		double vw = m.width, vh = viewportHeight();
		View to = fitView(f.box, vw, vh, shown ? kLabelRoom : 0, m.maxZoom);
		double duration = time(theme.focusTime);
		if (!animate || !hasView_ || duration < 0.02) {
			view_ = snapped(to);
			moving_ = false;
		} else {
			View from = viewNow(t);
			moveEaseOut_ = moving_ && t < moveStart_ + moveTime_;
			moveFrom_ = from;
			view_ = snapped(to);
			moveStart_ = t;
			moveTime_ = duration;
			moving_ = true;
		}
		hasView_ = true;
		baseScale_ = fitView(m.world, vw, vh, 0, m.maxZoom).scale;
	}

	double labelAlpha(double t) const { return t < labelSwap_ ? labelOut_.at(t) : labelIn_.at(t); }

	/** Sizes the pictures are painted at while moving: steps of √2 */
	static double step(double scale) { return std::pow(2.0, std::ceil(std::log2(scale) * 2 - 1e-9) / 2); }

	/**
	 * Asks the source's thread for the pictures missing for where the camera
	 * is and is going, most needed first: at rest, the middle of the screen
	 * first; moving, what stays on screen longest (where the camera is going).
	 */
	void request(double t)
	{
		const Model &m = *model_;
		double vw = m.width, vh = viewportHeight();
		View v = viewNow(t);
		bool moving = moving_ && t < moveStart_ + moveTime_;
		moving_ = moving;
		// Where the camera will be, every 80 ms to the end of its move
		std::vector<std::pair<Box, double>> ahead;
		for (double s = t; moving && s < moveStart_ + moveTime_ + 0.1 && ahead.size() < 40; s += 0.08) {
			View f = viewNow(s);
			ahead.push_back({f.shows(vw, vh), f.scale});
		}
		Box shown = v.shows(vw, vh);
		uint64_t assets = Assets::instance() ? Assets::instance()->version() : 0;
		double base = step(baseScale_);

		std::vector<std::pair<double, Job>> wants;
		for (auto it = elements_.begin(); it != elements_.end();) {
			Element &e = it->second;
			if (e.gone && e.opacity.at(t) <= 0) {
				obs_enter_graphics();
				e.tex.release();
				e.old.release();
				obs_leave_graphics();
				it = elements_.erase(it);
				orderDirty_ = true;
				continue;
			}
			bool page = e.kind == kTitle || e.kind == kLabel;
			bool visible = page || overlaps(e.box, shown);
			double scale = 0, priority = 0;
			if (page) {
				bool hidden = (e.kind == kTitle && !m.showTitle) ||
					      (e.kind == kLabel && shownLabel_.empty());
				scale = hidden ? 0 : 1;
				priority = 1e6;
			} else if (visible && !moving) {
				// At rest: as sharp as can be
				scale = v.scale;
				priority = 1000 - std::hypot((e.box.x + e.box.w / 2 - shown.x - shown.w / 2) / shown.w,
							     (e.box.y + e.box.h / 2 - shown.y - shown.h / 2) / shown.h);
			} else if (moving) {
				// As big as it gets in the next half second on screen (at most
				// twice as big as now), unless what it has is close enough
				int first = -1, last = -1;
				double most = 0;
				for (size_t i = 0; i < ahead.size(); i++) {
					if (!overlaps(e.box, ahead[i].first))
						continue;
					first = first < 0 ? static_cast<int>(i) : first;
					last = static_cast<int>(i);
					if (i * 0.08 < 0.5 || most == 0)
						most = std::max(most, std::min(ahead[i].second, v.scale * 2));
				}
				// Not worth painting bigger when it's leaving the screen
				bool leaving = visible && e.tex.tex && last + 1 < static_cast<int>(ahead.size()) &&
					       last * 0.08 < 0.2;
				if (most > 0 && !leaving) {
					bool close = e.tex.tex && e.tex.scale >= most * 0.75 && e.tex.scale <= most * 3;
					scale = close ? e.tex.scale : step(most);
					priority = 500 + last * 10 - first;
				}
			}
			bool stale = e.tex.content != e.content || (e.tex.incomplete && e.tex.assets != assets);
			// Off screen, a small one: there at once when the camera gets there
			if (scale == 0 && !e.gone && (!e.tex.tex || (!visible && e.tex.scale > base * 2.5) || stale))
				scale = base;
			scale = std::min(scale, kMaxTexture / std::max({e.box.w, e.box.h, 1.0}));
			if (scale > 0 && (!e.tex.tex || e.tex.scale != scale || stale))
				wants.push_back(
					{priority,
					 {model_, it->first, e.kind, e.id, e.line, e.box, scale, e.content, assets}});
			++it;
		}
		std::stable_sort(wants.begin(), wants.end(),
				 [](const auto &a, const auto &b) { return a.first > b.first; });
		std::lock_guard<std::mutex> lock(mutex_);
		// Not the one being painted, or painted and not taken yet
		auto done = [this](const Job &job) {
			auto same = [&job](const std::string &key, double scale, size_t content, uint64_t version) {
				return key == job.key && scale == job.scale && content == job.content &&
				       version == job.assets;
			};
			return same(busy_.key, busy_.scale, busy_.content, busy_.assets) ||
			       std::any_of(results_.begin(), results_.end(), [&same](const Result &r) {
				       return same(r.key, r.scale, r.content, r.assets);
			       });
		};
		jobs_.clear();
		for (auto &[priority, job] : wants)
			if (!done(job))
				jobs_.push_back(std::move(job));
		if (!jobs_.empty())
			wake_.notify_all();
	}

	// -----------------------------------------------------------------
	// Drawing

	void upload(double t)
	{
		std::vector<Result> results;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			results.swap(results_);
		}
		for (Result &r : results) {
			auto it = elements_.find(r.key);
			if (it == elements_.end())
				continue;
			Element &e = it->second;
			if (r.content != e.content || r.sprite.image.isNull())
				continue;
			const QImage &image = r.sprite.image;
			const uint8_t *data = image.constBits();
			gs_texture_t *tex = gs_texture_create(static_cast<uint32_t>(image.width()),
							      static_cast<uint32_t>(image.height()), GS_BGRA, 1, &data,
							      0);
			if (!tex)
				continue;
			if (e.tex.tex && e.tex.content != r.content) {
				// The new content fades in over the old (a new caption goes in
				// once the old one has faded out)
				e.old.release();
				e.old = e.tex;
				e.fadeStart = t;
			} else {
				e.tex.release();
			}
			e.tex = {tex, r.sprite.at, r.scale, r.content, r.sprite.incomplete, r.assets};
		}
	}

	void startIntro(double t)
	{
		introPending_ = false;
		if (speed() <= 0)
			return;
		std::vector<Element *> sets;
		for (auto &[key, e] : elements_) {
			if (e.kind == kSet)
				sets.push_back(&e);
			else if (e.kind == kLine || e.kind == kRound)
				e.introAt = t + time(0.3);
		}
		std::stable_sort(sets.begin(), sets.end(), [](Element *a, Element *b) { return a->box.x < b->box.x; });
		double stagger = sets.empty() ? 0 : std::min(0.015, kStaggerTime / sets.size());
		for (size_t i = 0; i < sets.size(); i++)
			sets[i]->introAt = t + time(stagger * i);
	}

	void drawTexture(const Texture &tex, double x, double y, double scale, double alpha)
	{
		if (!tex.tex || alpha <= 0.002)
			return;
		struct vec4 color;
		vec4_set(&color, static_cast<float>(alpha), static_cast<float>(alpha), static_cast<float>(alpha),
			 static_cast<float>(alpha));
		gs_effect_set_texture(imageParam_, tex.tex);
		gs_effect_set_vec4(colorParam_, &color);
		gs_matrix_push();
		gs_matrix_translate3f(static_cast<float>(x + tex.at.x * scale),
				      static_cast<float>(y + tex.at.y * scale), 0);
		gs_matrix_scale3f(static_cast<float>(tex.at.w * scale / gs_texture_get_width(tex.tex)),
				  static_cast<float>(tex.at.h * scale / gs_texture_get_height(tex.tex)), 1);
		gs_draw_sprite(tex.tex, 0, 0, 0);
		gs_matrix_pop();
	}

	void drawElement(const Element &e, double x, double y, double scale, double alpha, double t)
	{
		double fade = e.old.tex ? std::min(1.0, (t - e.fadeStart) / std::max(0.001, time(kContentFade))) : 1;
		if (e.old.tex && fade < 1)
			drawTexture(e.old, x, y, scale, alpha);
		drawTexture(e.tex, x, y, scale, alpha * easeSine(fade));
	}

	void render()
	{
		// Drawn once per frame, however many views show the source
		if (!model_ || !effect_ || orderDirty_)
			return;
		double t = frame_;

		const Model &m = *model_;
		uint32_t vw = m.width, vh = static_cast<uint32_t>(viewportHeight());
		View v = viewNow(t);
		bool srgb = gs_framebuffer_srgb_enabled();
		gs_enable_framebuffer_srgb(false);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		gs_technique_t *tech = gs_effect_get_technique(effect_, "Draw");

		// The world, through the viewport: once per frame
		if (!worldDrawn_)
			gs_texrender_reset(world_);
		if (!worldDrawn_ && gs_texrender_begin(world_, vw, vh)) {
			worldDrawn_ = true;
			struct vec4 clear;
			vec4_zero(&clear);
			gs_clear(GS_CLEAR_COLOR, &clear, 0, 0);
			gs_ortho(0, static_cast<float>(vw), 0, static_cast<float>(vh), -100, 100);
			gs_technique_begin(tech);
			gs_technique_begin_pass(tech, 0);
			Box shown = v.shows(vw, vh);
			for (const Element *e : order_) {
				if (introPending_ || e->kind == kTitle || e->kind == kLabel ||
				    !overlaps(e->tex.tex ? e->tex.at : e->box, shown))
					continue;
				double alpha = e->opacity.at(t), dx = 0;
				if (e->introAt >= 0) {
					double k = std::clamp((t - e->introAt) /
								      time(e->kind == kSet ? kSetIn : kFadeTime),
							      0.0, 1.0);
					double eased = 1 - (1 - k) * (1 - k);
					alpha *= eased;
					if (e->kind == kSet)
						dx = -40 * (1 - eased);
				}
				drawElement(*e, v.left() + dx * v.scale, v.top(), v.scale, alpha, t);
			}
			gs_technique_end_pass(tech);
			gs_technique_end(tech);
			gs_texrender_end(world_);
		}

		// The page: the title, the world under it, the caption
		gs_technique_begin(tech);
		gs_technique_begin_pass(tech, 0);
		double top = m.showTitle ? kTitleHeight : 0;
		if (m.showTitle)
			drawElement(elements_[keyOf(kTitle, "")], 0, 0, 1, 1, t);
		Texture world{gs_texrender_get_texture(world_),
			      {0, 0, static_cast<double>(vw), static_cast<double>(vh)}};
		drawTexture(world, 0, top, 1, 1);
		const Element &label = elements_[keyOf(kLabel, "")];
		drawTexture(t < labelSwap_ && label.old.tex ? label.old : label.tex, m.width / 2.0, m.height - 32.0, 1,
			    labelAlpha(t));
		gs_technique_end_pass(tech);
		gs_technique_end(tech);

		gs_blend_state_pop();
		gs_enable_framebuffer_srgb(srgb);
	}

private:
	obs_source_t *self_;
	std::atomic<uint32_t> width_{1920}, height_{1080};
	std::thread thread_;

	// Shared with the source's thread
	std::mutex mutex_;
	std::condition_variable wake_;
	bool stopping_ = false;
	bool dirty_ = true;
	Options options_;
	std::deque<Job> jobs_;
	/** The job being painted */
	Job busy_{};
	std::vector<Result> results_;
	std::shared_ptr<const Model> pending_;

	// The source's thread's
	json lastInput_;
	bool warned_ = false;

	// The graphics thread's
	std::shared_ptr<const Model> model_;
	std::map<std::string, Element> elements_;
	std::vector<Element *> order_;
	bool orderDirty_ = true;
	bool introPending_ = false;
	double builtAt_ = 0;
	gs_effect_t *effect_ = nullptr;
	gs_eparam_t *imageParam_ = nullptr, *colorParam_ = nullptr;
	double frame_ = 0;
	bool worldDrawn_ = false;
	gs_texrender_t *world_ = nullptr;
	View view_, moveFrom_;
	bool hasView_ = false, moving_ = false, moveEaseOut_ = false;
	double moveStart_ = 0, moveTime_ = 0, baseScale_ = 1;
	std::string shownLabel_;
	bool labelShown_ = false;
	Anim labelOut_{0, 0, 0, 0}, labelIn_{0, 0, 0, 0};
	double labelSwap_ = 0;
};

// ---------------------------------------------------------------------
// OBS

const char *getName(void *)
{
	return obs_module_text("HyperDrive.BracketFocus");
}

void *create(obs_data_t *settings, obs_source_t *self)
{
	Source *source = new Source(self);
	source->update(settings);
	return source;
}

void destroy(void *data)
{
	delete static_cast<Source *>(data);
}

void update(void *data, obs_data_t *settings)
{
	static_cast<Source *>(data)->update(settings);
}

void getDefaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "channel", "main");
	obs_data_set_default_bool(settings, "title", true);
	obs_data_set_default_bool(settings, "ignore_focus", false);
	obs_data_set_default_int(settings, "max_zoom", 0);
	obs_data_set_default_int(settings, "width", 1920);
	obs_data_set_default_int(settings, "height", 1080);
}

obs_properties_t *getProperties(void *)
{
	obs_properties_t *props = obs_properties_create();
	obs_property_t *channel = obs_properties_add_list(props, "channel",
							  obs_module_text("HyperDrive.Layout.Channel"),
							  OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_STRING);
	std::vector<std::string> channels{"main"};
	if (Companion *companion = Companion::instance())
		companion->client().withState(
			[&channels](const nlohmann::json &state) { channels = hd::bracketChannels(state); });
	for (const std::string &name : channels)
		obs_property_list_add_string(channel, name.c_str(), name.c_str());
	obs_property_set_long_description(channel, obs_module_text("HyperDrive.Layout.Channel.Help"));
	obs_properties_add_bool(props, "title", obs_module_text("HyperDrive.BracketFocus.Title"));
	obs_properties_add_bool(props, "ignore_focus", obs_module_text("HyperDrive.BracketFocus.IgnoreFocus"));
	obs_property_t *zoom = obs_properties_add_int_slider(
		props, "max_zoom", obs_module_text("HyperDrive.BracketFocus.MaxZoom"), 0, 500, 10);
	obs_property_int_set_suffix(zoom, "%");
	obs_property_set_long_description(zoom, obs_module_text("HyperDrive.BracketFocus.MaxZoom.Help"));
	obs_properties_add_int(props, "width", obs_module_text("HyperDrive.Layout.Width"), 1, 8192, 1);
	obs_properties_add_int(props, "height", obs_module_text("HyperDrive.Layout.Height"), 1, 8192, 1);
	obs_property_t *info =
		obs_properties_add_text(props, "info", obs_module_text("HyperDrive.BracketFocus.Info"), OBS_TEXT_INFO);
	obs_property_text_set_info_word_wrap(info, true);
	return props;
}

} // namespace

void bracket_source_register()
{
	hd::bracket::Assets::create();
	obs_source_info info = {};
	info.id = kId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	info.icon_type = OBS_ICON_TYPE_SLIDESHOW;
	info.get_name = getName;
	info.create = create;
	info.destroy = destroy;
	info.update = update;
	info.get_defaults = getDefaults;
	info.get_properties = getProperties;
	info.get_width = [](void *data) {
		return static_cast<Source *>(data)->width();
	};
	info.get_height = [](void *data) {
		return static_cast<Source *>(data)->height();
	};
	info.video_tick = [](void *data, float) {
		static_cast<Source *>(data)->tick();
	};
	info.video_render = [](void *data, gs_effect_t *) {
		static_cast<Source *>(data)->render();
	};
	obs_register_source(&info);
}

void bracket_source_shutdown()
{
	{
		std::lock_guard<std::mutex> lock(sourcesMutex);
		for (Source *source : sources)
			source->stop();
	}
	hd::bracket::Assets::destroy();
}
