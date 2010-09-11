if (! window["Evd"] || typeof (window["Evd"]) != "object")
    window["Evd"] = {};

Evd.LongPolling = function (config) {
    this._init (config);
};

Evd.LongPolling.prototype = {
    FRAME_SEP: String.fromCharCode (0),

    _init: function (config) {
        this.url = config.url;
        if (this.url.charAt (this.url.length-1) != "/")
            this.url += "/";
        this.url += "lp";

        this._backlog = "";

        this._nrReceivers = 1;
        this._minSenders = 1;
        this._maxSenders = 1;

        this._senders = [];

        this._opened = false;
        this._handshaking = false;

        for (var i=0; i<this._maxSenders; i++) {
            var xhr = this._setupNewXhr (true);
            this._senders.push (xhr);
        }
    },

    _handshake: function () {
        var self = this;

        var xhr = new XMLHttpRequest ();

        xhr.onload = function () {
            self._handshaking = false;

            if (this.status == 200) {
                self._opened = true;
                self.send ("");
                self._connect ();
            }
            else {
                /* @TODO: raise error */
                alert ("error handshaking: " + this.statusText);
            }

            this.onload = null;
        };

        this._handshaking = true;

        xhr.open ("POST", this.url, true);
        xhr.send ();
    },

    _xhrOnLoad: function () {
        var self = this._owner;
        var data = this.responseText + "";

        if (data != "") {
            var frames = data.split (self.FRAME_SEP);
            for (var i=0; i<frames.length-1; i++) {
                self._deliver (frames[i]);
            }
        }

        self._recycleXhr (this);
    },

    _xhrOnAbort: function () {
        xhr._owner._recycleXhr (this);
    },

    _setupNewXhr: function (sender) {
        var self = this;

        var xhr = new XMLHttpRequest ();
        xhr._sender = sender;
        xhr._owner = this;

        xhr.onload = this._xhrOnLoad;
        xhr.onabort = this._xhrOnAbort;

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
            if (self._backlog) {
                var data = this._backlog;
                this._backlog = "";
                this._send (xhr, data);
            }
            else {
                this._senders.push (xhr);
            }
        }
    },

    _deliver: function (data) {
        if (this._callback)
            this._callback (data, this._callbackData);
    },

    _connectXhr: function (xhr) {
        xhr.open ("GET", this.url, true);
        xhr.send ();
    },

    open: function () {
        this._handshake ();
    },

    _connect: function () {
        for (var i=0; i<this._nrReceivers; i++) {
            var xhr = this._setupNewXhr (false);
            this._connectXhr (xhr);
        }
    },

    setOnReceive: function (callback, userData) {
        this._callback = callback;
        this._callbackData = userData;
    },

    _send: function (xhr, data) {
        xhr.open ("POST", this.url, true);
        xhr.send (data);
    },

    send: function (data) {
        if (this._handshaking) {
            this._backlog += (data != "" ? this.FRAME_SEP + data : "");
        }
        else if (this._opened) {
            if (data == "" && this._backlog == "")
                return;

            var xhr = this._senders.shift ();
            if (xhr) {
                if (this._backlog != "") {
                    data = this._backlog + (data != "" ? this.FRAME_SEP + data : "");
                    this._backlog = "";
                }

                this._send (xhr, data);
            }
            else {
                if (this._backlog)
                    this._backlog += (data != "" ? this.FRAME_SEP + data : "");
                else
                    this._backlog = data;
            }
        }
        else {
            throw ("Failed to send, long-polling transport is closed");
        }
    }
};

Evd.transport = new Evd.LongPolling ({ url: "/transport" });

Evd.transport.open ();
