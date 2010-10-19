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
    Evd.Object = function () {
        var eventListeners = {};

        this.addEventListener = function (eventName, handler) {
            if (! eventListeners[eventName])
                eventListeners[eventName] = [];

            eventListeners[eventName].push (handler);
        };

        this.removeEventListener = function (eventName, handler) {
            if (! eventListeners[eventName])
                return;

            var i = 0;
            while (i < eventListeners[eventName].length)
                if (eventListeners[eventName][i] == handler)
                    eventListeners[eventName].splice (i, 1);
                else
                    i++;
        };

        this._fireEvent = function (eventName, args) {
            if (! eventListeners[eventName])
                return;

            for (var i=0; i<eventListeners[eventName].length; i++)
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
Evd.Peer.prototype = new Evd.Object ();

Evd.Object.extend (Evd.Peer.prototype, {
    _init: function (args) {
        this.id = args.id;
        this.transport = args.transport;

        this.backlog = [];
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

    close: function () {
        return this.transport.closePeer (this);
    }
});

// Evd.LongPolling
Evd.LongPolling = new Evd.Constructor ();
Evd.LongPolling.prototype = new Evd.Object ();

Evd.Object.extend (Evd.LongPolling.prototype, {
    PEER_DATA_KEY: "org.eventdance.lib.LongPolling",
    PEER_ID_HEADER_NAME: "X-Org-EventDance-Peer-Id",

    _init: function (args) {
        if (! args.url) {
            this.url = "/";
        }
        else {
            this.url = args.url;
            if (this.url.charAt (this.url.length-1) != "/")
                this.url += "/";
        }
        this.url += "lp";

        this._nrReceivers = 1;
        this._nrSenders = 1;

        this._senders = [];

        this._opened = false;
        this._handshaking = false;

        this.peer = null;

        this._activeXhrs = [];

        for (var i=0; i<this._nrSenders; i++) {
            var xhr = this._setupNewXhr (true);
            this._senders.push (xhr);
        }
    },

    _handshake: function () {
        var self = this;

        var xhr = new XMLHttpRequest ();

        xhr.onreadystatechange = function () {
            if (this.readyState != 4)
                return;

            self._handshaking = false;

            if (this.status == 200) {
                self._opened = true;

                var peerId = this.getResponseHeader (self.PEER_ID_HEADER_NAME);

                // create new peer
                var peer = new Evd.Peer ({
                    id: this.getResponseHeader (self.PEER_ID_HEADER_NAME),
                    transport: self
                });
                peer.getRemoteId = function () {
                    return peerId;
                };
                peer[self.PEER_DATA_KEY] = {};

                // @TODO: by now it is assumed that only one peer can use
                // this transport
                self.peer = peer;

                self._fireEvent ("new-peer", [peer]);

                self._connect ();
            }
            else {
                throw ("error handshaking: " + this.statusText);
            }

            this.onload = null;
        };

        this._opened = false;
        this._handshaking = true;

        xhr.open ("POST", this.url + "/handshake", true);
        xhr.send ();
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

    _xhrOnLoad: function (xhr) {
        var data = xhr.responseText + "";

        var peer = this.peer;
        if (! peer)
            return;

        var hdr_len, msg_len, msg, t;
        while (data != "") {
            t = this._readMsgHeader (data);
            hdr_len = t[0];
            msg_len = t[1];

            msg = data.substr (hdr_len, msg_len);
            data = data.substr (hdr_len + msg_len);

            peer[this.PEER_DATA_KEY]["msg"] = msg;
            try {
                this._fireEvent ("receive", [peer]);
            }
            catch (e) {
                alert (e);
            }
            delete (peer[this.PEER_DATA_KEY]["msg"]);
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
            if (this.readyState == 4) {
                // remove xhr from list of actives
                if (self._activeXhrs.indexOf (this) >= 0)
                    self._activeXhrs.splice (self._activeXhrs.indexOf (this));

                if (this.status == 200) {
                    self._xhrOnLoad (this);
                    self._recycleXhr (this);
                }
                else if (this.status == 404) {
                    self._closePeer (self.peer, false);
                }
                else {
                    var xhr = this;
                    setTimeout (function () {
                                    self._recycleXhr (xhr);
                                    xhr = null;
                                }, 100);
                }
            }
        };

        return xhr;
    },

    _recycleXhr: function (xhr) {
        var self = this;

        if (! xhr._sender) {
            setTimeout (function () {
                self._connectXhr (xhr);
            }, 1);
        }
        else {
            this._senders.push (xhr);
            if (this.peer.backlog.length > 0)
                this.sendText (this.peer, "");
        }
    },

    _connectXhr: function (xhr) {
        xhr.open ("GET", this.url + "/receive", true);
        xhr.setRequestHeader (this.PEER_ID_HEADER_NAME, this.peer.getRemoteId ());

        this._activeXhrs.push (xhr);

        xhr.send ();
    },

    open: function () {
        if (! this.opened)
            this._handshake ();
    },

    _connect: function () {
        for (var i=0; i<this._nrReceivers; i++) {
            var xhr = this._setupNewXhr (false);
            this._connectXhr (xhr);
        }
    },

    _send: function (peer, xhr, data) {
        xhr.open ("POST", this.url + "/send", true);
        xhr.setRequestHeader (this.PEER_ID_HEADER_NAME, peer.getRemoteId ());

        this._activeXhrs.push (xhr);

        xhr.send (data);
    },

    _addMsgHeader: function (msg) {
        var hdr = [];
        // @TODO: calculate message length accurately, considering that
        // it is UTF-16. By now, use 'length' property.
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

    send: function (peer, data, size) {
        throw ("Sending raw data is not implemented. Use 'sendText()' instead");
    },

    sendText: function (peer, data) {
        if (this._handshaking || this._senders.length == 0) {
            peer.backlog.push (data);
        }
        else if (this._opened) {
            var buf = "";
            var msg;
            while (peer.backlog.length > 0) {
                msg = peer.backlog.shift ();
                buf += this._addMsgHeader (msg);
            }

            if (data)
                buf += this._addMsgHeader (data);

            if (buf) {
                var xhr = this._senders.shift ();
                this._send (peer, xhr, buf);
            }
        }
        else {
            throw ("Failed to send, long-polling transport is closed");
        }
    },

    receive: function (peer) {
        throw ("Receiving raw data is not implemented. Use 'receiveText()' instead");
    },

    receiveText: function (peer) {
        var data = peer[this.PEER_DATA_KEY];
        if (! data || typeof (data) != "object")
            return null;
        else
            return data["msg"];
    },

    _closePeer: function (peer, gracefully) {
        if (! this._opened)
            return;

        this._opened = false;
        this._handshaking = false;
        this.peer = null;

        // send a 'close' command
        xhr = new XMLHttpRequest ();
        xhr.open ("POST", this.url + "/close", false);
        xhr.setRequestHeader (this.PEER_ID_HEADER_NAME, peer.getRemoteId ());
        xhr.send ();

        // cancel all active XHRs
        for (var xhr in this._activeXhrs)
            xhr.abort ();
        this._activeXhrs = [];

        this._fireEvent ("peer-closed", [peer, gracefully]);
    },

    closePeer: function (peer) {
        this._closePeer (peer, true);
    },

    close: function () {
        if (! this._opened)
            return;

        this.closePeer (this.peer);
    }
});
