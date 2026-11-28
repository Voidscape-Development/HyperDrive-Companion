# HyperDrive Companion

An OBS Studio plugin for [HyperDrive](https://github.com/Voidscape-Development/HyperDrive), the
stream helper for fighting game tournaments. It runs HyperDrive from inside OBS and ties your
scenes to what happens in HyperDrive:

- **Several HyperDrives at once**: every setup, station or side stream at the event, each with a
  name and a color. The dock, the sources, the hotkeys and the rules each pick one.
- **A HyperDrive dock** with the scoreboard (scores, swap, team colors, resets), the stream
  queue, the crew battle, the stage strike, the bracket focus and the Display Controls, live from
  HyperDrive.
- **A HyperDrive Layout source**: pick a layout and its page from a list, plus its options
  (scoreboard, display groups, bracket channel). No more typing URLs or browsing for `.html` files.
- **A HyperDrive Bracket Focus source**: HyperDrive's zooming bracket (`bracket_focus`), drawn by
  OBS itself instead of a browser, so its camera moves stay smooth on any bracket.
- **Automation**: rules of *when* something happens (a set is won, match point, the stage is
  picked, the game changes, a scene goes live, the stream starts, a hotkey…), *only if* some conditions hold, *then*
  actions in order: switch scenes, show or hide layouts with their animations, show or hide
  sources, run any HyperDrive command, record, save the replay buffer, wait. In groups you turn on
  and off together.
- **Hotkeys** for everything the dock does.

It needs OBS Studio 33 or newer, on Windows, macOS or Linux, and HyperDrive running on the same
computer or on your network.

## Installing

Download the package for your system from the
[releases page](https://github.com/Voidscape-Development/HyperDrive-Companion/releases) and
install it (the Windows and macOS installers, or the `.deb` on Ubuntu). Then restart OBS.

## Using it

### Connecting

The plugin connects to HyperDrive at `localhost:5500`. If HyperDrive runs on another computer, or
you changed its port, set its address in **Tools › HyperDrive Companion › Instances**. Add a row
for every other HyperDrive to connect to at the same time (another station, a side stream), with a
name and a color tag. The first one is the default: the sources, hotkeys and rules use it unless
they pick another. Each instance keeps reconnecting when its HyperDrive restarts, without holding
up the others.

### The dock

Show it from the **Docks** menu (**HyperDrive**). The top picks the HyperDrive instance and the
scoreboard the dock controls (the scoreboard buttons show when there's more than one), with its
connection status under it. Its tabs:

| Tab | |
| - | - |
| Score | The round, best of and game, the teams with their scores (− / +), match point and set won, their colors (click the stripe), swap, reset and load the next set |
| Queue | The next set of the stream queue for this scoreboard, and loading it, the set on stream (start.gg) or HyperDrive's set selector |
| Crew | The crew battle: each team's stocks or games, the active player (pick one), score/undo, next player and resets |
| Strike | The stage strike: undo, redo, restart, who won rock-paper-scissors and who won the game |
| Bracket | The bracket focus of a channel: previous/next round, whole bracket, follow the scoreboard, tour |
| Display | Every layout folder and group HyperDrive knows: uncheck one to play its hide animation, check it to show it again |

When HyperDrive refuses a command (e.g. nothing to undo), the dock says so for a moment. You can
turn that off in the settings. The bottom says how many automation rules are armed and what the
last one did; **Edit rules** opens them.

### HyperDrive Layout source

Add a **HyperDrive Layout** source, pick its **HyperDrive** instance (the default one unless you
have several), then the **Layout** (a folder of HyperDrive's `layout/`)
and its **Page**. **See the layout's preview** opens the page's preview image. The options are
the ones HyperDrive's layouts take in their URL:

| Option | |
| - | - |
| Scoreboard | Shows another scoreboard than 1 |
| Display groups | Puts the page in Display Controls groups, e.g. `main` or `main,top8` |
| Bracket channel | Bracket layouts: a channel other than `main` |
| More URL options | Anything else, e.g. `foo=1&bar=2` |

It's a Browser source underneath, 1920x1080 by default, so it also has the width, height, custom
CSS and refresh options. You can interact with it like a Browser source. The page is loaded from
HyperDrive's web server, so the source follows when you change HyperDrive's address.

HyperDrive lists its layouts at `GET /layouts` (added in
[HyperDrive#27](https://github.com/Voidscape-Development/HyperDrive/pull/27)). With an older
HyperDrive, set its folder in **Tools › HyperDrive Companion › Connection** and the plugin lists
the layouts from there.

### HyperDrive Bracket Focus source

The `bracket_focus` layout, drawn natively: the bracket of a channel, zoomed to what's in focus
(from the dock's Bracket tab, the hotkeys, HyperDrive's bracket widget, its web page or a Stream
Deck), the rest dimmed, with the outlines, the caption saying what's in focus and the title bar. It
follows HyperDrive's layout theme (colors, font, corners, what's displayed, focus glow, zoom time
and maximum zoom, animation speed), unless the source overrides it.

| Option | |
| - | - |
| HyperDrive | The instance whose bracket it shows (the default one by default) |
| Bracket channel | The channel shown (`main` by default) |
| Always show the whole bracket | Ignores the focus (`?ignore_focus=1`) |
| Width, height | 1920x1080 by default; **Use canvas size** sets them to OBS's canvas |
| **Camera** | |
| Maximum zoom | How big a set is drawn when zoomed to, at most; 0 for the layout theme's |
| Minimum zoom | How small the bracket is drawn, at least (a big bracket stays readable, cropped); 0 for none |
| Room around what's in focus | 60px by default |
| When nothing is in focus | Show the whole bracket, or stay where it was |
| Camera movement | Smooth, gentle, fast then slow, or steady |
| **Override layout theme** | |
| Out of focus opacity, camera move time, animation speed | -1 for the layout theme's |
| Focus caption, outline, dim losers, round names, pending sets' sources, avatars, characters, flags, seeds | The layout theme's, shown or hidden |
| Custom focus color | One focus color for every mode |
| **Look** | |
| Background | None (transparent), the layout theme's, or a color |
| Intro animation, standings, byes | Shown by default |
| Focus caption position | Where the caption saying what's in focus goes: any of the 8 places along the edges (bottom center by default). The camera keeps that side free |
| Caption margin | From the edge, 32px by default |
| Largest picture size | 2048, 4096 (default) or 8192px: less video memory, or sharper close-ups |
| **Title bar** | The logo, tournament, event, phase and pool above the bracket (`index.html`), or not (`no_title.html`), with any of its fields, 50 to 200% the size, and its background's opacity. A bar takes the top or bottom of the page; floating, it's as wide as what it shows, in a corner over the bracket, which takes the whole page (as with no title); the camera frames what's in focus below or beside its corner when it would be under it, unless asked not to |

The camera moves, fades and outlines run on OBS's graphics thread every frame. Each set, round name
and line is a picture painted on a thread of the source's own at the size the camera needs (ahead
of it while it moves, sharp once it stops), so painting never holds a frame up. It needs a
HyperDrive that places the bracket for it (`bracket.channels.<channel>.placement`); with an older
one, the source shows only its title bar. Images (flags, avatars, characters, the logo) and the
layouts' fonts come from HyperDrive's web server.

### Automation

In **Tools › HyperDrive Companion › Automation**, each rule is:

- **When**: something happens, on a HyperDrive instance (or any) and a scoreboard (or any).
- **Only if**: conditions that must all hold then.
- **Then**: actions, run in order.

| When | |
| - | - |
| A set is won | A team reaches the games needed for the scoreboard's best of |
| A new set is loaded | The scoreboard's set changes (by its start.gg/parry.gg id, or its players). Swapping the teams doesn't count |
| The score changes | Any point, for the same set |
| Match point is reached | A team gets one game away from winning the set |
| The stage strike starts / the stage is picked | The players start striking or picking / the game's stage is chosen |
| The stream queue's next set changes | The set the scoreboard's stream queue loads next |
| A crew player is out | A player of the crew battle loses their last stock |
| The bracket focus changes | On a channel, or any |
| The game changes | To a given game of HyperDrive's, or any. Also when the instance connects with a game already picked |
| HyperDrive connects / can't be reached | The instance comes back, or goes away |
| A scene goes live | A given scene, or any |
| Recording / the stream starts or stops | OBS's |
| An automation hotkey is pressed | **HyperDrive: Automation hotkey 1** to **10** in OBS's hotkeys |
| Every few minutes | Every N minutes |

| Only if | |
| - | - |
| The scene live is / isn't | A scene |
| The set is a best of, the round's name has, a team has won (games) | On the event's scoreboard |
| OBS is recording / streaming, the replay buffer is on | |
| This HyperDrive is connected | An instance |
| The game is / isn't | A game of HyperDrive's |

| Then | |
| - | - |
| Switch scene | In Studio Mode, through the preview and the transition |
| Show / hide layouts | `all`, `folder:<layout folder>` or `group:<group>`, one or several (**+**), with their animations, as with HyperDrive's Display Controls |
| Show / hide a source | In a scene, or the one live |
| Turn a filter on/off | A source's filter |
| HyperDrive command | Anything the hotkeys do (scores, swap, resets, load the next set, stage strike, crew battle, Display Controls, bracket focus), on the event's scoreboard or a given one |
| Wait | Before the next action |
| Start / stop recording, save the replay buffer | |

Every rule that matches runs. HyperDrive actions go to the instance the event came from (for OBS's
events: the rule's instance, or the default one). Rules can be put in **groups** (e.g. "Top 8"):
uncheck a group to turn all its rules off. **Export** saves the rules shown (all, or a group's) to
a file, and **Import** adds a file's rules, e.g. at the next event. **Test run** runs a rule's
actions right away. **Activity** shows what ran, and why.

The rules of older versions (one scene switch each) and their **Scenes › Layouts** rows become
rules the first time a scene collection is loaded.

### Hotkeys

Set them in OBS's **Settings › Hotkeys**, under the names starting with "HyperDrive". They control
the instance and scoreboard picked in **Tools › HyperDrive Companion › Hotkeys** (by default, the
instance in the dock). The **Automation hotkey** ones trigger rules.

The rules, their groups, the hotkeys' instance and scoreboard and the keys are saved with the scene
collection. The instances are saved for all of them.

## How it works

The plugin keeps one connection to each HyperDrive instance's web server, as the layouts and the
Stream Deck plugin do:

- **State**: socket.io (Engine.IO 4 over a WebSocket). HyperDrive sends the whole program state
  when the plugin connects (`program_state`), then the changes (`program_state_update`), which are
  applied as in HyperDrive's `layout/include/globals.js`. A missed change makes it ask for the
  whole state again.
- **Commands**: the web server's links (`/scoreboard1-team1-scoreup`,
  `/display/folder/<folder>/hide`…), whose answer tells a refused command apart.

| | |
| - | - |
| `src/core/` | Everything that doesn't need OBS or Qt, unit tested: the Engine.IO/socket.io packets, the state and what's read from it, the links, the automation (triggers, rule matching, conditions), the layout list, the saved config (instances, rules, groups, and the migration of older ones), the bracket focus's theme, focus and camera |
| `src/net/` | The connection, with [IXWebSocket](https://github.com/machinezone/IXWebSocket) |
| `src/companion.*` | The hub on OBS's UI thread: the instances' connections, the settings, the layout lists and the automation (running the rules' actions) |
| `src/layout-source.*` | The HyperDrive Layout source, wrapping a private Browser source |
| `src/bracket/` | The HyperDrive Bracket Focus source (`source.*`) and its pieces' painting (`paint.*`); what it shows (placement, theme, focus, camera) is in `src/core/bracket-focus.*` |
| `src/hotkeys.*` | The hotkeys |
| `src/ui/` | The dock and the settings dialog (Qt), and what they share (`style.hpp`) |
| `src/plugin-main.cpp` | Registers everything, and saves to/loads from the scene collection |
| `tests/` | The unit tests of `src/core/` |
| `data/locale/en-US.ini` | The plugin's text |

## Building

The plugin uses the [OBS plugin template](https://github.com/obsproject/obs-plugintemplate)'s
build system. See its wiki for each platform's requirements. In short:

```sh
cmake --preset ubuntu-x86_64      # or macos, windows-x64
cmake --build --preset ubuntu-x86_64
```

[nlohmann/json](https://github.com/nlohmann/json) and IXWebSocket are fetched at configure time,
at the versions pinned in `cmake/dependencies.cmake`.

The unit tests need neither OBS nor Qt:

```sh
cmake -S tests -B build_tests
cmake --build build_tests
ctest --test-dir build_tests --output-on-failure
```

GitHub Actions checks the formatting (clang-format 19 and gersemi), runs the unit tests, and builds
the plugin for Windows, macOS and Ubuntu on pull requests and pushes. To make a release, push a
version tag (e.g. `0.1.0`) to `main`: a draft release is created with the installers. Bump
`version` in `buildspec.json` first.

## Translations

The text is in `data/locale/en-US.ini`. To add a language, copy it to `<locale>.ini` (e.g.
`fr-FR.ini`) and translate the values.

## License

GPL-2.0-or-later, as OBS Studio. See [`LICENSE`](LICENSE).
