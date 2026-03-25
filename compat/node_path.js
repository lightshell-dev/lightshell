// Node.js path module shim
// Pure JS implementation of path operations

var path = {};

path.sep = '/';  // Unix separator (macOS + Linux)

path.join = function() {
    var parts = [];
    for (var i = 0; i < arguments.length; i++) {
        if (arguments[i]) parts.push(arguments[i]);
    }
    return path.normalize(parts.join('/'));
};

path.resolve = function() {
    var resolved = '';
    for (var i = arguments.length - 1; i >= 0; i--) {
        var p = arguments[i];
        if (!p) continue;
        resolved = p + '/' + resolved;
        if (p[0] === '/') break;  // absolute path found
    }
    return path.normalize(resolved);
};

path.normalize = function(p) {
    var parts = p.split('/');
    var result = [];
    var isAbsolute = p[0] === '/';
    for (var i = 0; i < parts.length; i++) {
        if (parts[i] === '.' || parts[i] === '') continue;
        if (parts[i] === '..') { result.pop(); continue; }
        result.push(parts[i]);
    }
    var out = result.join('/');
    if (isAbsolute) out = '/' + out;
    return out || '.';
};

path.dirname = function(p) {
    var idx = p.lastIndexOf('/');
    if (idx <= 0) return idx === 0 ? '/' : '.';
    return p.substring(0, idx);
};

path.basename = function(p, ext) {
    var idx = p.lastIndexOf('/');
    var base = idx >= 0 ? p.substring(idx + 1) : p;
    if (ext && base.length > ext.length &&
        base.substring(base.length - ext.length) === ext) {
        return base.substring(0, base.length - ext.length);
    }
    return base;
};

path.extname = function(p) {
    var base = path.basename(p);
    var idx = base.lastIndexOf('.');
    if (idx <= 0) return '';
    return base.substring(idx);
};

path.isAbsolute = function(p) {
    return p.length > 0 && p[0] === '/';
};

path.parse = function(p) {
    return {
        root: path.isAbsolute(p) ? '/' : '',
        dir: path.dirname(p),
        base: path.basename(p),
        ext: path.extname(p),
        name: path.basename(p, path.extname(p))
    };
};

registerModule('path', path);
