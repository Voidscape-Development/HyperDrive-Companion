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
#include "layout-source.hpp"

#include <string>

#include <QDesktopServices>
#include <QUrl>

#include <obs-module.h>

#include "companion.hpp"
#include "core/layouts.hpp"
#include "plugin-support.h"

namespace {

constexpr const char *kId = "hyperdrive_layout";
constexpr const char *kDefaultCss = "body { background-color: rgba(0, 0, 0, 0); margin: 0px auto; overflow: hidden; }";

struct LayoutSource {
	obs_source_t *self = nullptr;
	obs_source_t *browser = nullptr;
	uint32_t width = 1920;
	uint32_t height = 1080;
	std::string url;
};

std::string pageUrl(obs_data_t *settings)
{
	Companion *companion = Companion::instance();
	std::string page = obs_data_get_string(settings, "page");
	if (!companion || page.empty())
		return "";
	hd::LayoutOptions options;
	options.scoreboard = static_cast<int>(obs_data_get_int(settings, "scoreboard"));
	options.display = obs_data_get_string(settings, "display");
	options.channel = obs_data_get_string(settings, "channel");
	options.extra = obs_data_get_string(settings, "extra_query");
	return hd::layoutUrl(companion->baseUrl(), page, options);
}

std::string previewUrl(obs_data_t *settings)
{
	Companion *companion = Companion::instance();
	std::string page = obs_data_get_string(settings, "page");
	if (!companion || page.empty())
		return "";
	for (const hd::LayoutFolder &folder : companion->layouts()) {
		for (const hd::LayoutPage &p : folder.pages) {
			if (p.path == page && !p.preview.empty())
				return hd::layoutUrl(companion->baseUrl(), p.preview, hd::LayoutOptions());
		}
	}
	return "";
}

/** The folder a page is in: "layout/<folder>/<page>.html" */
std::string folderOf(const std::string &page)
{
	const std::string prefix = "layout/";
	if (page.compare(0, prefix.size(), prefix) != 0)
		return "";
	size_t slash = page.find('/', prefix.size());
	return slash == std::string::npos ? "" : page.substr(prefix.size(), slash - prefix.size());
}

void fillPages(obs_properties_t *props, obs_data_t *settings)
{
	obs_property_t *list = obs_properties_get(props, "page");
	obs_property_list_clear(list);
	std::string folder = obs_data_get_string(settings, "folder");
	std::string page = obs_data_get_string(settings, "page");
	bool found = false;
	if (Companion *companion = Companion::instance()) {
		for (const hd::LayoutFolder &f : companion->layouts()) {
			if (f.folder != folder)
				continue;
			for (const hd::LayoutPage &p : f.pages) {
				obs_property_list_add_string(list, p.name.c_str(), p.path.c_str());
				found = found || p.path == page;
			}
		}
	}
	// Keep a page that isn't listed (HyperDrive is off, the layout was removed)
	if (!found && !page.empty())
		obs_property_list_insert_string(list, 0, page.c_str(), page.c_str());
}

void fillFolders(obs_properties_t *props, obs_data_t *settings)
{
	obs_property_t *list = obs_properties_get(props, "folder");
	obs_property_list_clear(list);
	std::string folder = obs_data_get_string(settings, "folder");
	bool found = false;
	size_t count = 0;
	bool fromServer = false;
	if (Companion *companion = Companion::instance()) {
		for (const hd::LayoutFolder &f : companion->layouts()) {
			obs_property_list_add_string(list, f.folder.c_str(), f.folder.c_str());
			found = found || f.folder == folder;
			count++;
		}
		fromServer = companion->layoutsFromServer();
	}
	if (!found && !folder.empty())
		obs_property_list_insert_string(list, 0, folder.c_str(), folder.c_str());

	obs_property_t *info = obs_properties_get(props, "layouts_info");
	obs_property_set_visible(info, count == 0 || !fromServer);
	obs_property_set_description(info, obs_module_text(count == 0 ? "HyperDrive.Layout.NoLayouts"
								      : "HyperDrive.Layout.FromFolder"));
	fillPages(props, settings);
}

bool folderModified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	std::string folder = obs_data_get_string(settings, "folder");
	std::string page = obs_data_get_string(settings, "page");
	if (folderOf(page) != folder) {
		// The folder's first page, preferably its index
		std::string first;
		if (Companion *companion = Companion::instance()) {
			for (const hd::LayoutFolder &f : companion->layouts()) {
				if (f.folder != folder)
					continue;
				for (const hd::LayoutPage &p : f.pages) {
					if (first.empty() || p.name == "index")
						first = p.path;
				}
			}
		}
		obs_data_set_string(settings, "page", first.c_str());
	}
	fillPages(props, settings);
	return true;
}

bool refreshList(obs_properties_t *props, obs_property_t *, void *data)
{
	if (Companion *companion = Companion::instance())
		companion->layouts(true);
	LayoutSource *source = static_cast<LayoutSource *>(data);
	obs_data_t *settings = obs_source_get_settings(source->self);
	fillFolders(props, settings);
	obs_data_release(settings);
	return true;
}

bool reloadPage(obs_properties_t *, obs_property_t *, void *data)
{
	LayoutSource *source = static_cast<LayoutSource *>(data);
	if (!source->browser)
		return false;
	obs_properties_t *browserProps = obs_source_properties(source->browser);
	obs_property_t *refresh = obs_properties_get(browserProps, "refreshnocache");
	if (refresh)
		obs_property_button_clicked(refresh, source->browser);
	obs_properties_destroy(browserProps);
	return false;
}

bool openInBrowser(obs_properties_t *, obs_property_t *, void *data)
{
	LayoutSource *source = static_cast<LayoutSource *>(data);
	if (!source->url.empty())
		QDesktopServices::openUrl(QUrl(QString::fromStdString(source->url)));
	return false;
}

bool openPreview(obs_properties_t *, obs_property_t *, void *data)
{
	LayoutSource *source = static_cast<LayoutSource *>(data);
	obs_data_t *settings = obs_source_get_settings(source->self);
	std::string url = previewUrl(settings);
	obs_data_release(settings);
	if (!url.empty())
		QDesktopServices::openUrl(QUrl(QString::fromStdString(url)));
	return false;
}

const char *getName(void *)
{
	return obs_module_text("HyperDrive.Layout");
}

void update(void *data, obs_data_t *settings)
{
	LayoutSource *source = static_cast<LayoutSource *>(data);
	source->width = static_cast<uint32_t>(obs_data_get_int(settings, "width"));
	source->height = static_cast<uint32_t>(obs_data_get_int(settings, "height"));
	source->url = pageUrl(settings);
	if (!source->browser)
		return;

	obs_data_t *browser = obs_data_create();
	obs_data_set_bool(browser, "is_local_file", false);
	obs_data_set_string(browser, "url", source->url.empty() ? "about:blank" : source->url.c_str());
	obs_data_set_int(browser, "width", source->width);
	obs_data_set_int(browser, "height", source->height);
	obs_data_set_string(browser, "css", obs_data_get_string(settings, "css"));
	obs_data_set_bool(browser, "shutdown", obs_data_get_bool(settings, "shutdown"));
	obs_data_set_bool(browser, "restart_when_active", obs_data_get_bool(settings, "restart_when_active"));
	obs_data_set_bool(browser, "reroute_audio", false);
	obs_source_update(source->browser, browser);
	obs_data_release(browser);
}

void *create(obs_data_t *settings, obs_source_t *self)
{
	LayoutSource *source = new LayoutSource();
	source->self = self;

	std::string name = std::string(obs_source_get_name(self)) + " (HyperDrive)";
	obs_data_t *browser = obs_data_create();
	obs_data_set_string(browser, "url", "about:blank");
	source->browser = obs_source_create_private("browser_source", name.c_str(), browser);
	obs_data_release(browser);
	// Without obs-browser, OBS still creates a source, which shows nothing
	if (!obs_get_source_output_flags("browser_source"))
		obs_log(LOG_ERROR, "\"%s\" can't show its layout: OBS has no Browser source (obs-browser)",
			obs_source_get_name(self));
	if (source->browser)
		obs_source_add_active_child(self, source->browser);

	update(source, settings);
	return source;
}

void destroy(void *data)
{
	LayoutSource *source = static_cast<LayoutSource *>(data);
	if (source->browser) {
		obs_source_remove_active_child(source->self, source->browser);
		obs_source_release(source->browser);
	}
	delete source;
}

void getDefaults(obs_data_t *settings)
{
	obs_data_set_default_string(settings, "folder", "scoreboard_simple");
	obs_data_set_default_string(settings, "page", "layout/scoreboard_simple/index.html");
	obs_data_set_default_int(settings, "scoreboard", 1);
	obs_data_set_default_int(settings, "width", 1920);
	obs_data_set_default_int(settings, "height", 1080);
	obs_data_set_default_string(settings, "css", kDefaultCss);
	obs_data_set_default_bool(settings, "shutdown", false);
	obs_data_set_default_bool(settings, "restart_when_active", false);
}

obs_properties_t *getProperties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_property_t *folder = obs_properties_add_list(props, "folder", obs_module_text("HyperDrive.Layout.Folder"),
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_set_modified_callback(folder, folderModified);
	obs_properties_add_list(props, "page", obs_module_text("HyperDrive.Layout.Page"), OBS_COMBO_TYPE_LIST,
				OBS_COMBO_FORMAT_STRING);
	obs_property_t *info = obs_properties_add_text(props, "layouts_info", "", OBS_TEXT_INFO);
	obs_property_text_set_info_type(info, OBS_TEXT_INFO_WARNING);
	obs_properties_add_button2(props, "refresh_list", obs_module_text("HyperDrive.Layout.RefreshList"), refreshList,
				   data);

	obs_properties_add_int(props, "scoreboard", obs_module_text("HyperDrive.Layout.Scoreboard"), 1, 32, 1);
	obs_property_t *display = obs_properties_add_text(
		props, "display", obs_module_text("HyperDrive.Layout.Display"), OBS_TEXT_DEFAULT);
	obs_property_set_long_description(display, obs_module_text("HyperDrive.Layout.Display.Help"));
	obs_property_t *channel = obs_properties_add_text(
		props, "channel", obs_module_text("HyperDrive.Layout.Channel"), OBS_TEXT_DEFAULT);
	obs_property_set_long_description(channel, obs_module_text("HyperDrive.Layout.Channel.Help"));
	obs_properties_add_text(props, "extra_query", obs_module_text("HyperDrive.Layout.ExtraQuery"),
				OBS_TEXT_DEFAULT);

	obs_properties_add_int(props, "width", obs_module_text("HyperDrive.Layout.Width"), 1, 8192, 1);
	obs_properties_add_int(props, "height", obs_module_text("HyperDrive.Layout.Height"), 1, 8192, 1);
	obs_properties_add_text(props, "css", obs_module_text("HyperDrive.Layout.Css"), OBS_TEXT_MULTILINE);
	obs_properties_add_bool(props, "shutdown", obs_module_text("HyperDrive.Layout.Shutdown"));
	obs_properties_add_bool(props, "restart_when_active", obs_module_text("HyperDrive.Layout.RestartWhenActive"));

	obs_properties_add_button2(props, "reload", obs_module_text("HyperDrive.Layout.Reload"), reloadPage, data);
	obs_properties_add_button2(props, "open", obs_module_text("HyperDrive.Layout.Open"), openInBrowser, data);
	obs_properties_add_button2(props, "preview", obs_module_text("HyperDrive.Layout.Preview"), openPreview, data);

	if (data) {
		LayoutSource *source = static_cast<LayoutSource *>(data);
		obs_data_t *settings = obs_source_get_settings(source->self);
		fillFolders(props, settings);
		obs_data_release(settings);
	}
	return props;
}

uint32_t getWidth(void *data)
{
	return static_cast<LayoutSource *>(data)->width;
}

uint32_t getHeight(void *data)
{
	return static_cast<LayoutSource *>(data)->height;
}

void videoRender(void *data, gs_effect_t *)
{
	LayoutSource *source = static_cast<LayoutSource *>(data);
	if (source->browser)
		obs_source_video_render(source->browser);
}

void enumActiveSources(void *data, obs_source_enum_proc_t callback, void *param)
{
	LayoutSource *source = static_cast<LayoutSource *>(data);
	if (source->browser)
		callback(source->self, source->browser, param);
}

// Clicks and keys go to the page, as with a Browser source's Interact
void mouseClick(void *data, const struct obs_mouse_event *event, int32_t type, bool up, uint32_t count)
{
	LayoutSource *source = static_cast<LayoutSource *>(data);
	if (source->browser)
		obs_source_send_mouse_click(source->browser, event, type, up, count);
}

void mouseMove(void *data, const struct obs_mouse_event *event, bool leave)
{
	LayoutSource *source = static_cast<LayoutSource *>(data);
	if (source->browser)
		obs_source_send_mouse_move(source->browser, event, leave);
}

void mouseWheel(void *data, const struct obs_mouse_event *event, int xDelta, int yDelta)
{
	LayoutSource *source = static_cast<LayoutSource *>(data);
	if (source->browser)
		obs_source_send_mouse_wheel(source->browser, event, xDelta, yDelta);
}

void focus(void *data, bool focused)
{
	LayoutSource *source = static_cast<LayoutSource *>(data);
	if (source->browser)
		obs_source_send_focus(source->browser, focused);
}

void keyClick(void *data, const struct obs_key_event *event, bool keyUp)
{
	LayoutSource *source = static_cast<LayoutSource *>(data);
	if (source->browser)
		obs_source_send_key_click(source->browser, event, keyUp);
}

} // namespace

void layout_source_register()
{
	obs_source_info info = {};
	info.id = kId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_INTERACTION | OBS_SOURCE_SRGB;
	info.icon_type = OBS_ICON_TYPE_BROWSER;
	info.get_name = getName;
	info.create = create;
	info.destroy = destroy;
	info.update = update;
	info.get_defaults = getDefaults;
	info.get_properties = getProperties;
	info.get_width = getWidth;
	info.get_height = getHeight;
	info.video_render = videoRender;
	info.enum_active_sources = enumActiveSources;
	info.mouse_click = mouseClick;
	info.mouse_move = mouseMove;
	info.mouse_wheel = mouseWheel;
	info.focus = focus;
	info.key_click = keyClick;
	obs_register_source(&info);
}
