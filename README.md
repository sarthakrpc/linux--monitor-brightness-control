# Brightness Control

A brightness slider for your **external monitor**, in the panel on Linux.

![The brightness slider that opens when you click the sun icon](docs/popup.png)

The brightness keys on a laptop only change the laptop's own screen. A monitor
plugged in over HDMI or DisplayPort ignores them, and you end up pressing the
little buttons under the monitor. This app puts a slider in your panel instead.

Tested on **Linux Mint 22.3 (Cinnamon)** with an Acer SA322QU over HDMI. It has
not been tried on other systems.

## What you get

- **A sun icon in the panel.** Click it and drag the slider.
- **Scroll on the icon** to go up or down 5% at a time, without opening anything.
- **It stays out of your way.** The window you were working in stays active
  while the slider is open. Click anywhere else, or press `Esc`, to close it.
- **It starts by itself** whenever you log in.
- **No passwords.** You type your password once, to install. Never again.

## Install

1. Download **`brightness-control_1.2_amd64.deb`** from the [Releases page](https://github.com/sarthakrpc/linux--monitor-brightness-control/releases/latest).
2. Double-click it and press **Install Package**.
3. Type your password when asked.

That's it. The sun icon appears in the panel straight away, and from now on it
starts by itself whenever you log in. **Monitor Brightness** is also in your
applications menu.

There is nothing else to install first. The one tool it relies on, `ddcutil`,
comes from Mint's own software sources, and the installer fetches it for you
during step 2 (so you need to be online).

Don't have the installer file? See [Build the installer yourself](#build-the-installer-yourself).

## Using it

| Do this | And this happens |
|---|---|
| **Click** the sun icon | The slider opens just above it. Drag it, or use the arrow keys. |
| **Scroll** on the sun icon | Brightness goes up or down by 5%. |
| **Hover** over the sun icon | It tells you the current brightness. |
| **Click anywhere else**, or press `Esc` | The slider closes. |
| **Right-click** the sun icon | A menu with *Quit*. |

Choosing *Quit* only removes the icon. Your monitor keeps the brightness you
set, and the icon returns the next time you log in or open Monitor Brightness
from the menu.

### A keyboard shortcut, if you like

Open **System Settings → Keyboard → Shortcuts → Custom Shortcuts**, add one,
and give it this command:

```bash
brightness-control
```

Pressing your shortcut then opens the slider in the bottom-right corner, and
pressing it again closes it.

## If something isn't right

**The sun icon is missing.** Open Monitor Brightness from the menu once; that
brings it back. If it never appears at login, open Mint's *Startup
Applications* and make sure *Monitor Brightness (panel icon)* is switched on.
If it is still missing, right-click the panel, choose *Applets*, and check that
*XApp Status Applet* is there. Mint has it by default.

**The slider moves but the monitor doesn't change.** Your monitor has to allow
being controlled from the computer. This shows whether it does:

```bash
ddcutil getvcp 10
```

If that prints a brightness value, the app will work. If it says no monitor was
found, look in the monitor's own menu (the buttons on the monitor) for a
setting called **DDC/CI** and switch it on. If it complains about permissions,
restart the computer once: access to the monitor is set up by `ddcutil` when it
is installed, and takes effect at the next start. If it still complains, run
this, then log out and back in:

```bash
sudo usermod -aG i2c $USER
```

**The brightness follows the slider a moment late.** That's the monitor, not
the app: monitors take about a third of a second to answer. It always ends up
exactly where you let go.

**The slider shows the wrong number.** The app reads the brightness once, when
it starts. If you change it with the buttons on the monitor, the slider doesn't
know until you move it or log in again.

**I have two external monitors.** It controls the first one it finds.

## Uninstall

```bash
sudo apt remove brightness-control
```

The icon disappears and nothing else is left behind. Your monitor keeps
whatever brightness it had.

## Build the installer yourself

You need a C compiler and the GTK headers:

```bash
sudo apt install build-essential pkg-config libgtk-3-dev ddcutil
```

Then, in this folder:

```bash
make deb
```

It takes a second. The installer appears at
`build/brightness-control_1.2_amd64.deb`. Keep a copy of it somewhere safe: it
contains everything, so you can reinstall later without this folder.

To try it without installing, use `make run`.

## More detail

- [docs/TECHNICAL.md](docs/TECHNICAL.md) explains how it works inside: how it
  talks to the monitor, why the icon is built the way it is, what the package
  does when it installs, and how to work on the code.
