if (! window["Evd"] || typeof (window["Evd"]) != "object")
    window["Evd"] = {};

if (! Evd["Object"] || typeof (Evd["Object"]) != "object") {
    // Evd.Constructor
    Evd.Constructor = function (args) {
        return function (args) {
            if (! args || typeof (args) != "object" || args.constructor != Object)
                args = {};

            this._init (args);
        };
    };

    // Evd.Object
    Evd.Object = function (constructor) {
        var eventListeners = {};

        this.constructor = constructor;

        this.addEventListener = function (eventName, handler) {
            if (! eventListeners[eventName])
                eventListeners[eventName] = [];

            eventListeners[eventName].push (handler);
        };

        this.removeEventListener = function (eventName, handler) {
            if (! eventListeners[eventName])
                return;

            for (var i=0; i<eventListeners[eventName].length; i++)
                if (eventListeners[eventName][i] === handler)
                    eventListeners[eventName][i] = null;

            setTimeout (function () {
                var handlers = [];

                for (var i=0; i<eventListeners[eventName].length; i++)
                    if (eventListeners[eventName][i])
                        handlers.push (eventListeners[eventName][i]);

                eventListeners[eventName] = handlers;
            }, 1);
        };

        this.removeAllEventListeners = function () {
            eventListeners = [];
        };

        this._fireEvent = function (eventName, args) {
            if (! eventListeners[eventName])
                return;

            for (var i=0; i<eventListeners[eventName].length; i++)
                if (eventListeners[eventName][i])
                    eventListeners[eventName][i].apply (this, args);
        };
    };

    Evd.Object.extend = function (prototype1, prototype2) {
        for (var key in prototype2)
            prototype1[key] = prototype2[key];
    };
}

// Evd.Peer
Evd.Peer = new Evd.Constructor ();
Evd.Peer.prototype = new Evd.Object (Evd.Peer);

Evd.Object.extend (Evd.Peer.prototype, {
    _init: function (args) {
        this.id = args.id;
        this.transport = args.transport;
        this.backlog = [];
        this._closed = false;
    },

    send: function (msg, size) {
        return this.transport.send (this, msg, size);
    },

    sendText: function (msg) {
        return this.transport.sendText (this, msg);
    },

    receive: function () {
        return this.transport.receive (this);
    },

    receiveText: function () {
        return this.transport.receiveText (this);
    },

    close: function (gracefully) {
        if (! this._closed) {
            this._closed = true;

            if (gracefully == undefined)
                gracefully = true;

            this.transport.closePeer (this, gracefully);
        }
    }
});

// Evd.LongPolling
Evd.LongPolling = new Evd.Constructor ();
Evd.LongPolling.prototype = new Evd.Object (Evd.LongPolling);

Evd.Object.extend (Evd.LongPolling.prototype, {
    PEER_DATA_KEY: "org.eventdance.lib.LongPolling",

    _init: function (args) {
        this._peerId = args.peerId;
        this._onError = args.onError;

        this._nrReceivers = 1;
        this._nrSenders = 1;

        this._senders = [];
        this._receivers = [];

        this._opened = false;
        this._connected = false;

        this._activeXhrs = [];

        var i;
        for (i=0; i<this._nrSenders; i++) {
            var xhr = this._setupNewXhr (true);
            this._senders.push (xhr);
        }

        for (i=0; i<this._nrReceivers; i++) {
            var xhr = this._setupNewXhr (false);
            this._receivers.push (xhr);
        }
    },

    _readMsgHeader: function (data) {
        var hdr_len, msg_len;
        var hdr = data.charCodeAt (0) / 1;

        if (hdr <= 0x7F - 2) {
            hdr_len = 1;
            msg_len = hdr;
        }
        else if (hdr == 0x7F - 1) {
            hdr_len = 5;

            var len_st = data.substr (1, 4);
            msg_len = parseInt (len_st, 16);
        }
        else {
            hdr_len = 17;

            var len_st = data.substr (1, 16);
            msg_len = parseInt (len_st, 16);
        }

        return [hdr_len, msg_len];
    },

    _xhrOnLoad: function (data) {
        var hdr_len, msg_len, msg, t;
        while (data != "") {
            t = this._readMsgHeader (data);
            hdr_len = t[0];
            msg_len = t[1];

            msg = data.substr (hdr_len, msg_len);
            data = data.substr (hdr_len + msg_len);

            this._fireEvent ("receive", [msg, null]);
        }
    },

    _setupNewXhr: function (sender) {
        var self = this;

        var xhr = new XMLHttpRequest ();
        xhr._sender = sender;

        xhr.onabort = function () {
            self._recycleXhr (this);
        };

        xhr.onreadystatechange = function () {
            if (! self._connected && this.readyState == 1 && ! this._sender) {
                self._connected = true;
                self._fireEvent ("connect", [true, null]);
            }

            if (this.readyState != 4)
                return;

            // remove xhr from list of actives
            if (self._activeXhrs.indexOf (this) >= 0)
                self._activeXhrs.splice (self._activeXhrs.indexOf (this));

            self._recycleXhr (this);

            if (this.status != 200) {
                var error = new Error ("Long polling error " + this.status);
                error.code = this.status;

                if (this._sender)
                    self._fireEvent ("send", [false, error]);
                else
                    self._fireEvent ("receive", [null, error]);
            }
            else {
                if (this._sender)
                    self._fireEvent ("send", [true, null]);
                else
                    setTimeout (function () {
                                    self._connect ();
                                }, 1);

                var data = xhr.responseText.toString ();
                if (data)
                    self._xhrOnLoad (data);
            }
        };

        return xhr;
    },

    _recycleXhr: function (xhr) {
        if (! xhr._sender)
            this._receivers.push (xhr);
        else
            this._senders.push (xhr);
    },

    _connectXhr: function (xhr) {
        xhr.open ("GET", this._addr + "/receive?" + this._peerId, true);

        this._activeXhrs.push (xhr);

        xhr.send ();
    },

    _connect: function () {
        for (var i=0; i<this._receivers.length; i++) {
            var xhr = this._receivers.shift ();
            this._connectXhr (xhr);
        }
    },

    _buildMsg: function (msg) {
        var len = msg.length;
        var hdr_st = "";

        if (len <= 0x7F - 2) {
            hdr_st = String.fromCharCode (len);
        }
        else if (len <= 0xFFFF) {
            hdr_st = String.fromCharCode (0x7F - 1);

            var len_st = len.toString (16);
            while (len_st.length < 4)
                len_st = "0" + len_st;

            hdr_st += len_st;
        }
        else {
            hdr_st = String.fromCharCode (0x7F);

            var len_st = len.toString (16);
            while (len_st.length < 16)
                len_st = "0" + len_st;

            hdr_st += len_st;
        }

        return hdr_st + msg;
    },

    send: function (msgs) {
        var buf = "";
        var msg;
        while (msgs.length > 0) {
            msg = msgs.shift ();
            buf += this._buildMsg (msg);
        }

        var xhr = this._senders.shift ();

        xhr.open ("POST", this._addr + "/send?" + this._peerId, true);

        this._activeXhrs.push (xhr);

        xhr.send (buf);
    },

    canSend: function () {
        return this._senders.length > 0;
    },

    open: function (address, callback) {
        this._addr = address;
        this._opened = true;

        this._connect ();
    },

    close: function (gracefully) {
        this._opened = false;
        this._connected = false;

        var xhr;

        // cancel all active XHRs
        for (xhr in this._activeXhrs)
            xhr.abort ();
        this._activeXhrs = [];

        if (gracefully) {
            // send a 'close' command
            xhr = new XMLHttpRequest ();
            xhr.open ("POST", this._addr + "/close?" + this._peerId, false);
            xhr.send ();
        }

        this._peerId = null;
    },

    reconnect: function () {
        this._connect ();
    }
});

// Evd.WebSocket
Evd.WebSocket = new Evd.Constructor ();
Evd.WebSocket.prototype = new Evd.Object (Evd.WebSocket);

Evd.Object.extend (Evd.WebSocket.prototype, {

    _init: function (args) {
        this._peerId = args.peerId;
    },

    open: function (address, callback) {
        this._addr = address;
        this._opened = true;

        this._connect ();
    },

    _connect: function () {
        var self = this;

        if (this._ws != null) {
            this._ws.onopen = null;
            this._ws.onmessage = null;
            this._ws.onerror = null;
            this._onclose = null;
        }

        this._ws = new WebSocket (this._addr + "?" + this._peerId);
        this._ws.onopen = function () {
            self._connected = true;

            self._fireEvent ("connect", [true, null]);
            self._fireEvent ("send", [true, null]);
        };

        this._ws.onmessage = function (e) {
            self._fireEvent ("receive", [e.data, null]);
        };

        this._ws.onerror = function (e) {
            alert (e);
        };

        this._ws.onclose = function (e) {
            if (! self._opened)
                return;

            self._ws = null;

            var fatal = ! self._connected;
            self._connected = false;

            self._fireEvent ("disconnect", [fatal]);
        };
    },

    canSend: function () {
        return this._opened && this._ws != null && this._ws.readyState == 1;
    },

    send: function (data) {
        this._ws.send (data);
        this._fireEvent ("send", [true, null]);
    },

    reconnect: function () {
        this._connect ();
    },

    close: function (gracefully) {
        this._opened = false;
        this._connected = false;
        this._peerId = null;

        if (this._ws)
            this._ws.close ();
    }
});

// Evd.WebTransport
Evd.WebTransport = new Evd.Constructor ();
Evd.WebTransport.prototype = new Evd.Object (Evd.WebTransport);

Evd.Object.extend (Evd.WebTransport, {
    DEFAULT_ADDR: "/transport",
    PEER_DATA_KEY: "org.eventdance.lib.WebTransport.data",
    PEER_ID_HEADER_NAME: "X-Org-EventDance-WebTransport-Peer-Id",
    MECHANISM_HEADER_NAME: "X-Org-EventDance-WebTransport-Mechanism",
    URL_HEADER_NAME: "X-Org-EventDance-WebTransport-Url"
});

Evd.Object.extend (Evd.WebTransport.prototype, {

    _init: function (args) {
        if (args.address)
            this._addr = args.address.toString ();
        else
            this._addr = Evd.WebTransport.DEFAULT_ADDR;

        this._opened = false;
        this._outBuf = [];
        this._flushBuf = [];
        this._retryInterval = 500;
        this._retryCount = 0;

        this._availableMechanisms = "long-polling";
        if (window["WebSocket"])
            this._availableMechanisms += ";web-socket";
    },

    _onDisconnect: function (fatal) {
        if (this._mechanismName == "web-socket" && this._retryCount > 3) {
            this._availableMechanisms = this._availableMechanisms.replace (";web-socket", "");

            fatal = true;
        }

        this._retry (fatal);
    },

    _setupMechanism: function (peerId) {
        if (this._transport != null) {
            this._transport.removeAllEventListeners ();
            this._transport = null;
        }

        var transportProto = null;

        if (this._mechanismName == "long-polling")
            transportProto = Evd.LongPolling;
        else if (this._mechanismName == "web-socket")
            transportProto = Evd.WebSocket;
        else {
            // @TODO: raise error, failed to negotiate mechanism
            throw ("No mechanism can be negotiated");
            return;
        }

        var self = this;

        this._transport = new transportProto ({ peerId: peerId });

        this._transport.addEventListener ("connect",
            function (result, error) {
                self._onConnect (result, error);
            });
        this._transport.addEventListener ("receive",
            function (msg, error) {
                self._onReceive (msg, error);
            });
        this._transport.addEventListener ("send",
            function (result, error) {
                self._onFlush (result, error);
            });
        this._transport.addEventListener ("disconnect",
            function (fatal) {
                self._onDisconnect (fatal);
            });
    },

    _handshake: function () {
        var self = this;

        this._peerId = null;

        this._outBuf = [];

        var xhr = new XMLHttpRequest ();

        xhr.onreadystatechange = function () {
            if (this.readyState != 4)
                return;

            if (this.status == 200) {
                var peerId =
                    this.getResponseHeader (Evd.WebTransport.PEER_ID_HEADER_NAME);
                self._mechanismName =
                    this.getResponseHeader (Evd.WebTransport.MECHANISM_HEADER_NAME);
                self._mechanismUrl = this.getResponseHeader (Evd.WebTransport.URL_HEADER_NAME);

                self._setupMechanism (peerId);

                // create new peer
                var peer = new Evd.Peer ({
                    id: peerId,
                    transport: self
                 });
                peer[Evd.WebTransport.PEER_DATA_KEY] = {};

                self._peer = peer;
                self._transport.open (self._mechanismUrl);
            }
            else {
                self._retry (false);
            }
        };

        xhr.open ("POST", this._addr + "/handshake", true);
        xhr.setRequestHeader (Evd.WebTransport.MECHANISM_HEADER_NAME,
                              this._availableMechanisms);
        xhr.send ("");
    },

    _connect: function (peer) {
    },

    _onConnect: function (result, error) {
        this._retryCount = 0;

        this._fireEvent ("new-peer", [this._peer]);
    },

    open: function (address) {
        if (this._opened)
            throw ("Transport already opened, try closing first");

        this._opened = true;

        if (address)
            this._addr = address.toString ();

        this._handshake ();
    },

    _flushing: function () {
        return this._flushBuf.length > 0;
    },

    _flush: function () {
        if (this._transport.canSend ()) {
            this._flushBuf = this._outBuf;
            this._outBuf = [];
            this._transport.send (this._flushBuf);
        }
    },

    send: function (peer, data, size) {
        throw ("Sending raw data is not implemented. Use 'sendText()' instead");
    },

    sendText: function (peer, data) {
        if (peer != this._peer)
            throw ("Send failed, invalid peer");

        if (! data)
            return;

        this._outBuf.push (data);

        if (! this._flushing ())
            this._flush ();
    },

    _onReceive: function (msg, error) {
        if (! error) {
            if (! this._peer)
                return;

            this._peer[Evd.WebTransport.PEER_DATA_KEY].msg = msg;
            this._fireEvent ("receive", [this._peer]);
            this._peer[Evd.WebTransport.PEER_DATA_KEY].msg = null;
        }
        else {
            this._retry (error.code == 404);
        }
    },

    receiveText: function (peer) {
        return peer[Evd.WebTransport.PEER_DATA_KEY].msg;
    },

    _onFlush: function (result, error) {
        if (! error) {
            this._flushBuf = [];

            if (this._outBuf.length > 0) {
                var self = this;
                setTimeout (function () { self._flush (); }, 1);
            }
        }
        else {
            this._retry (error.code == 404);
        }
    },

    _retry: function (rehandshake) {
        if (! this._opened)
            return;

        // @TODO: implement a retry count and abort after a maximum.
        // Having fibonacci-based retry intervals would be nice.

        if (rehandshake) {
            if (this._peer)
                this._closePeer (this._peer, false);

            this._retryCount++;
            this._handshake ();
        }
        else {
            var self = this;

            // try reconnect
            setTimeout (function () {
                            if (self._transport)
                                self._transport.reconnect ();
                        }, self._retryInterval);

            // retry send
            if (this._outBuf.length > 0) {
                setTimeout (function () {
                                if (self._transport)
                                    self._flush ();
                            }, self._retryInterval);
            }
        }
    },

    _closePeer: function (peer, gracefully) {
        this._peer = null;
        this._outBuf = [];
        this._flushBuf = [];

        peer.close (gracefully);

        this._fireEvent ("peer-closed", [peer, gracefully]);
    },

    closePeer: function (peer, gracefully) {
        if (peer != this._peer)
            return;

        if (gracefully == undefined)
            gracefully = true;

        if (this._transport) {
            this._transport.removeAllEventListeners ();
            this._transport.close (gracefully);
            this._transport = null;
        }

        this._closePeer (peer, gracefully);

        if (this._opened) {
            var self = this;
            setTimeout (function () {
                            self._handshake ();
                        }, 100);
        }
    },

    close: function (gracefully) {
        if (! this._opened)
            return;

        if (gracefully == undefined)
            gracefully = true;

        this._opened = false;

        this.closePeer (this._peer, gracefully);

        this._fireEvent ("close", [gracefully]);
    }
});
