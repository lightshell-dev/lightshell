# Contributing to LightShell

Thanks for your interest in contributing! LightShell is an open-source desktop app framework and we welcome contributions of all kinds.

## Getting Started

### Prerequisites

- **C11 compiler** — Clang (macOS) or GCC (Linux)
- **macOS**: Xcode Command Line Tools (`xcode-select --install`)
- **Linux**: Vulkan SDK, X11 development headers

### Setup

```bash
git clone https://github.com/lightshell-dev/lightshell.git
cd lightshell
make                    # auto-fetches r8e engine, builds everything
make run-demo           # verify it works
```

### Build the CLI

```bash
cd cli && make
../build/lightshell init test-app
cd test-app
../../build/lightshell dev
```

### Running r8e engine tests

```bash
cd engine/r8e && make test    # 1500+ tests
```

## Project Structure

```
src/                    Platform layer (C / Objective-C)
  main.m                macOS entry point + event loop
  platform_darwin.m     macOS window, input, Metal surface
  platform_linux.c      Linux window, input, Vulkan surface
  gpu_metal.m           Metal GPU renderer
  gpu_vulkan.c          Vulkan GPU renderer
  text.c                Text shaping (uses r8e_font)
  glyph_atlas.c         Glyph atlas texture manager
  image.c               Image loading (stb_image)
  api_fs.c              File system API
  api_dialog_darwin.m   Native dialogs (macOS)
  api_clipboard_darwin.m  Clipboard (macOS)
  api_shell_darwin.m    Shell / URL opening
  api_sysinfo.c         System info
  api_menu_darwin.m     App menu (macOS)
cli/                    CLI tool (init, dev, build, doctor)
engine/                 Auto-fetched r8e JS engine (gitignored)
website/                Landing page (lightshell.dev)
docs/                   Documentation
examples/               Example apps
```

## What to Contribute

### Good first issues

- Add new native API modules (e.g., notifications, tray)
- Improve Metal/Vulkan rendering (shadows, gradients)
- Fix platform-specific issues
- Add examples and documentation

### Bigger contributions

- Linux Wayland native support
- Windows platform layer
- CSS property additions
- Performance optimizations

## Development Guidelines

### Code style

- **C**: C11, `-Wall -Wextra -Wpedantic`. No external dependencies.
- **Objective-C**: ARC enabled (`-fobjc-arc`). Follow Apple conventions.
- **JS**: Plain vanilla JS. No frameworks, no build tools.

### Commit messages

Short and descriptive:

```
fix: Metal layer initialization on HiDPI displays
feat(api): add notification support for macOS
docs: update getting started guide
```

### Pull requests

1. Fork the repo and create a branch from `main`
2. Make your changes
3. Run `make clean && make` to verify it builds
4. Run `cd engine/r8e && make test` to verify engine tests pass
5. Open a PR with a clear description

### Architecture decisions

- **Zero external dependencies** — everything vendored or built-in
- **Own JS engine** — r8e, not a browser or webview
- **Own GPU rendering** — Metal (macOS), Vulkan (Linux)
- **Own font rasterizer** — hardened TrueType, no FreeType/HarfBuzz
- **Pure C** — no C++, no Rust, no Go at runtime

## Reporting Bugs

Open an issue at [github.com/lightshell-dev/lightshell/issues](https://github.com/lightshell-dev/lightshell/issues) with:

- What you expected
- What actually happened
- Your OS and version
- Steps to reproduce

## License

By contributing, you agree that your contributions will be licensed under the [MIT License](LICENSE).
