# LightShell

[![CI](https://github.com/lightshell-dev/lightshell/actions/workflows/ci.yml/badge.svg)](https://github.com/lightshell-dev/lightshell/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Build desktop apps with JavaScript. Ship them under 1MB.

---

## Why LightShell?

| | Electron | Tauri | LightShell |
|---|---------|-------|------------|
| **Binary size** | ~180MB | ~8MB | **~1MB** |
| **Startup time** | ~1.2s | ~0.5s | **<100ms** |
| **Memory usage** | ~120MB | ~45MB | **~15MB** |
| **Dependencies** | Node.js, Chromium | Rust, system webview | **None** |
| **You write** | JS + Node | JS + Rust | **JS only** |
| **GPU rendering** | Chromium (bundled) | System webview | **Metal / Vulkan** |

LightShell doesn't bundle a browser or use a system webview. It has its own JavaScript engine ([r8e](https://github.com/lightshell-dev/r8e)), its own layout engine (flexbox), and renders directly to the GPU via Metal (macOS) or Vulkan (Linux). The result: a single native binary under 1MB with zero runtime dependencies.

## Quick Start

```bash
lightshell init my-app
cd my-app
lightshell dev
```

## How It Works

```
Your JavaScript / HTML / CSS
         │
    ┌────▼─────┐
    │   r8e    │  JavaScript engine (166KB, C11)
    │  engine  │  DOM, flexbox layout, CSS styling
    └────┬─────┘
         │ Display List
    ┌────▼─────┐
    │   GPU    │  Metal (macOS) / Vulkan (Linux)
    │ renderer │  Rectangles, text, images, clipping
    └────┬─────┘
         │
    Native window (Cocoa / X11)
```

No browser. No webview. No Node.js. No Go. No Rust. Pure C from top to bottom.

## Native APIs

```js
// File system
var content = lightshell.fs.readFile('./data.json', 'utf-8')
lightshell.fs.writeFile('./output.txt', 'Hello from LightShell')

// Native dialogs
var file = lightshell.dialog.open({
  filters: [{ name: 'JSON', extensions: ['json'] }]
})

// Clipboard
lightshell.clipboard.write('Copied!')
var text = lightshell.clipboard.read()

// Open URLs in default browser
lightshell.shell.open('https://lightshell.dev')

// Window management
lightshell.window.setTitle('My App')
lightshell.window.setSize(1200, 800)

// System info
lightshell.system.platform   // 'darwin' or 'linux'
lightshell.system.homeDir    // '/Users/you'

// App menu
lightshell.menu.set([
  { label: 'File', submenu: [
    { label: 'Quit', accelerator: 'Cmd+Q', click: function() { lightshell.app.quit() } }
  ]}
])
```

## Project Structure

```
my-app/
  lightshell.json      # App config
  src/
    index.html         # Entry point
    app.js             # Your code
    style.css          # Your styles
  assets/              # Images, fonts
```

**lightshell.json:**

```json
{
  "name": "my-app",
  "version": "1.0.0",
  "entry": "src/index.html",
  "window": {
    "title": "My App",
    "width": 1024,
    "height": 768,
    "resizable": true
  },
  "permissions": ["fs", "dialog", "clipboard", "shell", "menu"]
}
```

## CLI Commands

```bash
lightshell init [name]     # Create a new project
lightshell dev             # Run with hot reload
lightshell build           # Build native binary
lightshell doctor          # Check system dependencies
```

## Platform Support

| Platform | GPU | Window | Status |
|----------|-----|--------|--------|
| macOS (arm64, x86_64) | Metal | Cocoa | Supported |
| Linux (x86_64, aarch64) | Vulkan | X11 | Supported |

## Architecture

LightShell is built on three layers, all in C:

**1. r8e Engine** — A 166KB JavaScript engine with built-in DOM, flexbox layout, CSS styling, and event dispatch. Executes your app's JavaScript and produces a display list of rendering commands.

**2. GPU Renderer** — Consumes the display list and renders via Metal (macOS) or Vulkan (Linux). Instanced quad rendering for rectangles, glyph atlas for text, texture cache for images. 60fps with VSync.

**3. Platform Layer** — Native window management, input handling, and OS APIs (file system, dialogs, clipboard, menus). Cocoa/Objective-C on macOS, X11/C on Linux.

### Zero Dependencies

LightShell has no external library dependencies:

- **JavaScript engine**: r8e (our own, C11)
- **Font rendering**: Hardened TrueType rasterizer (our own, bounds-checked)
- **Image loading**: stb_image (vendored, public domain)
- **Text shaping**: Simple left-to-right (built-in)
- **GPU**: Metal / Vulkan (system frameworks)
- **Window**: Cocoa / X11 (system frameworks)

Bundled fonts: Inter and Open Sans (embedded as C arrays).

### Security

- r8e's 5-layer security architecture protects against untrusted JavaScript
- Capability-based permissions: apps declare what they need in `lightshell.json`
- Font parser is hardened: every byte read is bounds-checked, malformed fonts return errors
- No JIT compiler: eliminates an entire class of security vulnerabilities

## Building from Source

```bash
git clone https://github.com/lightshell-dev/lightshell.git
cd lightshell
make                    # auto-fetches r8e engine, builds everything
make run-demo           # run the demo app
```

The build system automatically fetches the [r8e engine](https://github.com/lightshell-dev/r8e) at the pinned version. No manual setup required.

### Build the CLI

```bash
cd cli && make
./build/lightshell --help
```

## Contributing

Contributions welcome. The codebase is pure C11 and Objective-C (macOS) with no external dependencies.

```bash
make clean && make      # build
cd ../r8e && make test  # run engine tests
```

## License

[MIT](LICENSE)
