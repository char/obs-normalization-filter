# obs_normalize_filter

Normalizer plugin for OBS studio based on the [tleydxdy fork of OBS](https://gitlab.com/tleydxdy/obs-studio/).

## Requirements

- `obs-studio` (`libobs`)
- `meson`
- `ninja`
- A C compiler

## Building (Linux)

1. Clone recursively
2. Use `meson` and `ninja` to build:

```
$ meson build
$ ninja -C build
```
