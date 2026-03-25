// Node.js process global shim
var process = {
    platform: lightshell.system.platform || 'darwin',
    arch: lightshell.system.arch || 'arm64',
    env: {},
    argv: [],
    cwd: function() { return lightshell.system.homeDir || '/'; },
    exit: function(code) { lightshell.app.quit(); },
    version: 'v18.0.0',  // fake Node version for compatibility checks
    versions: { node: '18.0.0' }
};
