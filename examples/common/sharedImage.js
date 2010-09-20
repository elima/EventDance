/*
 * sharedImage.js
 *
 * This file is part of the 'sharedImage.js' example.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

function SharedImage (container, onReady) {
    this._init (container, onReady);
}

SharedImage.prototype = {
    _init: function (container, onReady) {
        this._cnt = container;
        this._onReady = onReady;

        this._createElements ();

        this._drawn = false;
    },

    _createElements: function () {
        var self = this;

        this._canvas = document.createElement ("canvas");
        this._canvas.className = "canvas";
        this._ctx = this._canvas.getContext ("2d");

        this._wrapper = document.createElement ("div");
        this._wrapper.className = "wrapper";

        this._wrapper.onmousedown = function (e) {
            this.grabbed = true;
            this.grab_x = e.x;
            this.grab_y = e.y;

            var cmd = ["grab", {x: e.clientX, y: e.clientY}];
            var msg = Json.encode (cmd);
            Evd.transport.send (msg);
        };

        this._wrapper.onmousemove = function (e) {
            if (this.grabbed) {
                var cmd = ["move", {x: e.clientX, y: e.clientY}];
                var msg = Json.encode (cmd);
                Evd.transport.send (msg);
            }
        };

        function ungrab () {
            if (this.grabbed) {
                var cmd = ["ungrab"];
                var msg = Json.encode (cmd);
                Evd.transport.send (msg);

                this.grabbed = false;
            }
        };

        this._wrapper.onmouseup = ungrab;
        this._wrapper.onmouseout = ungrab;

        this._viewport = document.createElement ("div");
        this._viewport.className = "viewport";

        this._indexBox = document.createElement ("div");
        this._indexBox.className = "index_box";

        this._wrapper.appendChild (this._canvas);
        this._viewport.appendChild (this._indexBox);
        this._viewport.appendChild (this._wrapper);
        this._cnt.appendChild (this._viewport);

        this._img = new Image ();
        this._img.onload = function () {
            self._aspectRatio = this.width / this.height;

            if (self._onReady)
                self._onReady ();

            self._requestUpdate ();
        };
        this._img.src = "igalia.png";
    },

    setPosition: function (x, y) {
        this._wrapper.style.left = x + "px";
        this._wrapper.style.top = y + "px";
    },

    setRotation: function (angle) {
        var st = "rotate(" + angle + "deg)";
        this._canvas.style.transform = st;
        this._canvas.style.webkitTransform = st;
        this._canvas.style.OTransform = st;
        this._canvas.style.MozTransform = st;
    },

    setSize: function (w, h) {
        this._canvas.setAttribute ("width", w);
        this._canvas.setAttribute ("height", h);

        this._ctx.drawImage (this._img, 0, 0, w, h);

        this._drawn = true;
    },

    update: function (args) {
        if (args.w)
            this.setSize (args.w, args.w / this._aspectRatio);
        else if (! this._drawn)
            this.setSize (320, 320 / this._aspectRatio);

        if (args.r)
            this.setRotation (args.r);

        if (args.x && args.y) {
            this.setPosition (args.x, args.y);
        }
    },

    _requestUpdate: function () {
        var msg = '["req-update"]';
        Evd.transport.send (msg);
    },

    setViewportIndex: function (index) {
        this._indexBox.innerHTML = index/1;
    }
};
