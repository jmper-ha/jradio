# Setting up the development environment

[← README](../README.en.md) · [Русский](toolchain.md)

The project builds with ESP-IDF 5.5.x for the `esp32s3` target. The easiest way
in is the ESP-IDF extension for VS Code: it downloads the framework and the
compiler for you. A manual install is described at the end, and it is only for
people who build from a terminal without VS Code.

**Install Python yourself, before the extension.** The extension does not bring
an interpreter with it - it builds an environment out of one that is already
there. When there is none the install does not refuse outright but fails
part-way through with errors that do not mention Python: this was hit on
Windows, and installing Python by hand was what fixed it. On Linux and macOS an
interpreter is usually there already, but it is still named first in the
package list below, for the same reason.

## What you need

- Linux, macOS or Windows 10/11, 64-bit.
- About 5 GB of disk: ESP-IDF, the toolchain and the build directory.
- Internet for the install and for the first build - not afterwards.
- **A USB cable with data wires in it.** A charge-only cable looks exactly the
  same and the board powers up from it, but no port appears in the system. This
  is the most common cause of "it will not flash".

## 1. System packages

### Linux (Debian, Ubuntu)

```bash
sudo apt update
sudo apt install -y git wget flex bison gperf python3 python3-pip python3-venv \
                    cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
                    libusb-1.0-0
```

Python 3.9 or newer. `python3-venv` is not optional, and not only for ESP-IDF
itself: the littlefs component creates a second environment of its own to build
the data partition image, so without `venv` the failure comes not during the
install but in the middle of building the firmware.

Then serial port access without `sudo`:

```bash
sudo usermod -aG dialout $USER
```

and **log out and back in**, or the group will not apply. On Arch and Fedora the
group may be named differently: check who owns the port with
`ls -l /dev/ttyACM0`.

### macOS

```bash
xcode-select --install
brew install cmake ninja dfu-util python
```

### Windows

Install **Python from [python.org](https://www.python.org/downloads/windows/)**
and tick "Add python.exe to PATH" while doing it. This is the step without which
the ESP-IDF install goes wrong.

Check separately that `python` in a console is that one and not the Microsoft
Store stub: run `python --version`. If the Store opens, or no version is
printed, turn the aliases off under Settings → Apps → Advanced app settings →
App execution aliases - both `python` entries.

Git and the compiler do not need installing, the ESP-IDF installer brings them.
You may need the driver for your board's USB-UART bridge - CP210x, CH34x or
FTDI - if no COM port shows up.

One limitation: every VS Code task works on Windows except the host tests. Those
want a POSIX shell and gcc with sanitizers, which means Linux, macOS or WSL.

## 2. VS Code and the ESP-IDF extension

1. Install [VS Code](https://code.visualstudio.com/).
2. Open the project folder in it. VS Code will offer the recommended
   extensions - `espressif.esp-idf-extension` and `ms-vscode.cpptools`; accept.
   The list lives in [`.vscode/extensions.json`](../.vscode/extensions.json), so
   there is nothing to search the marketplace for.
3. The extension opens its setup wizard on first run. If it does not,
   `Ctrl+Shift+P` → **ESP-IDF: Configure ESP-IDF Extension** → **Express**.
   - **Version:** `v5.5.5`. That is what the project is built and verified on;
     any 5.5.x will do, but start with the same one.
   - **Install path:** `~/esp/` by default on Linux and macOS,
     `%USERPROFILE%\esp\` on Windows. **Do not pick a path with spaces or
     non-Latin characters in it** - parts of the toolchain trip over them, and
     the error messages will name anything except the path.
   - The wizard downloads ESP-IDF and the toolchain (about 2 GB) and creates a
     Python environment for them.
4. Wait for the install to report that it finished. Nothing will build before
   that.

To check it took: the **ESP-IDF: Show Examples** command should open a list of
examples.

## 3. First build and flash

- `Ctrl+Shift+B` builds.
- The rest is under **Terminal → Run Task**: `ESP-IDF: Flash`,
  `ESP-IDF: Monitor`, `Host tests`.
- **On a fresh board use `ESP-IDF: First flash (app + data)`.** An ordinary
  flash writes the application only and leaves the data partition alone, and a
  new board has none yet: with no web assets and no station list the device
  comes up and shows nothing.

The `esp32s3` target is already set in the project; there is no need to call
`set-target`.

The tasks call [`tools/idf.sh`](../tools/idf.sh), or [`tools/idf.ps1`](../tools/idf.ps1)
on Windows. The script finds the installed ESP-IDF - including one installed by
the extension - and activates it, so `export.sh` never has to be run by hand.
The port is detected automatically when only one board is attached; with several,
name the one you want in `ESPPORT`.

**The first build reaches the internet** even after the environment is
installed: the component manager downloads LVGL, the codecs and the panel
drivers, and the littlefs component installs `littlefs-python` from PyPI. What
can go wrong there and how to read the messages is in
[Building and flashing](build.en.md).

## 4. Without VS Code, from a terminal

```bash
mkdir -p ~/esp && cd ~/esp
git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git v5.5.5/esp-idf
cd v5.5.5/esp-idf && ./install.sh esp32s3
```

Then in every new terminal session:

```bash
source ~/esp/v5.5.5/esp-idf/export.sh
cd path/to/jradio
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

`export.sh` changes the current session only, and that is how it is meant to be
used. Do not put it in `.bashrc`: it puts its own Python ahead of the system
one, and that breaks something unrelated sooner or later.

## 5. The device's port

- **Linux:** `/dev/ttyACM*` or `/dev/ttyUSB*`, depending on the bridge on the
  board. The reliable way to tell: `ls /dev/tty{ACM,USB}*` before plugging it in
  and after.
- **macOS:** `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`.
- **Windows:** `COM*`, the number is in Device Manager.

Whoever opens the port first owns it. If a flash never starts and the log stays
empty, a monitor is almost certainly open in another window.

## 6. When it goes wrong

| Message | What it is | What to do |
|---|---|---|
| The extension's wizard fails part-way with errors (seen on Windows) | no system Python, or the Microsoft Store stub is on PATH | install Python from python.org, tick "Add to PATH", turn the Store aliases off, and run the wizard again |
| `idf.py: command not found` | environment not activated | `source .../export.sh` in this same session, or build with the VS Code tasks |
| `Permission denied: '/dev/ttyACM0'` | user is not in the port's group | `usermod -aG dialout`, then log out and back in |
| `Failed to connect to ESP32-S3` | charge-only cable, port busy, or the board is not in download mode | change the cable; close the monitor; hold BOOT, tap RESET, release BOOT |
| `Could not find a version that satisfies the requirement littlefs-python` | the network blinked while the data image was being built | run `idf.py build` again; only what is missing is fetched |
| Blank screen, device otherwise alive | only the application was written | `ESP-IDF: First flash (app + data)` |

## 7. What not everyone needs

**Node.js** is needed for the three browser-JavaScript tests (`tests/*.js`) and
for [`tools/gen_ui_fonts.sh`](../tools/gen_ui_fonts.sh), which calls
`npx lv_font_conv`. An ordinary build does not need it: the generated fonts are
in the repository.

**No Python packages have to be installed by hand** - packages, that is, not
the interpreter itself, which is needed and goes in first. Both ESP-IDF and the
littlefs component work inside their own environments, so installing anything
into the system Python has no effect on the build.
