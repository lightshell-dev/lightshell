// Node.js fs module shim
// Maps to lightshell.fs.* native APIs

var fs = {};

fs.readFileSync = function(path, options) {
    var encoding = null;
    if (typeof options === 'string') encoding = options;
    else if (options && options.encoding) encoding = options.encoding;
    return lightshell.fs.readFile(path, encoding || 'utf-8');
};

fs.writeFileSync = function(path, data, options) {
    var encoding = 'utf-8';
    if (typeof options === 'string') encoding = options;
    else if (options && options.encoding) encoding = options.encoding;
    lightshell.fs.writeFile(path, data);
};

fs.existsSync = function(path) {
    return lightshell.fs.exists(path);
};

fs.mkdirSync = function(path, options) {
    lightshell.fs.mkdir(path);
};

fs.readdirSync = function(path) {
    return lightshell.fs.readDir(path);
};

fs.statSync = function(path) {
    var s = lightshell.fs.stat(path);
    return {
        size: s ? s.size : 0,
        mtime: s ? new Date(s.mtime * 1000) : new Date(),
        isDirectory: function() { return s ? s.isDir : false; },
        isFile: function() { return s ? !s.isDir : false; }
    };
};

fs.unlinkSync = function(path) {
    lightshell.fs.remove(path);
};

fs.rmdirSync = function(path) {
    lightshell.fs.remove(path);
};

// Async versions (call sync and wrap in callback)
fs.readFile = function(path, options, callback) {
    if (typeof options === 'function') { callback = options; options = {}; }
    try {
        var data = fs.readFileSync(path, options);
        if (callback) callback(null, data);
    } catch (e) {
        if (callback) callback(e);
    }
};

fs.writeFile = function(path, data, options, callback) {
    if (typeof options === 'function') { callback = options; options = {}; }
    try {
        fs.writeFileSync(path, data, options);
        if (callback) callback(null);
    } catch (e) {
        if (callback) callback(e);
    }
};

fs.exists = function(path, callback) {
    callback(fs.existsSync(path));
};

fs.mkdir = function(path, options, callback) {
    if (typeof options === 'function') { callback = options; }
    try {
        fs.mkdirSync(path);
        if (callback) callback(null);
    } catch (e) {
        if (callback) callback(e);
    }
};

// fs.promises (async wrappers)
fs.promises = {
    readFile: function(path, options) {
        return Promise.resolve(fs.readFileSync(path, options));
    },
    writeFile: function(path, data, options) {
        fs.writeFileSync(path, data, options);
        return Promise.resolve();
    },
    mkdir: function(path, options) {
        fs.mkdirSync(path, options);
        return Promise.resolve();
    },
    stat: function(path) {
        return Promise.resolve(fs.statSync(path));
    },
    unlink: function(path) {
        fs.unlinkSync(path);
        return Promise.resolve();
    }
};

registerModule('fs', fs);
