# Building, flashing and tests

[← README](../README.en.md) · [Русский](build.md)

## Building and flashing

ESP-IDF 5.5.x, target `esp32s3`. Built and verified on 5.5.5; the version it
was built with is recorded in [`dependencies.lock`](../dependencies.lock).
Setting the environment up from scratch has [a page of its own](toolchain.en.md).

### Versions

The firmware version is not typed in anywhere: ESP-IDF runs `git describe
--always --tags --dirty` and puts the result in the app header, which is where
both the boot log and the About screen read it from. It refreshes on a commit by
itself - CMake watches the branch's ref file and re-reads the version when it
moves.

**Use tags.** Without them the version is a short hash, and no hash tells you
which build is newer. With `git tag v1.0.0` it becomes `v1.0.0-3-gabc1234`: the
release, how many commits followed it, and the hash.

```bash
git tag -a v1.1.0 -m "what is in this release"
git push origin v1.1.0        # a plain git push does not carry tags
```

**A new tag is not picked up on its own.** The version is read when CMake
configures, and what it watches is the branch's ref file - a commit moves that,
a tag does not. So the first build after `git tag` still reports the old string.
Any reconfigure fixes it, `touch CMakeLists.txt` being the shortest; in ordinary
use it does not arise, because a tag is put on a commit that has just been
made.

The `-dirty` suffix can go stale. It is computed at configure time, and editing
a file does not move the branch ref, so a build from a dirty tree may report a
version without it. For a build made from a commit this does not arise.

**The web interface has a version of its own**, because it and the firmware are
written by different commands. [`tools/stamp_version.py`](../tools/stamp_version.py)
writes it to `config/version.json` inside the data image on every build, from the
same `PROJECT_VER`. The device shows both side by side and says so when they
differ. The generated file lives only in the build directory, never in `data/`.

To build with another version without touching git:

```bash
idf.py -DPROJECT_VER=v1.2.3-test build
```

That value sticks in the CMake cache - to go back to git, delete the
`PROJECT_VER` line from `build/CMakeCache.txt` (an empty `-DPROJECT_VER=` gives
you version `1`, which is not what you wanted).

The shortest way in is VS Code: open the project folder and it offers the
recommended extensions - ESP-IDF and C/C++. Since 2.0 the extension installs
nothing itself and no longer has a "Configure ESP-IDF Extension" command:
`Ctrl+Shift+P` -> **ESP-IDF: Open ESP-IDF Installation Manager** downloads and
opens the EIM installer; pick 5.5.5 there and it installs the framework, the
toolchain and the Python environment. Then `Ctrl+Shift+P` -> **ESP-IDF: Select
Current ESP-IDF Version** and choose the one just installed; **ESP-IDF: Doctor
Command** checks the result. An ESP-IDF installed by hand earlier does not
appear in that list - reinstalling it through EIM is the easy way. After that
`Ctrl+Shift+B` builds, and the rest is under
Terminal - Run Task: flashing, the device log, the host tests. For a blank
board there is "ESP-IDF: First flash (app + data)", which writes the firmware
and the data partition both.

The tasks go through [`tools/idf.sh`](../tools/idf.sh), or
[`tools/idf.ps1`](../tools/idf.ps1) on Windows, which finds an installed ESP-IDF -
the extension's copy included - and activates it, so `export.sh` never has to
be sourced by hand. The port is detected when the machine has one board on it;
with several attached, name the right one in `ESPPORT`.

What it looks for is **5.5.5 exactly**, not "something from 5.5". On a machine
that has ever upgraded two versions sit side by side, and taking the first one
found meant the VS Code tasks built on one while a terminal that had sourced
`export.sh` built on the other - and a build on the wrong one rewrites
`dependencies.lock`. With 5.5.5 absent any 5.5.x is used and the script says so.

`IDF_PATH` does **not** override that, and the omission is deliberate: inside
VS Code that variable is not set by a person but by the ESP-IDF extension,
which exports whatever `idf.currentSetup` names into the task's environment.
To build with another version on purpose, point `JRADIO_IDF` at it - nothing
else sets that one. When `IDF_PATH` names something other than what the script
picked, it says so on the first line of the build.

Every task works on Windows except "Host tests": those want a POSIX shell and a
gcc with sanitizers, so Linux, macOS or WSL.

**The first build reaches the internet,** and not only for the framework. The
component manager fetches what `idf_component.yml` names - LVGL, the codecs,
the panel drivers - and the littlefs component builds a virtualenv of its own
and installs `littlefs-python` from PyPI into it, which is what turns `data/`
into a partition image. After that everything lives in `managed_components/`
and `build/` and is not fetched again.

A network hiccup during that step ends the build like this:

```
ERROR: Could not find a version that satisfies the requirement littlefs-python==0.15.0
ninja: build stopped: subcommand failed.
```

The message misleads - you are building firmware and not thinking about a data
image - but running `idf.py build` again is the whole fix: what compiled is
kept and only the missing piece is fetched. Where PyPI is permanently out of
reach (a corporate network, a proxy), pip's ordinary settings apply to this
virtualenv too - `PIP_INDEX_URL` and `PIP_PROXY`.

So does pip's cache, if the package has ever been installed: it lives in
`~/.cache/pip` and survives the `fullclean` that removes the virtualenv itself.
The build then needs no network at all - `PIP_NO_INDEX=1` with `PIP_FIND_LINKS`
pointing at a directory holding the wheel. Used on 2026-09-06, when `pypi.org`
answered over neither IPv4 nor IPv6 while `files.pythonhosted.org` was fine.

One more network step exists but only when regenerating fonts:
[`tools/gen_ui_fonts.sh`](../tools/gen_ui_fonts.sh) calls `npx lv_font_conv` from
npm. An ordinary build needs none of it - the generated faces are in the
repository.

The same from a terminal:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash              # application only, leaves LittleFS alone
idf.py -p PORT littlefs-flash     # overwrites the whole data partition
idf.py -p PORT monitor
```

`littlefs-flash` **destroys user data** - playlist edits, saved networks,
device settings and the Yandex Music account link: all of it lives on that one
partition. Take a copy first - `curl -O -J http://<ip>/api/backup` - and put it
back afterwards:
`curl -X POST --data-binary @jradio-*.zip "http://<ip>/api/restore?name=backup.zip"`,
which is what the two buttons on the settings page do. The playlist is not in
that copy, so compare it separately: `curl http://<ip>/api/playlist`. Without a
copy the settings return to their defaults after the flash, and the account has
to be linked again.

## Tests

```bash
bash tests/run_host_tests.sh
```

89 suites, no ESP-IDF activation needed. They compile the real component
sources rather than mocks, with `-Werror` and the address and undefined
behaviour sanitizers. That is why format parsing, state machines and view
derivation live in files with no ESP-IDF dependencies - new logic belongs
there. Browser JavaScript runs under Node against a hand-written fake DOM, with
no npm and no bundler.

## Data on the device

The `littlefs` partition holds both the web assets and user data: `data/www/`
(gzipped at build time), `data/config/stations.csv`,
`data/config/settings.csv`, `data/radio_img/` with the station pictures, and
`wifi.json` and `yandex.json`, which the device creates itself and which are not
in Git. Everything under `data/` goes into the image, so station pictures survive
`littlefs-flash` - one uploaded through the browser and never put in the
repository will not. Do not commit passwords, tokens or
keys.
