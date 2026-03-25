/**
 * LightShell Electron Compatibility Layer
 *
 * Clean-room implementation of Electron's public API surface,
 * mapping calls to LightShell's native APIs (lightshell.*).
 *
 * APIs are not copyrightable (Oracle v Google, US Supreme Court 2021).
 *
 * ES5 syntax throughout for r8e compatibility.
 */

// ---------------------------------------------------------------------------
// Internal state
// ---------------------------------------------------------------------------

var _readyCallbacks = [];
var _isReady = false;
var _windowAllClosedCallback = null;

var _ipcMainHandlers = {};
var _ipcMainListeners = {};
var _ipcRendererListeners = {};

var _warned = {};

function warnUnsupported(name) {
    if (!_warned[name]) {
        _warned[name] = true;
        console.log('[lightshell] Warning: electron.' + name + ' is not yet supported');
    }
}

// ---------------------------------------------------------------------------
// app
// ---------------------------------------------------------------------------

var app = {
    on: function(event, callback) {
        if (event === 'ready') {
            if (_isReady) { callback(); }
            else { _readyCallbacks.push(callback); }
        }
        if (event === 'window-all-closed') {
            _windowAllClosedCallback = callback;
        }
    },

    quit: function() {
        lightshell.app.quit();
    },

    getPath: function(name) {
        if (name === 'home') return lightshell.system.homeDir;
        if (name === 'temp') return lightshell.system.tempDir;
        if (name === 'userData') return lightshell.app.dataDir;
        return '';
    },

    getVersion: function() {
        return lightshell.app.version || '0.0.0';
    },

    getName: function() {
        return 'LightShell App';
    },

    isReady: function() {
        return _isReady;
    },

    whenReady: function() {
        if (_isReady) return Promise.resolve();
        return new Promise(function(resolve) {
            _readyCallbacks.push(resolve);
        });
    }
};

// ---------------------------------------------------------------------------
// BrowserWindow
// ---------------------------------------------------------------------------

function BrowserWindow(options) {
    options = options || {};
    this._width = options.width || 800;
    this._height = options.height || 600;
    this._title = options.title || '';
    this._listeners = {};

    if (options.width) {
        lightshell.window.setSize(options.width, options.height || 600);
    }
    if (options.title) {
        lightshell.window.setTitle(options.title);
    }
    if (options.x !== undefined && options.y !== undefined) {
        lightshell.window.setPosition(options.x, options.y);
    }

    var self = this;

    this.webContents = {
        send: function(channel) {
            var args = Array.prototype.slice.call(arguments, 1);
            if (_ipcRendererListeners[channel]) {
                for (var i = 0; i < _ipcRendererListeners[channel].length; i++) {
                    _ipcRendererListeners[channel][i].apply(null, [{}].concat(args));
                }
            }
        },
        executeJavaScript: function(code) {
            return eval(code);
        }
    };
}

BrowserWindow.prototype.loadFile = function(path) {
    lightshell.window.loadFile(path);
};

BrowserWindow.prototype.loadURL = function(url) {
    // v1: file:// protocol only
    if (url.indexOf('file://') === 0) {
        lightshell.window.loadFile(url.replace('file://', ''));
    }
};

BrowserWindow.prototype.show = function() {
    lightshell.window.show();
};

BrowserWindow.prototype.hide = function() {
    lightshell.window.hide();
};

BrowserWindow.prototype.close = function() {
    lightshell.window.close();
};

BrowserWindow.prototype.minimize = function() {
    lightshell.window.minimize();
};

BrowserWindow.prototype.maximize = function() {
    lightshell.window.maximize();
};

BrowserWindow.prototype.setTitle = function(title) {
    this._title = title;
    lightshell.window.setTitle(title);
};

BrowserWindow.prototype.getTitle = function() {
    return this._title;
};

BrowserWindow.prototype.setSize = function(width, height) {
    this._width = width;
    this._height = height;
    lightshell.window.setSize(width, height);
};

BrowserWindow.prototype.getSize = function() {
    return [this._width, this._height];
};

BrowserWindow.prototype.center = function() {
    // Center is handled at the native layer if available
    if (lightshell.window.center) {
        lightshell.window.center();
    }
};

BrowserWindow.prototype.on = function(event, cb) {
    if (!this._listeners[event]) this._listeners[event] = [];
    this._listeners[event].push(cb);
};

// ---------------------------------------------------------------------------
// dialog
// ---------------------------------------------------------------------------

var dialog = {
    showOpenDialog: function(win, options) {
        // win is optional; if options is undefined, first arg is options
        if (!options) { options = win; win = null; }
        return lightshell.dialog.open(options);
    },

    showSaveDialog: function(win, options) {
        if (!options) { options = win; win = null; }
        return lightshell.dialog.save(options);
    },

    showMessageBox: function(win, options) {
        if (!options) { options = win; win = null; }
        return lightshell.dialog.message(options.message, options);
    },

    showErrorBox: function(title, content) {
        lightshell.dialog.message(content, { type: 'error', title: title });
    }
};

// ---------------------------------------------------------------------------
// ipcMain
// ---------------------------------------------------------------------------

var ipcMain = {
    on: function(channel, listener) {
        if (!_ipcMainListeners[channel]) _ipcMainListeners[channel] = [];
        _ipcMainListeners[channel].push(listener);
    },

    handle: function(channel, handler) {
        _ipcMainHandlers[channel] = handler;
    },

    removeHandler: function(channel) {
        delete _ipcMainHandlers[channel];
    },

    removeAllListeners: function(channel) {
        if (channel) {
            delete _ipcMainListeners[channel];
        } else {
            _ipcMainListeners = {};
        }
    }
};

// ---------------------------------------------------------------------------
// ipcRenderer
// ---------------------------------------------------------------------------

var ipcRenderer = {
    send: function(channel) {
        var args = Array.prototype.slice.call(arguments, 1);
        if (_ipcMainListeners[channel]) {
            var event = {
                reply: function(replyChannel) {
                    var replyArgs = Array.prototype.slice.call(arguments, 1);
                    if (_ipcRendererListeners[replyChannel]) {
                        for (var j = 0; j < _ipcRendererListeners[replyChannel].length; j++) {
                            _ipcRendererListeners[replyChannel][j].apply(null, [{}].concat(replyArgs));
                        }
                    }
                }
            };
            for (var i = 0; i < _ipcMainListeners[channel].length; i++) {
                _ipcMainListeners[channel][i].apply(null, [event].concat(args));
            }
        }
    },

    invoke: function(channel) {
        var args = Array.prototype.slice.call(arguments, 1);
        if (_ipcMainHandlers[channel]) {
            return Promise.resolve(_ipcMainHandlers[channel].apply(null, [{}].concat(args)));
        }
        return Promise.reject('No handler for channel: ' + channel);
    },

    on: function(channel, listener) {
        if (!_ipcRendererListeners[channel]) _ipcRendererListeners[channel] = [];
        _ipcRendererListeners[channel].push(listener);
    },

    removeAllListeners: function(channel) {
        if (channel) {
            delete _ipcRendererListeners[channel];
        } else {
            _ipcRendererListeners = {};
        }
    }
};

// ---------------------------------------------------------------------------
// clipboard
// ---------------------------------------------------------------------------

var clipboard = {
    readText: function() {
        return lightshell.clipboard.read();
    },
    writeText: function(text) {
        return lightshell.clipboard.write(text);
    }
};

// ---------------------------------------------------------------------------
// shell
// ---------------------------------------------------------------------------

var shell = {
    openExternal: function(url) {
        return lightshell.shell.open(url);
    },
    openPath: function(path) {
        return lightshell.shell.open('file://' + path);
    }
};

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------

function Menu() {
    this._items = [];
}

Menu.buildFromTemplate = function(template) {
    lightshell.menu.set(template);
    return new Menu();
};

Menu.setApplicationMenu = function(menu) {
    // Menu is already set via buildFromTemplate
};

// ---------------------------------------------------------------------------
// Exports
// ---------------------------------------------------------------------------

var exports = {
    app: app,
    BrowserWindow: BrowserWindow,
    dialog: dialog,
    ipcMain: ipcMain,
    ipcRenderer: ipcRenderer,
    clipboard: clipboard,
    shell: shell,
    Menu: Menu
};

// ---------------------------------------------------------------------------
// Unsupported API stubs
// ---------------------------------------------------------------------------

exports.Tray = function() { warnUnsupported('Tray'); };
exports.Notification = function() { warnUnsupported('Notification'); };
exports.nativeTheme = { shouldUseDarkColors: false };
exports.screen = {
    getPrimaryDisplay: function() {
        warnUnsupported('screen');
        return {};
    }
};
exports.session = { defaultSession: {} };
exports.globalShortcut = {
    register: function() { warnUnsupported('globalShortcut'); },
    unregister: function() { warnUnsupported('globalShortcut'); },
    unregisterAll: function() { warnUnsupported('globalShortcut'); }
};
exports.powerMonitor = {
    on: function() { warnUnsupported('powerMonitor'); }
};
exports.systemPreferences = {
    on: function() { warnUnsupported('systemPreferences'); }
};

// ---------------------------------------------------------------------------
// Fire ready event after module loads
// ---------------------------------------------------------------------------

_isReady = true;
for (var i = 0; i < _readyCallbacks.length; i++) {
    _readyCallbacks[i]();
}
