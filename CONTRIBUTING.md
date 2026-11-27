# Contributing

Issues and pull requests are welcome. Small, focused changes land fastest.

## Build and test

```sh
pio run                                   # examples/Minimal for the tier 1 targets
PLATFORMIO_SRC_DIR=examples/Blank pio run -e esp32-lean -e esp32c3-noweb   # module flags
pio test -e native                        # host unit tests
python tools/build_web.py --check         # generated web header matches webui/
python tools/check_version.py             # every version pin agrees
```

CI runs the same set and fails on any compiler warning from `src/` or `examples/`.

## Style

`.clang-format` and `.editorconfig` hold the rules; run `clang-format -i` on the files you touch. Two-space indent, 150 columns, braces on every branch, `_member` names.

## Rules the code follows

See the review checklist in [docs/wiki/Development.md](docs/wiki/Development.md#review-checklist): nothing blocks, only the loop task changes state, no `Serial` or GPIO numbers in `src/`, chip checks only in `ChipInfo.h`, routes that change state under `/api/`, no secrets in logs.

## Versioning

Semantic versioning. The public API is the headers, the persisted NVS keys, the console commands, the HTTP routes and the WebSocket messages. A change to any of them that breaks an existing project is a major release. Keep `CHANGELOG.md` current in the same pull request.
