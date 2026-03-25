// Node.js os module shim
// Maps to lightshell.system.*

var os = {};

os.platform = function() { return lightshell.system.platform; };
os.arch = function() { return lightshell.system.arch; };
os.homedir = function() { return lightshell.system.homeDir; };
os.tmpdir = function() { return lightshell.system.tempDir; };
os.hostname = function() { return lightshell.system.hostname; };
os.type = function() {
    var p = lightshell.system.platform;
    if (p === 'darwin') return 'Darwin';
    if (p === 'linux') return 'Linux';
    return 'Unknown';
};
os.release = function() { return '0.0.0'; };
os.EOL = '\n';
os.cpus = function() { return []; };
os.totalmem = function() { return 0; };
os.freemem = function() { return 0; };

registerModule('os', os);
