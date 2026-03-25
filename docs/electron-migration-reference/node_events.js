// Node.js events module shim

function EventEmitter() {
    this._events = {};
}

EventEmitter.prototype.on = function(event, listener) {
    if (!this._events[event]) this._events[event] = [];
    this._events[event].push(listener);
    return this;
};

EventEmitter.prototype.once = function(event, listener) {
    var self = this;
    function wrapper() {
        self.removeListener(event, wrapper);
        listener.apply(this, arguments);
    }
    return this.on(event, wrapper);
};

EventEmitter.prototype.emit = function(event) {
    var listeners = this._events[event];
    if (!listeners) return false;
    var args = Array.prototype.slice.call(arguments, 1);
    for (var i = 0; i < listeners.length; i++) {
        listeners[i].apply(this, args);
    }
    return true;
};

EventEmitter.prototype.removeListener = function(event, listener) {
    var listeners = this._events[event];
    if (!listeners) return this;
    for (var i = 0; i < listeners.length; i++) {
        if (listeners[i] === listener) {
            listeners.splice(i, 1);
            break;
        }
    }
    return this;
};

EventEmitter.prototype.removeAllListeners = function(event) {
    if (event) { this._events[event] = []; }
    else { this._events = {}; }
    return this;
};

EventEmitter.prototype.listenerCount = function(event) {
    return (this._events[event] || []).length;
};

// Also used as 'off'
EventEmitter.prototype.off = EventEmitter.prototype.removeListener;

registerModule('events', { EventEmitter: EventEmitter });
