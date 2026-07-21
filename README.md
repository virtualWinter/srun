# srun

A tiny [rofi](https://github.com/davatorium/rofi)/dmenu-like application
launcher for Wayland compositors (written for **swm**, but works with any
xdg-shell compositor).

It's a normal Wayland client: a floating window with a text entry and a
filterable list of applications read from your `.desktop` files. Type to
filter, move the selection with the arrow keys (or Tab), press Enter to
launch, Escape (or the window's close button) to quit.

## Build & install

```sh
make              # builds build/srun (also generates the xdg-shell protocol)
make run          # build and launch srun
make install    # installs to ~/.local/bin/srun (PREFIX overridable; no root needed)
```

Dependencies (auto-detected via `pkg-config`): `wayland-client`, `xkbcommon`,
`cairo`, plus `wayland-protocols` / `wayland-scanner` (to generate the
xdg-shell client protocol at build time).

## Usage

Run `srun` (e.g. from a terminal, or bound to a key in your compositor).

**Keyboard**

| Key                 | Action                                            |
|---------------------|---------------------------------------------------|
| Type                | Filter the application list (name or command)     |
| `Up` / `Down`       | Move selection                                    |
| `Tab` / `Shift+Tab` | Cycle selection forward / backward                |
| `Enter`             | Launch the selected application, then quit        |
| `Escape`            | Quit without launching                            |
| `Backspace`         | Delete a character                                |

Type a query prefixed with `#` to switch to **run-in-terminal** mode: the
prompt changes from `>` to `#` and the list shows executables found in your
`$PATH` instead of desktop apps. Type to filter them, or just `#` to list
everything; pick one (or type a command with arguments, e.g. `#htop -d 5`) and
press Enter to open a terminal and run it. The command runs in your `$SHELL`
(default `/bin/sh`), and the terminal is chosen automatically (`$TERMINAL`,
else st, alacritty, kitty, foot, urxvt, xterm, konsole, …).

**Mouse**

| Action                        | Result                                  |
|-------------------------------|-----------------------------------------|
| Move pointer over the window  | Highlights the entry under the cursor   |
| Scroll wheel                 | Move through the list                  |
| Left-click an entry           | Launch that application, then quit       |

> **Focus note:** `srun` is a plain xdg-shell window, so under a
> focus-follows-cursor compositor (like swm) it receives keyboard input
> only while the pointer is over it. Move the mouse onto the launcher
> (or click it) and typing works. If it ever prints
> `srun: failed to parse keyboard keymap`, keyboard input can't work and
> that message indicates a keymap problem rather than a focus one.

## How the application list is built

`srun` scans for `*.desktop` files in:

- `$XDG_DATA_HOME/applications` (default `~/.local/share/applications`)
- `/usr/share/applications`
- `/usr/local/share/applications`

Entries that aren't real launchable applications are skipped automatically:
anything with `NoDisplay=true` or `Hidden=true`, anything that isn't
`Type=Application`, anything without an `Exec=`, and bare URL scheme handlers
(`MimeType=…x-scheme-handler…` with no `Categories` — real browsers/apps that
also register a handler are kept). If no `.desktop` files are found, a small
built-in fallback list is used so the launcher is never empty.

### Curating the list (`srun.conf`)

To hide or show specific apps, create a config file at
`$XDG_CONFIG_HOME/srun/srun.conf` (default `~/.config/srun/srun.conf`). A
default, fully-commented file is written there on first run. The file is
organized into sections:

```ini
[filter]

# exclude = hides a matching app
exclude = nm-applet
exclude = gcr-prompter

# include = if any include lines exist, ONLY matching apps are shown
#include = firefox
#include = terminal
```

Patterns match case-insensitively against the app's **Name**, its
**`.desktop` filename**, or its **executable**. `exclude` wins over `include`.
This is useful for trimming applets, control centers, and other entries your
distro ships but you don't want in the menu.

Launching parses the desktop entry's `Exec=` line (quote-aware, with `%`
field codes stripped) and runs it directly; if the command isn't found it
falls back to `sh -c "Exec"`.

## Theme (shared with ssettings)

srun and ssettings share one global colour theme file,
`~/.config/swm/theme.conf`. The file is organized into sections:

```ini
[colors]

bg = #1a1a24
border = #88b6fc
title = #ebf0fa
hint = #9a9eb3
sep = #595e73
sel = #88b6fc38
label = #fbfdff
text = #d2d6e1
value = #8c92a6
caret = #d9e5ff
term = #ffa95c
```

With no file present, srun uses its built-in palette (shown above). Any key
you set there also changes ssettings, and vice versa — see the ssettings
README for the full key list (`bg`, `border`, `title`, `hint`, `sep`, `sel`,
`label`, `text`, `value`, `caret`, `term`, …).

## Integrating with swm

swm's default config already binds the launcher:

```ini
[bindings]

SUPER+d = exec srun
```

For that to work, `srun` must be on your `PATH` (e.g. after `sudo make
install` here, or run it from this directory). To use a different launcher,
edit `~/.config/swm/swm.conf`.

## Files

```
src/srun.h                  Shared types, globals, and prototypes
src/main.c                 Wayland/shell setup, shared client state, event loop
src/apps.c                 Desktop file parsing, application list, filtering
src/config.c               User curation config (srun.conf parser)
src/launch.c               Application launching and terminal detection
src/icon.c                 GTK icon theme resolution and PNG loading
src/theme.c                Global colour theme (shared with ssettings)
src/render.c               Cairo rendering of the launcher window
src/input.c                Keyboard and pointer handling, auto-repeat
Makefile                   Build (into build/) + protocol generation + install
build/                     Build output (binary, objects, and the generated
                          xdg-shell protocol) — gitignored
xdg-shell-client-protocol.h/.c   Generated at build time (gitignored/cleaned)
```
