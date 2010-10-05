if (! window["Evd"] || typeof (window["Evd"]) != "object")
    window["Evd"] = {};

Evd.LongPolling = function (config) {
    this._init (config);
};

Evd.LongPolling.prototype = {
    _init: function (config) {
        this.url = config.url;
        if (this.url.charAt (this.url.length-1) != "/")
            this.url += "/";
        this.url += "lp";

        this._backlog = "";

        this._nrReceivers = 1;
        this._minSenders = 1;
        this._maxSenders = 2;

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

                if (self._backlog != "")
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

        var hdr_len, msg_len, msg, t;
        while (data != "") {
            t = this._readMsgHeader (data);
            hdr_len = t[0];
            msg_len = t[1];

            msg = data.substr (hdr_len, msg_len);
            data = data.substr (hdr_len + msg_len);

            try {
                this._deliver (msg);
            }
            catch (e) {
            }
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
                if (this.status == 200) {
                    self._xhrOnLoad (this);
                    self._recycleXhr (this);
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

    _addToBacklog: function (data) {
        this._backlog += data;
    },

    _addMsgHeader: function (msg) {
        var hdr = [];
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

    send: function (data) {
        if (this._handshaking) {
            data = this._addMsgHeader (data);
            this._addToBacklog (data);
        }
        else if (this._opened) {
            if (data == "" && this._backlog == "")
                return;

            if (data)
                data = this._addMsgHeader (data);

            var xhr = this._senders.shift ();
            if (xhr) {
                data = this._backlog + data;
                this._backlog = "";

                this._send (xhr, data);
            }
            else {
                this._addToBacklog (data);
            }
        }
        else {
            throw ("Failed to send, long-polling transport is closed");
        }
    }
};

Evd.transport = new Evd.LongPolling ({ url: "/transport" });

Evd.transport.open ();
