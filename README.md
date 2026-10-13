# HyperDrive Companion

An OBS Studio plugin for [HyperDrive](https://github.com/Voidscape-Development/HyperDrive), the
stream helper for fighting game tournaments. It runs HyperDrive from inside OBS and ties your
scenes to what happens in HyperDrive:

- **A HyperDrive dock** with the scoreboard (scores, swap, team colors, resets), the stream
  queue, the crew battle, the stage strike, the bracket focus and the Display Controls, live from
  HyperDrive.
- **A HyperDrive Layout source**: pick a layout and its page from a list, plus its options
  (scoreboard, display groups, bracket channel). No more typing URLs or browsing for `.html` files.
- **Scenes › Layouts**: when a scene goes live, show or hide HyperDrive layouts with their
  animations (HyperDrive's Display Controls).
- **Scene automation**: switch scenes when a set is won, a new set is loaded, the stage strike
  starts, the stage is picked, or the score changes.
- **Hotkeys** for everything the dock does.

It needs OBS Studio 31 or newer, on Windows, macOS or Linux, and HyperDrive running on the same
computer or on your network.

## Installing

Download the package for your system from the
[releases page](https://github.com/Voidscape-Development/HyperDrive-Companion/releases) and
install it (the Windows and macOS installers, or the `.deb` on Ubuntu). Then restart OBS.

## Using it

### Connecting

The plugin connects to HyperDrive at `localhost:5500`. If HyperDrive runs on another computer, or
you changed its port, set its address in **Tools › HyperDrive Companion › Connection**. The dock
shows whether it's connected, and keeps reconnecting when HyperDrive restarts.

### The dock

Show it from the **Docks** menu (**HyperDrive**). The picker at the top selects the scoreboard the
dock controls. Its tabs:

| Tab | |
| - | - |
| Score | The teams, their scores (− / +) and colors (click the swatch), the match and best of, swap and reset |
| Queue | The next set of the stream queue for this scoreboard, and loading it, the set on stream (start.gg) or HyperDrive's set selector |
| Crew | The crew battle: each team's stocks or games, the active player (pick one), score/undo, next player and resets |
| Strike | The stage strike: undo, redo, restart, who won rock-paper-scissors and who won the game |
| Bracket | The bracket focus of a channel: previous/next round, whole bracket, follow the scoreboard, tour |
| Display | Every layout folder and group HyperDrive knows: uncheck one to play its hide animation, check it to show it again |

When HyperDrive refuses a command (e.g. nothing to undo), the dock says so. You can turn that off
in the settings.

### HyperDrive Layout source

Add a **HyperDrive Layout** source, then pick the **Layout** (a folder of HyperDrive's `layout/`)
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

### Scenes › Layouts

In **Tools › HyperDrive Companion › Scenes › Layouts**, add rows like
"when **Break** goes live, **hide** `folder:scoreboard`". Layouts are `all`, `folder:<layout
folder>` or `group:<group>`. The layouts play their animations, as with HyperDrive's Display
Controls, instead of being cut by OBS.

### Scene automation

In **Tools › HyperDrive Companion › Automation**, each rule switches to a scene when something
happens on a scoreboard (or any scoreboard):

| When | |
| - | - |
| A set is won | A team reaches the games needed for the scoreboard's best of |
| A new set is loaded | The scoreboard's set changes (by its start.gg/parry.gg id, or its players). Swapping the teams doesn't count |
| The stage strike starts | The players start striking or picking |
| The stage is picked | The game's stage is chosen |
| The score changes | Any point, for the same set |

A rule can wait a few seconds before switching, and can be limited to when a given scene is live
(e.g. only from **Gameplay**). When several rules match, the first one in the list wins. A
switch still waiting is replaced by a later one.

### Hotkeys

Set them in OBS's **Settings › Hotkeys**, under the names starting with "HyperDrive". The
scoreboard ones control the scoreboard picked in **Tools › HyperDrive Companion › Hotkeys**.

The rules, the scene/layout rows, the hotkeys' scoreboard and the keys are saved with the scene
collection. HyperDrive's address is saved for all of them.

## How it works

The plugin keeps one connection to HyperDrive's web server, as the layouts and the Stream Deck
plugin do:

- **State**: socket.io (Engine.IO 4 over a WebSocket). HyperDrive sends the whole program state
  when the plugin connects (`program_state`), then the changes (`program_state_update`), which are
  applied as in HyperDrive's `layout/include/globals.js`. A missed change makes it ask for the
  whole state again.
- **Commands**: the web server's links (`/scoreboard1-team1-scoreup`,
  `/display/folder/<folder>/hide`…), whose answer tells a refused command apart.

| | |
| - | - |
| `src/core/` | Everything that doesn't need OBS or Qt, unit tested: the Engine.IO/socket.io packets, the state and what's read from it, the links, the trigger detection, the layout list, the saved config |
| `src/net/` | The connection, with [IXWebSocket](https://github.com/machinezone/IXWebSocket) |
| `src/companion.*` | The hub on OBS's UI thread: the connection, the settings, the layout list and the automation |
| `src/layout-source.*` | The HyperDrive Layout source, wrapping a private Browser source |
| `src/hotkeys.*` | The hotkeys |
| `src/ui/` | The dock and the settings dialog (Qt) |
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
