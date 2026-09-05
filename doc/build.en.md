# Building, flashing and tests

[← README](../README.en.md) · [Русский](build.md)

## Building and flashing

ESP-IDF 5.5.x, target `esp32s3`. Built and verified on 5.5.5; the version it
was built with is recorded in [`dependencies.lock`](../dependencies.lock).
Setting the environment up from scratch has [a page of its own](toolchain.en.md).

The shortest way in is VS Code: open the project folder and it offers the
recommended extensions - ESP-IDF and C/C++. The ESP-IDF extension opens its own
setup wizard on first run, which downloads the framework and the toolchain;
pick 5.5.x. After that `Ctrl+Shift+B` builds, and the rest is under
Terminal - Run Task: flashing, the device log, the host tests. For a blank
board there is "ESP-IDF: First flash (app + data)", which writes the firmware
and the data partition both.

The tasks go through [`tools/idf.sh`](../tools/idf.sh), or
[`tools/idf.ps1`](../tools/idf.ps1) on Windows, which finds an installed ESP-IDF -
the extension's copy included - and activates it, so `export.sh` never has to
be sourced by hand. The port is detected when the machine has one board on it;
with several attached, name the right one in `ESPPORT`.

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
partition. Compare the live state first: `curl http://<ip>/api/playlist`,
`curl http://<ip>/api/settings` and `curl http://<ip>/api/yandex`. The settings
return to their defaults after the flash, and the account has to be linked
again.

## Tests

```bash
bash tests/run_host_tests.sh
```

72 suites, no ESP-IDF activation needed. They compile the real component
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
