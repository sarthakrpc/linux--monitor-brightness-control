# Brightness Control

A tiny tray slider for **external monitor brightness** on Linux Mint / Cinnamon.
Laptop brightness keys don't reach an HDMI/DisplayPort monitor; this talks to it
over DDC/CI (via `ddcutil`) instead.

- Click the sun icon in the panel → a small slider pops up above it, drag to set brightness.
- Scroll on the icon → ±5% without opening anything.
- Right-click → Quit.
- Click anywhere else or press `Esc` to close. The popup never steals focus from your active window.
- Launching it again while it's running just toggles the slider, so you can bind the command to a keyboard shortcut.

Single C file, GTK 3, ~28 KB binary.

## Tested on

Linux Mint 22.3 (Cinnamon, X11) with GTK 3.24, `ddcutil` 1.4.1 and `libxapp1` 3.2,
driving an Acer SA322QU over HDMI. That is the only setup it has been tested on.
Other Ubuntu/Debian based desktops with GTK 3 should work but are untried, and the
popup positioning relies on X11 so Wayland sessions are not expected to work properly.

## Requirements

Runtime:

| Package | Why |
| --- | --- |
| `ddcutil` | sends the brightness commands to the monitor |
| `libgtk-3-0` | UI |
| `libxapp1` | panel icon (preinstalled on Mint) |

Build:

```bash
sudo apt install build-essential pkg-config libgtk-3-dev ddcutil
```

`libxapp-dev` is **not** needed — the Makefile links straight against `libxapp.so.1`.

### Check that your monitor supports DDC/CI first

```bash
ddcutil detect
```

```bash
ddcutil getvcp 10
```

The first should list your monitor as `Display 1`, the second should print its
current brightness. If either fails, the app can't work either — enable "DDC/CI"
in the monitor's on-screen menu, and if you get permission errors add yourself
to the `i2c` group and log out and back in:

```bash
sudo usermod -aG i2c $USER
```

## Build and run

```bash
make
```

```bash
./brightness-control
```

The sun icon appears in the panel's status area. No terminal output means it's working.

## Install

### Option A: .deb package (recommended)

Installs to `/usr/bin`, adds a "Monitor Brightness" menu entry, and autostarts on
login for every user. After this the source folder isn't needed any more.

```bash
make deb
```

```bash
sudo apt install ./brightness-control_1.1.0_amd64.deb
```

Remove it with:

```bash
sudo apt remove brightness-control
```

### Option B: per-user, no root

Installs to `~/.local/bin`, with the menu entry in `~/.local/share/applications`
and autostart in `~/.config/autostart`.

```bash
make install
```

```bash
make uninstall
```

Use one option or the other, not both — otherwise two autostart entries exist
(harmless, the second launch just toggles the slider open at login, but annoying).

### After installing or upgrading

The copy that's already running keeps running the old code. Restart it once
(later logins pick up the new version by themselves):

```bash
pkill -x brightness-cont; setsid brightness-control >/dev/null 2>&1 &
```

(`brightness-cont` is not a typo — process names are cut to 15 characters.)

## Keyboard shortcut

System Settings → Keyboard → Shortcuts → Custom Shortcuts → add one with the
command `brightness-control`. Since the app is single-instance, the shortcut
toggles the slider in the bottom-right corner.

## Releasing a new version

1. Bump `VERSION` in the `Makefile`.
2. `make deb`
3. `sudo apt install ./brightness-control_<version>_amd64.deb`

## Troubleshooting

**No icon in the panel.** Check that it's running: `pgrep -x brightness-cont`.
If it is, make sure the panel has the *XApp Status Applet* (right-click panel →
Applets). Mint's panel does not display old-style tray icons, which is why this
uses `libxapp`; on desktops without an XApp applet it falls back to a regular
tray icon automatically.

**Slider moves but the brightness doesn't change.** Run the `ddcutil` checks
above. Run `./brightness-control` from a terminal to see `ddcutil` errors.

**Slider feels laggy.** Each DDC/CI write takes ~0.3 s on the monitor's side.
While dragging, only the most recent position is sent, so it lands on the right
value as soon as you stop.

**Multiple monitors.** It controls the first DDC-capable display that
`ddcutil detect` reports.

## Files

| File | What |
| --- | --- |
| `main.c` | the whole app: tray icon, popup, `ddcutil` worker thread |
| `Makefile` | `make`, `make install` / `uninstall`, `make deb`, `make clean` |
| `brightness-control.desktop.in` | template for the menu + autostart entry |
| `debian-control.in` | template for the package metadata |
