# Installing the toolchain

[← README](../README.en.md) · [Русский](toolchain.md)

The project builds with ESP-IDF 5.5.x for the `esp32s3` target. The easiest
way is VS Code with the ESP-IDF extension: it downloads the framework and the
compiler and creates the Python environment itself. A terminal-only install
without VS Code is described at the end.

If the toolchain is already in place, go to [Building and flashing](build.en.md).

## What you need

- Linux, macOS or Windows 10/11, 64-bit.
- About 5 GB of disk: ESP-IDF, the toolchain and the build directory.
- Internet access for the installation and the first build.
- A USB cable **that carries data** - a charge-only cable looks the same but
  gives no port.
- Python 3.9 or newer, **installed before the extension**: the extension does
  not bring an interpreter with it, and without one the installation breaks
  halfway with errors that never mention Python.

## 1. System packages

### Linux (Debian, Ubuntu)

```bash
sudo apt update
sudo apt install -y git wget flex bison gperf python3 python3-pip python3-venv \
                    cmake ninja-build ccache libffi-dev libssl-dev dfu-util \
                    libusb-1.0-0
```

`python3-venv` is required: both ESP-IDF and the littlefs component create
Python environments of their own.

To use the port without `sudo`, add yourself to the group and **log in again**:

```bash
sudo usermod -aG dialout $USER
```

On Arch and Fedora the group may be named differently - look at the port's
owner: `ls -l /dev/ttyUSB0`.

### macOS

```bash
xcode-select --install
brew install cmake ninja dfu-util python
```

### Windows

1. Install Python from [python.org](https://www.python.org/downloads/windows/)
   and tick **"Add python.exe to PATH"** in the installer.
2. Check in a console: `python --version`. If the Microsoft Store opens
   instead of a version, turn off the aliases in "Settings → Apps → Advanced
   app settings → App execution aliases" - both `python` lines.
3. Git and the compiler need not be installed - the ESP-IDF installer brings
   them. If the system shows no COM port for the board, install the driver for
   its USB-UART bridge (CP210x, CH34x or FTDI).

Everything works on Windows except the host tests: they need a POSIX shell
and gcc, that is Linux, macOS or WSL.

## 2. VS Code and the ESP-IDF extension

1. Install [VS Code](https://code.visualstudio.com/) and open the project
   folder in it. VS Code offers the recommended extensions - ESP-IDF and
   C/C++; accept.
2. Install ESP-IDF through the extension: `Ctrl+Shift+P` → **ESP-IDF: Open
   ESP-IDF Installation Manager**. The installer opens; choose version
   **v5.5.5**. Keep the default install path (`~/esp/` or
   `%USERPROFILE%\esp\`) - above all, **no spaces or non-Latin letters** in
   it, some of the tools trip on those.
3. Wait for the installation to finish (about 2 GB), then `Ctrl+Shift+P` →
   **ESP-IDF: Select Current ESP-IDF Version** and pick the installed one.
   The check is **ESP-IDF: Doctor Command**.

The message "File …/build/project_description.json cannot be found" on the
first open is not an error: the file appears after the first build.

## 3. The first build and flash

- `Ctrl+Shift+B` - build.
- **Terminal → Run Task** - the rest: `ESP-IDF: Flash`, `ESP-IDF: Monitor`,
  `ESP-IDF: Build, Flash & Monitor`, `Host tests`.
- **On a new board - `ESP-IDF: First flash (app + data)`**: writes both the
  firmware and the data partition with the web interface and the station
  list. A normal flash leaves the data alone, and a new board has none yet.

The `esp32s3` target is already set in the project; no `set-target` needed.
The tasks call [`tools/idf.sh`](../tools/idf.sh) (on Windows
[`tools/idf.ps1`](../tools/idf.ps1)): the script finds the installed ESP-IDF,
including one put there by the installer, and activates it. The port is found
automatically with one board; with several, name it in `ESPPORT`.

The script looks for 5.5.5 specifically, not "any 5.5", so that the VS Code
tasks and the terminal build with the same version. To build with another one
deliberately, set `JRADIO_IDF` to its path.

What comes next is in [Building and flashing](build.en.md).

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
idf.py -p /dev/ttyUSB0 flash monitor
```

`export.sh` acts on the current session only - that is how it is meant to be
used. Do not add it to `.bashrc`: it puts its own Python ahead of the system
one.

### Windows: "idf.py is not recognized as the name of a cmdlet"

This is not a breakage. `idf.py` works only together with its environment,
and the activation script puts that on `PATH` - in the current terminal window
only. The options, from simple to involved:

1. **Build with the VS Code tasks** (`Terminal → Run Task → ESP-IDF: Build`) -
   `tools\idf.ps1` finds and activates the toolchain itself.
2. **Call the same script by hand** from any terminal in the project folder:

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\idf.ps1 build
   ```

3. **Activate the toolchain in this session**, if you need `idf.py` itself:

   ```powershell
   . C:\Espressif\frameworks\esp-idf-v5.5.5\export.ps1
   idf.py build
   ```

   The leading dot and space are required. If PowerShell refuses to run the
   script, first `Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass`.
   An Installation Manager install activates with
   `Microsoft.PowerShell_profile.ps1` next to the `esp-idf` folder instead.
4. **Open the extension's terminal**: `Ctrl+Shift+P` → `ESP-IDF: Open ESP-IDF
   Terminal` - the toolchain is already active there.

The `IDF_PATH` and `IDF_TOOLS_PATH` variables need not be set permanently: they
say *where* the toolchain is but do not put it on `PATH`, and after an upgrade
they keep pointing at the old version.

## 5. The device's port

- **Linux:** `/dev/ttyUSB*` or `/dev/ttyACM*`, depending on the bridge on the
  board. The reliable way: `ls /dev/tty{ACM,USB}*` before plugging in and
  after.
- **macOS:** `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`.
- **Windows:** `COM*`; the number is in Device Manager.

It is the board's **UART port**, not its USB connector: that one belongs to the
stick. The port belongs to whoever opened it first - if a flash does not
start, close the monitor.

## 6. If something goes wrong

| Message | Cause | What to do |
|---|---|---|
| The installer fails halfway (Windows) | no system Python, or the Store stub on PATH | install Python from python.org with "Add to PATH", turn off the Store aliases, run again |
| `idf.py: command not found`, "idf.py is not recognized" | the toolchain is not activated in this terminal | `source …/export.sh`, on Windows `. …\export.ps1`, or the VS Code tasks |
| `Permission denied: '/dev/ttyUSB0'` | the user is not in the port's group | `usermod -aG dialout` and log in again |
| `Failed to connect to ESP32-S3` | a charge-only cable, a busy port, the board not in bootloader mode | change the cable; close the monitor; hold BOOT, tap RESET, release BOOT |
| `Could not find a version that satisfies the requirement littlefs-python` | the network blinked during the first build | run `idf.py build` again |
| Blank screen, the device runs | only the app was flashed | `ESP-IDF: First flash (app + data)` |
| `ESP-IDF Python virtual environment … not found` in a VS Code task | an Installation Manager install keeps its environment where `export.ps1` does not look | run the task from the "ESP-IDF Terminal" |

## 7. Not needed by everyone

- **Node.js** - for the browser JavaScript tests and for
  [`tools/gen_ui_fonts.sh`](../tools/gen_ui_fonts.sh) (font generation via
  `npx lv_font_conv`). Not needed for a build: the generated fonts are in the
  repository.
- **Python packages need not be installed by hand**: both ESP-IDF and littlefs
  work in environments of their own.
