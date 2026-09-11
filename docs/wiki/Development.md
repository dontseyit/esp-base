# Development

[Home](Home.md) / Development

This page is for working on esp-base itself.

## Repository layout

```
src/                  the library
src/web/              generated page header, committed
webui/                index.html, app.js, style.css
tools/build_web.py    webui/ -> src/web/index_html_gz.h
test/                 host unit tests (pio test -e native)
examples/minimal/     example sketch, text Frame, example apps
docs/wiki/            this wiki
.github/workflows/    CI
platformio.ini        builds the example against the library for every target
```

## Build

```sh
pio run                          # tier 1: esp32, esp32s3, esp32c3
pio run -e esp32c6 -e esp32s2    # tier 2
pio run -e esp32s3 -t upload -t monitor
pio test -e native               # host unit tests for the request checks in WebGuard.h
```

`platformio.ini` links the library into the example with `symlink://.`, so edits in `src/` are picked up directly.

Before a release, check on each tier 1 board: the AP appears when no network is reachable, the captive portal opens, `wifi set` connects without a reboot, the device reconnects after a router power cycle, four console clients work at once, free heap is stable after an hour of logging, espota and browser OTA both succeed, and a wrong OTA password is rejected.

## Web UI

1. Edit `webui/`. It is plain HTML, CSS and JS with no framework and no build dependencies.
2. Run `python tools/build_web.py` to regenerate `src/web/index_html_gz.h`, then commit it. Consumer builds never run scripts.
3. Flash or OTA the example to try it. The ETag changes with the content, so browsers pick up the new page.

`python tools/build_web.py --check` fails when the header is stale or the page exceeds 20 KB gzipped. CI runs it.

## CI

| Job | Does |
| --- | --- |
| checks | `build_web.py --check` and the host unit tests |
| build | all five targets; fails on any compiler warning from `src/` or `examples/` |
| release | on `v*` tags, a GitHub release with `esp-base-minimal-<env>.bin` for each target |

Actions are pinned to commit SHAs, the default token is read-only, and PlatformIO and the platform are pinned to fixed versions.

## Extending the library

- A new module is a class with `begin()` and `loop()`. Make it a member of `EspBase`, start it in `EspBase::begin()`, call it from `EspBase::loop()` and expose it through an accessor.
- Work that arrives from other tasks goes through a queue or a flag; only the loop task changes state.
- A new persisted key goes into `EspBaseKeys` and into `All[]` in `ConfigStore.cpp`, so `config list|get|set` know it. Keys are 15 characters max.
- A chip difference becomes a function in `ChipInfo.h`. There is no `#if` on targets anywhere else.
- Commands are registered in `EspBase::registerBuiltins()` or in the module's `begin()`.

## Review checklist

- [ ] Nothing blocks; no `delay()` in loop paths.
- [ ] No `Serial` in `src/`, no GPIO numbers, no literal core ids.
- [ ] Chip checks only in `ChipInfo.h`, through `SOC_*` macros.
- [ ] Cross-task hand-offs use a queue, a mutex or a flag.
- [ ] Routes that change state live under `/api/`, so the origin check and the login cover them.
- [ ] No secrets in logs, command output or API responses.
- [ ] Warning free on every target; `build_web.py --check` passes.
- [ ] README, wiki and CHANGELOG updated.

## Versioning and release

The project uses semantic versioning. Adding a config field with a default, a command, a route or a JSON field is a minor release. Renaming or removing a field, changing a default that alters behaviour, changing a persisted key or the WebSocket message shapes is a major release. Fixes are patch releases.

To release: bump `version` in `library.json` and `ESPBASE_VERSION` in `EspBaseConfig.h`, update `CHANGELOG.md`, then tag `vX.Y.Z`. Consumers pin the tag in `lib_deps`.
