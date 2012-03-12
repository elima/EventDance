/*
 * sharedImageServer.js
 *
 * This file is part of the 'sharedImage.js' example.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

const Lang = imports.lang;
const Evd = imports.gi.Evd;

function SharedImageServer (args) {
    this._init (args);
}

SharedImageServer.prototype = {
    DEFAULT_VP_WIDTH: 640,
    DEFAULT_VP_HEIGHT: 400,
    IMAGE_WIDTH: 320,
    IMAGE_HEIGHT: 320,

    UPDATE_SIZE: 1,
    UPDATE_ROTATION: 2,
    UPDATE_POSITION: 4,
    UPDATE_ALL: 7,

    _init: function (args) {
        this.image = {
            w: this.IMAGE_WIDTH,
            h: this.IMAGE_HEIGHT,
            r: 0,
            url: "img.png",
            grabs: {}
        };

        this.image.x = -this.image.w / 2;
        this.image.y = -this.image.h / 2;

        this.image.s =
            Math.floor (Math.sqrt (Math.abs (this.image.w * this.image.w) +
                                   Math.abs (this.image.h * this.image.h)));

        this._viewports = [];
        this._freeViewports = [];

        this._mapping = {};

        if (args["rotate"])
            this._rotateSrcId = Evd.timeout_add (null,
                                                 35,
                                                 0,
                                                 Lang.bind (this,
                                                            this._rotateImage));

        this._updateFlags = 0;
    },

    _rotateImage: function () {
        this.image.r = (this.image.r + 1) % 360;
        this._updateFlags |= this.UPDATE_ROTATION;

        this.updateAllPeers ();

        return true;
    },

    _calculateViewportCoordsFromIndex: function (index) {
        var jumps = [{dx:-1, dy: 0},
                     {dx: 0, dy:-1},
                     {dx: 1, dy: 0},
                     {dx: 0, dy: 1}];

        var i = 0;
        var c = 0;
        var times = 1;
        var ct = 0;
        var x = 0, y = 0;
        var j = 0;

        for (i=0; i<index; i++) {
            x += this.DEFAULT_VP_WIDTH * jumps[j].dx;
            y += this.DEFAULT_VP_HEIGHT * jumps[j].dy;

            c++;
            if (c == times) {
                c = 0;
                j = (j + 1) % jumps.length;
            }

            ct ++;
            if (ct == 2) {
                ct = 0;
                times ++;
            }
        }

        return [x, y];
    },

    _createNewViewport: function (index) {
        var vp = {
            w: this.DEFAULT_VP_WIDTH,
            h: this.DEFAULT_VP_HEIGHT,
            needUpdate: false
        };

        var coord = this._calculateViewportCoordsFromIndex (index);
        var x = coord[0];
        var y = coord[1];

        vp.x = x - this.DEFAULT_VP_WIDTH / 2;
        vp.y = y - this.DEFAULT_VP_HEIGHT / 2;

        this._viewports[index] = vp;

        return vp;
    },

    _mapViewport: function (index, peer) {
        this._mapping[peer.id] = index;
        this._viewports[index].owner = peer;
    },

    acquireViewport: function (peer) {
        var index;

        if (this._freeViewports.length == 0) {
            index = this._viewports.length;
            this._createNewViewport (index);
        }
        else {
            index = this._freeViewports.shift ();
        }

        this._mapViewport (index, peer);

        return index;
    },

    releaseViewport: function (peer) {
        var vpIndex = this._mapping[peer.id];
        if (vpIndex == undefined)
            return false;
        var vp = this._viewports[vpIndex];

        this.ungrabImage (peer);
        delete (vp.owner);
        delete (this._mapping[peer.id]);

        if (vpIndex == this._viewports.length - 1) {
            delete (this._viewports[vpIndex]);
            this._viewports.length--;
        }
        else {
            this._freeViewports.push (vpIndex);
        }

        return vpIndex;
    },

    _translateToViewportCoords: function (viewport, x, y) {
        return [x - viewport.x, y - viewport.y];
    },

    _translateFromViewportCoords: function (viewport, x, y) {
        return [viewport.x + x, viewport.y + y];
    },

    updatePeer: function (peer, flags) {
        var vpIndex = this._mapping[peer.id];
        if (vpIndex == undefined)
            return false;
        var vp = this._viewports[vpIndex];

        var coord = this._translateToViewportCoords (vp,
                                                     this.image.x,
                                                     this.image.y);
        var x = coord[0];
        var y = coord[1];


        var obj = {};

        if ( (flags & this.UPDATE_POSITION) > 0) {
            obj.x = x;
            obj.y = y;
        }

        if ( (flags & this.UPDATE_ROTATION) > 0)
            obj.r = this.image.r;

        if ( (flags & this.UPDATE_SIZE) > 0) {
            obj.w = this.image.w;
        }

        if (this._peerOnUpdateFunc)
            this._peerOnUpdateFunc (peer, obj);

        return true;
    },

    _viewportNeedsUpdate: function (vp) {
        var needsUpdate = vp.needsUpdate;

        vp.needsUpdate =
            ( ( (this.image.x >= vp.x && this.image.x <= vp.x + vp.w) ||
                (this.image.x+this.image.w >= vp.x &&
                 this.image.x+this.image.w <= vp.x + vp.w) ) &&
              ( (this.image.y >= vp.y && this.image.y <= vp.y + vp.h) ||
                (this.image.y+this.image.h >= vp.y &&
                 this.image.y+this.image.h <= vp.y + vp.h) ) ) ||

            ( ( (vp.x >= this.image.x && vp.x <= this.image.x + this.image.w) ||
                (vp.x + vp.w >= this.image.x &&
                 vp.x + vp.w <= this.image.x + this.image.w) ) &&
              ( (vp.y >= this.image.y && vp.y <= this.image.y + this.image.h) ||
                (vp.y + vp.h >= this.image.y &&
                 vp.y + vp.h <= this.image.y + this.image.h) ) );

        return needsUpdate;
    },

    updateAllPeers: function (force) {
        var vp, peer;

        for (var i in this._mapping) {
            var index = this._mapping[i];

            vp = this._viewports[index];
            if (force || this._viewportNeedsUpdate (vp)) {
                peer = vp.owner;
                this.updatePeer (peer, this._updateFlags);
                peer = null;
            }
            vp = null;
        }

        this._updateFlags = 0;
    },

    grabImage: function (peer, args) {
        var vpIndex = this._mapping[peer.id];
        if (vpIndex == undefined)
            return false;
        var vp = this._viewports[vpIndex];

        if (args.x == undefined || args.y == undefined)
            return false;

        var coord = this._translateFromViewportCoords (vp, args.x, args.y);
        var x = coord[0];
        var y = coord[1];


        this.image.grabs[peer.id] = {x: x, y: y};

        return true;
    },

    ungrabImage: function (peer) {
        var vpIndex = this._mapping[peer.id];
        if (vpIndex == undefined)
            return false;
        var vp = this._viewports[vpIndex];

        delete (this.image.grabs[peer.id]);

        return true;
    },

    _calculateS: function (x1, y1, x2, y2) {
        var dx = Math.abs (x2 - x1);
        var dy = Math.abs (y2 - y1);
        return Math.floor (Math.sqrt (dx * dx + dy * dy));
    },

    moveImage: function (peer, args) {
        var force = false;

        var vpIndex = this._mapping[peer.id];
        if (vpIndex == undefined)
            return false;
        var vp = this._viewports[vpIndex];

        var grab = this.image.grabs[peer.id];
        if (grab == undefined)
            return false;

        var coord = this._translateFromViewportCoords (vp, args.x, args.y);
        var x = coord[0];
        var y = coord[1];

        var dx = x - grab.x;
        var dy = y - grab.y;

        var grabCount = 0;
        for (var i in this.image.grabs)
            grabCount++;

        if (grabCount == 1) {
            this.image.x += dx;
            this.image.y += dy;
        }
        else if (grabCount == 2) {
            var otherGrab;
            for (var id in this.image.grabs)
                if (id != peer.id) {
                    otherGrab = this.image.grabs[id];
                    break;
                }

            var ds1 = this._calculateS (otherGrab.x, otherGrab.y,
                                        grab.x, grab.y);
            var ds2 = this._calculateS (otherGrab.x, otherGrab.y, x, y);

            var per = (ds2 / ds1) * 100;

            var newW = Math.abs ( (this.image.w * per) / 100);
            var newH = Math.abs ( (this.image.h * per) / 100);

            this.image.x += (this.image.w - newW) / 2;
            this.image.y += (this.image.h - newH) / 2;

            this.image.w = newW;
            this.image.h = newH;

            force = true;
            this._updateFlags |= this.UPDATE_SIZE;
        }

        grab.x = x;
        grab.y = y;

        this._updateFlags |= this.UPDATE_POSITION;

        this.updateAllPeers (force);

        return true;
    },

    setPeerOnUpdate: function (func) {
        this._peerOnUpdateFunc = func;
    }
};
