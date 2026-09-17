# brightness-control — technical notes

How Brightness Control works underneath: how it reaches the monitor, why the
panel icon and the popup are built the way they are, what the package does,
and how to work on it. For installing and using it, see the
[README](../README.md).

The target is Linux Mint / Ubuntu with Cinnamon on X11. Everything is one C
file, `main.c`, against GTK 3.

## How it reaches the monitor

A laptop's brightness keys drive the panel's own backlight
(`/sys/class/backlight`). An external monitor has no such device: its backlight
belongs to the monitor. What it does have is **DDC/CI**, a small command
channel that runs over the I²C wires already inside the HDMI/DisplayPort
cable. Brightness is "VCP feature 0x10".

The app does not speak DDC/CI itself. It runs [`ddcutil`](https://www.ddcutil.com):

```
ddcutil detect --brief              once at start, to find the monitor's I²C bus
ddcutil getvcp 10 --brief --bus N   once at start, to set the slider
ddcutil setvcp 10 <v> --noverify --bus N
```

Passing `--bus` skips ddcutil's own detection on every call, and `--noverify`
skips the read-back. Together they bring a write down from over a second to
about 0.3 s, which is the monitor's own response time and cannot be improved.

### The worker thread

0.3 s per write is far too slow to do on every slider movement, and blocking
the UI thread on it would freeze the slider. So all `ddcutil` calls live in one
worker thread, and the UI never waits for it:

- the slider writes its value into `target` (under a mutex) and signals;
- the worker sends `target`, and when that returns looks again. If the value
  moved on in the meantime it sends the new one, otherwise it sleeps.

While you drag, everything between "the value when the last write started" and
"the value now" is skipped. The monitor always ends on the final position, and
there is never a queue of stale writes to work through.

The brightness is read once, at start. A change made with the monitor's own
buttons is not noticed; reading before every popup would delay it by the
`getvcp` round trip.

## The panel icon

The obvious GTK 3 choice, `GtkStatusIcon`, **does not show up on Mint**.
Cinnamon's panel carries the *XApp Status Applet* and not the legacy system
tray. Cinnamon still owns the tray selection, so an XEmbed icon is accepted,
embedded in a 1×1 unmapped window, and never drawn. Everything appears to work
and nothing is visible.

So the icon is an `XAppStatusIcon` from `libxapp`, which is what Mint's own
applets use. It talks to the applet over D-Bus (`org.x.StatusIcon.*`), and on a
desktop without an XApp applet it falls back to a `GtkStatusIcon` by itself.

`libxapp1` is installed on every Mint system, but `libxapp-dev` usually is
not. Needing it would mean a root step just to compile, for five functions. So
`main.c` declares those five prototypes itself and the Makefile links against
the runtime library by exact name (`-l:libxapp.so.1`). The functions used have
been stable since libxapp 1.6.

The icon's signals give what the popup needs: `button-release-event` carries
the icon's position on screen and which edge the panel is on.

Because the icon is a D-Bus object, it can be driven without a mouse, which is
how clicks are tested:

```bash
N=org.x.StatusIcon.brightness_control; P=/org/x/StatusIcon/Icon
busctl --user call $N $P org.x.StatusIcon ButtonPress   iiuui 2198 1400 1 0 3
busctl --user call $N $P org.x.StatusIcon ButtonRelease iiuui 2198 1400 1 0 3
busctl --user call $N $P org.x.StatusIcon Scroll iiu 1 0 0     # 0 = up, 1 = down
busctl --user get-property $N $P org.x.StatusIcon TooltipText
```

libxapp ignores a release that had no press before it, so send both.

## The popup

The popup must not take focus: opening a volume-style slider should not grey
out the window you were typing in. A normal toplevel always asks the window
manager for focus, so the popup is a `GTK_WINDOW_POPUP` (override-redirect),
which the window manager never sees. That is how menus work.

The price is that a focus-less window gets no focus-out event and no keys. So,
again like a menu, it takes a pointer-and-keyboard grab while it is open:

- a click outside the popup arrives at the popup, which closes itself. As with
  any menu, that click is consumed;
- `Esc`, the arrow keys, `+`/`-`, `Page Up`/`Down`, `Home` and `End` arrive
  through the grab and are handled in `on_key`, because GTK will not route
  keys to the slider in a window that has no focus;
- if the grab is refused, which happens for a moment when the popup is opened
  from a keyboard shortcut and the window manager still holds the keyboard, it
  retries every 50 ms for a second and then gives up and closes.

Placement uses the coordinates from the click: centred over the icon, on the
side of the panel that faces the screen, clamped into the monitor's work area.
Opened without a click (see below) it goes to the bottom-right corner.

## One instance

The app is a `GtkApplication` with the id `com.qxlabs.BrightnessControl`.
Running the command a second time does not start a second copy: GLib forwards
the activation to the running one, which toggles the popup. That is what makes
a keyboard shortcut work, and why two autostart entries would be harmless.

## The package

`brightness-control_<version>_amd64.deb` is self-contained: it carries the
compiled program, so the source tree is not needed to install or reinstall.
From a terminal:

```bash
sudo apt install ./build/brightness-control_1.2_amd64.deb
```

It installs:

| Path | What |
|---|---|
| `/usr/bin/brightness-control` | the program |
| `/usr/share/applications/brightness-control.desktop` | the menu entry |
| `/etc/xdg/autostart/brightness-control.desktop` | starts it at login, for every user |
| `/usr/share/doc/brightness-control/` | the README and these notes |

The autostart entry is a conffile, so switching it off system-wide survives
upgrades. One user can switch it off for themselves in *Startup Applications*.

Nothing is bundled. `Depends:` names `ddcutil`, GTK 3 and `libxapp1`, and both
ways people install a .deb on Mint resolve that from the distribution's
repositories: GDebi (the double-click installer) and `apt install ./file.deb`.
Only a bare `dpkg -i` does not, and `sudo apt -f install` finishes the job
after one. Access to the monitor needs nothing from this package either:
`ddcutil` ships a udev rule (`60-ddcutil.rules`) that gives the logged-in user
the I²C devices of graphics cards, and `i2c-dev` is built into Ubuntu and Mint
kernels, so there is no group to join and no module to load.

The maintainer scripts only deal with the running icon:

- **`postinst`** starts the icon straight away for everyone logged in to a
  desktop, so installing needs no log-out. It finds graphical sessions with
  `loginctl` and starts the program as a transient unit of *that user's*
  systemd (`runuser` + `systemd-run --user`), so the process is theirs and is
  not a child of dpkg. `DISPLAY` and `XAUTHORITY` come from the user manager's
  environment, which the desktop session fills in at login. Best effort: if
  any of it is missing, the icon simply appears at the next login.
- **`prerm`** stops the running icon, so an upgrade does not leave the old
  program running and a removal does not leave a deleted one. After an upgrade
  the new `postinst` brings it back.

The version lives in one place, `VERSION` in the `Makefile`. It is compiled
into the binary, and `dist/build-deb.sh` reads it back with `--version`, so the
package can never disagree with the program inside it.

## Working on it

```bash
make            # build/brightness-control
make run        # build and run from here
make deb        # build/brightness-control_<version>_amd64.deb
```

Only one copy runs at a time, so stop the installed one first or your build
will just toggle its popup (the name is cut short on purpose; process names
hold 15 characters):

```bash
pkill -x brightness-cont
```

To work on the UI without touching a real monitor, put a stand-in `ddcutil`
ahead of the real one in `PATH`:

```bash
mkdir -p /tmp/fakebin && cat > /tmp/fakebin/ddcutil <<'EOF'
#!/bin/sh
case "$1" in
detect) printf 'Display 1\n   I2C bus:          /dev/i2c-99\n' ;;
getvcp) echo "VCP 10 C 40 100" ;;
esac
EOF
chmod +x /tmp/fakebin/ddcutil
PATH=/tmp/fakebin:$PATH ./build/brightness-control
```

### Layout

| Path | What |
|---|---|
| `main.c` | the whole app: `ddcutil` worker, popup, panel icon |
| `Makefile` | the build, and the version |
| `dist/build-deb.sh` | assembles the package from `build/` |
| `dist/*.desktop` | the menu entry and the autostart entry |
| `dist/deb/` | the package's control file and maintainer scripts |
| `docs/` | these notes and the README's picture |
| `build/` | all output; not in git |

### Known limits

- X11 only. Override-redirect placement and input grabs are X11 mechanisms; on
  Wayland the popup cannot position itself.
- One monitor: the first DDC-capable display `ddcutil detect` lists.
- Brightness only. Contrast (VCP 0x12) and input switching (0x60) would be the
  same three lines of `ddcutil`, but there is no UI for them.
