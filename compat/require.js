// CommonJS require() implementation for r8e
// Module cache and built-in module registry

var _modules = {};      // module cache: id → { exports }
var _builtins = {};     // built-in module map: name → exports object

function require(id) {
    // Check cache first
    if (_modules[id]) return _modules[id].exports;

    // Check built-in modules
    if (_builtins[id]) {
        _modules[id] = { exports: _builtins[id] };
        return _builtins[id];
    }

    // Check for 'electron' compat
    if (id === 'electron' || id === 'lightshell/electron') {
        // electron.js registers itself in _builtins during load
        if (_builtins['electron']) {
            _modules[id] = { exports: _builtins['electron'] };
            return _builtins['electron'];
        }
    }

    throw new Error('Cannot find module: ' + id);
}

// Register a built-in module
function registerModule(name, exports) {
    _builtins[name] = exports;
}
