# Development

[Home](Home.md) / Development

This page is for working on esp-base itself. [CONTRIBUTING.md](../../CONTRIBUTING.md) has the short version.

## Repository layout

```
src/                  the library (namespace espbase)
src/web/              generated page header, committed
webui/                index.html, app.js, style.css
tools/build_web.py    webui/ -> src/web/index_html_gz.h
tools/check_version.py  every version pin must agree
test/                 host unit tests (pio test -e native) and the Arduino shim they build against
examples/Blank/       project template: platformio.ini and a ten line sketch
examples/Minimal/     the app framework: text Frame, launcher, clock, dialog, service
docs/wiki/            this wiki
.github/workflows/    CI
platformio.ini        builds the examples against the library for every target
library.json          PlatformIO manifest; library.properties is the Arduino one
```

## Build

```sh
pio run                                   # tier 1: esp32, esp32s3, esp32c3, examples/Minimal
pio run -e esp32c6 -e esp32s2             # tier 2
pio run -e esp32s3 -t upload -t monitor
PLATFORMIO_SRC_DIR=examples/Blank pio run -e esp32-lean -e esp32c3-noweb   # module flags, from the Blank sketch
pio test -e native                        # host unit tests
```

`platformio.ini` links the library into the example with `symlink://.`, so edits in `src/` are picked up directly. PlatformIO converts `.ino` files only at the root of `src_dir`, which is why the module-flag builds switch the source directory with `PLATFORMIO_SRC_DIR` instead of a source filter.

To try the template as a consumer would, copy `examples/Blank` somewhere and change its `lib_deps` to `symlink:///path/to/esp-base`.

Before a release, check on each tier 1 board: the AP appears when no network is reachable, the captive portal opens, `wifi set` connects without a reboot, the device reconnects after a router power cycle, `time` shows the clock after the first connection, four console clients work at once, free heap is stable after an hour of logging, espota and browser OTA both succeed, and a wrong OTA password is rejected.

## Tests

`pio test -e native` builds each `test/test_*` folder on the host. The tests include the library source they exercise directly (`#include "../../src/Console.cpp"`), and `test/shim/` provides the `String`, `Print`, `Stream`, `millis()` and FreeRTOS stand-ins they need. Covered: the request checks (`WebGuard.h`), the command parser and registry (`Console`), the log ring buffer (`Log`), the persisted key helpers (`Keys`) and the app registry. The WiFi state machine, the web server and the app manager depend on the radio, the TCP stack and FreeRTOS queues and are checked on hardware with the list above.

A new pure function goes into a file without Arduino includes when it can (`Keys.cpp`, `WebGuard.h`), and gets a test.

## Web UI

1. Edit `webui/`. It is plain HTML, CSS and JS with no framework and no build dependencies.
2. Run `python tools/build_web.py` to regenerate `src/web/index_html_gz.h`, then commit it. Consumer builds never run scripts.
3. Flash or OTA the example to try it. The ETag changes with the content, so browsers pick up the new page.

`python tools/build_web.py --check` fails when the header is stale or the page exceeds 20 KB gzipped. CI runs it.

## Style

`.clang-format` (Google based, 150 columns, pointers left, short case labels on one line) and `.editorconfig`. Run `clang-format -i` on the files you touch; CI does not enforce it, reviewers do.

## CI

| Job | Does |
| --- | --- |
| checks | `build_web.py --check`, `check_version.py` and the host unit tests |
| build | `examples/Minimal` on all five targets, `examples/Blank` with `ESPBASE_NO_OTA ESPBASE_NO_APPS ESPBASE_LOG_MAX_LEVEL=2` and with `ESPBASE_NO_WEB ESPBASE_NO_APPS`; fails on any compiler warning from `src/` or `examples/` |
| release | on `X.Y.Z` tags, attaches the example binaries to the GitHub release |

Actions are pinned to commit SHAs, the default token is read-only, and PlatformIO and the platform are pinned to fixed versions.

## Extending the library

- A new module is a class with `begin()` and `loop()`. Make it a member of `EspBase`, start it in `EspBase::begin()`, call it from `EspBase::loop()` and expose it through an accessor. If a project could reasonably not want it, give it an `ESPBASE_NO_x` switch and a CI build.
- Work that arrives from other tasks goes through a queue or a flag; only the loop task changes state.
- A new persisted key goes into `Keys.h` and into `All[]` in `Keys.cpp`, so `config list|get|set` know it. Keys are 15 characters max, and a test in `test/test_keys` checks that.
- A chip difference becomes a function in `ChipInfo.h`. There is no `#if` on targets anywhere else.
- Commands are registered in `EspBase::registerBuiltins()` or in the module's `begin()`, on `Console`, never on the web side.
- Everything is in `namespace espbase`. Macros stay global and carry the `ESPBASE_` or `LOG_` prefix.

## Review checklist

- [ ] Nothing blocks; no `delay()` in loop paths.
- [ ] No `Serial` in `src/`, no GPIO numbers, no literal core ids.
- [ ] Chip checks only in `ChipInfo.h`, through `SOC_*` macros.
- [ ] Cross-task hand-offs use a queue, a mutex or a flag.
- [ ] Routes that change state live under `/api/`, so the origin check and the login cover them.
- [ ] No secrets in logs, command output or API responses.
- [ ] Builds with every module switch; warning free on every target; `build_web.py --check` and `pio test -e native` pass.
- [ ] README, wiki and CHANGELOG updated.

## Versioning and release

The project uses semantic versioning. The public API is the headers, the persisted NVS keys, the console commands, the HTTP routes and the WebSocket message shapes. Adding a config field with a default, a command, a route or a JSON field is a minor release. Renaming or removing any of those, changing a default that alters behaviour, or changing a persisted key is a major release. Fixes are patch releases. When a name must go, keep the old one for one minor release marked `[[deprecated]]`.

To release: bump the version in `library.json`, `library.properties`, `ESPBASE_VERSION` in `EspBaseConfig.h`, and the pinned tag in `README.md`, `docs/wiki/Getting-Started.md` and `examples/Blank/platformio.ini`; add the `CHANGELOG.md` entry; run `python tools/check_version.py`; push; then publish a GitHub release with the new tag `X.Y.Z` (no `v`) on that commit. The tag starts the release job, which attaches the example binaries; the notes written when publishing are kept. Consumers pin the tag in `lib_deps`.
