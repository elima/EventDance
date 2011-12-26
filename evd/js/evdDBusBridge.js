Evd.DBus = {
    NAMESPACE: "org.eventdance.lib.dbus",

    Commands: {
        NONE:                0,
        ERROR:               1,
        REPLY:               2,
        NEW_CONNECTION:      3,
        CLOSE_CONNECTION:    4,
        OWN_NAME:            5,
        UNOWN_NAME:          6,
        NAME_ACQUIRED:       7,
        NAME_LOST:           8,
        REGISTER_OBJECT:     9,
        UNREGISTER_OBJECT:  10,
        NEW_PROXY:          11,
        CLOSE_PROXY:        12,
        CALL_METHOD:        13,
        CALL_METHOD_RETURN: 14,
        EMIT_SIGNAL:        15
    },

    ProxyFlags: {
        NONE:                        0,
        DO_NOT_LOAD_PROPERTIES: 1 << 0,
        DO_NOT_CONNECT_SIGNALS: 1 << 1,
        DO_NOT_AUTO_START:      1 << 2
    },

    MethodCallFlags: {
        NONE:               0,
        NO_AUTO_START: 1 << 0
    },

    OwnNameFlags: {
        NONE:                   0,
        ALLOW_REPLACEMENT: 1 << 0,
        REPLACE:           1 << 1
    }
};

// Evd.DBus.Connection
Evd.DBus.Connection = new Evd.Constructor ();
Evd.DBus.Connection.prototype = new Evd.Object (Evd.DBus.Connection);

Evd.DBus.Connection._serial = 0;

Evd.Object.extend (Evd.DBus.Connection.prototype, {
    _init: function (args) {
        // @TODO: validate arguments */

        var self = this;

        this._id = 0;
        this._expected = {};
        this._proxies = {};
        this._nameOwners = {};
        this._regObjs = {};

        var reuse = args.reuse;
        if (reuse === undefined)
            reuse = true;

        this._callback = args.callback;

        this._peer = args.peer;

        this._peerOnReceive = function (peer) {
            self._processMsg (peer.receiveText ());
        };
        this._peer.transport.addEventListener ("receive", this._peerOnReceive);

        this._peerOnClose = function (peer) {
            if (peer == self._peer)
                self.close ();
        };
        this._peer.transport.addEventListener ("peer-closed", this._peerOnClose);

        var addr = args.address;

        this.sendMessage (Evd.DBus.Commands.NEW_CONNECTION,
                          0,
                          [addr,reuse],
                          this._onNewConnection, this);
    },

    _processMsg: function (msgStr) {
        try {
            var msg = JSON.parse (msgStr);
            if (typeof (msg) != "object" ||
               msg.constructor != Array) {
                throw ("Message must be a JSON array");
            }
        }
        catch (e) {
            throw ("Message parsing error: " + e);
            return;
        };

        var cmd = msg[0];
        var serial = msg[1];
        var connId = msg[2];
        var subject = msg[3];
        var args;

        if (connId != this._id)
            return;

        if (cmd == Evd.DBus.Commands.CALL_METHOD) {
            args = JSON.parse (msg[4]);
            this._onMethodCalled (serial, subject, args);
        }
        else if (this._expected[serial]) {
            var closure = this._expected[serial];
            delete (this._expected[serial]);

            if (closure.cb) {
                args = JSON.parse (msg[4]);
                closure.cb.apply (closure.scope, [cmd, subject, args]);
            }
        }
        else {
            switch (cmd) {
                case Evd.DBus.Commands.EMIT_SIGNAL:
                    args = JSON.parse (msg[4]);
                    this._signalEmitted (subject, args);
                    break;

                case Evd.DBus.Commands.NAME_ACQUIRED:
                case Evd.DBus.Commands.NAME_LOST:
                    args = JSON.parse (msg[4]);
                    var owningId = subject;
                    var ownerData = this._nameOwners[owningId];
                    if (! ownerData) {
                        throw ("Error: No owner for name id '"+owningId+"'");
                    }

                    if (cmd == Evd.DBus.Commands.NAME_ACQUIRED && ownerData.nameAcquiredCb)
                        ownerData.nameAcquiredCb (this, ownerData.name, owningId);
                    else if (cmd == Evd.DBus.Commands.NAME_LOST && ownerData.nameLostCb)
                        ownerData.nameLostCb (this, ownerData.name, owningId);
                    break;

                default:
                    throw ("Error: unexpected message: " + msgStr);
                    break;
            }
        }
    },

    _sendMessage: function (cmd, serial, subject, args) {
        var argsStr = JSON.stringify (args);
        var msg = JSON.stringify ([cmd, serial, this._id, subject, argsStr]);
        this._peer.sendText (msg);
    },

    sendMessage: function (cmd, subject, args, callback, scope) {
        // @TODO: validate message arguments */

        var serial = Evd.DBus.Connection._serial++;

        var closure = {
            cmd: cmd,
            cb: callback,
            scope: scope
        };
        this._expected[serial] = closure;

        this._sendMessage (cmd, serial, subject, args);

        return closure;
    },

    _buildErrorFromArgs: function (prefix, args) {
        return new Error (prefix + " (" + args[0] + "): " + args[1]);
    },

    _onNewConnection: function (cmd, subject, args) {
        if (! this._callback)
            return;

        if (cmd == Evd.DBus.Commands.REPLY) {
            this._id = args[0];
            this._callback (this, null);
        }
        else if (cmd == Evd.DBus.Commands.ERROR) {
            this._callback (null, this._buildErrorFromArgs ("Connection failed", args));
        }
        else {
            throw ("Unexpected reply for NEW_CONNECTION command");
        }

        delete (this._callback);
    },

    newProxy: function (name, objectPath, interfaceName, flags, obj, vtable) {
        var args = [name, objectPath, interfaceName, flags];

        this.sendMessage (Evd.DBus.Commands.NEW_PROXY,
                          this._id,
                          args,
                          function (cmd, subject, argsStr) {
                              this._onNewProxy (cmd,
                                                subject,
                                                argsStr,
                                                obj,
                                                vtable);
                          },
                          this);
    },

    _onNewProxy: function (cmd, subject, args, proxyObj, vtable) {
        if (cmd == Evd.DBus.Commands.REPLY) {
            var proxyId = args[0];
            this._proxies[proxyId] = {
                proxy: proxyObj,
                vtable: vtable
            };
            vtable.onNewProxy.apply (proxyObj, [proxyId, null]);
        }
        else if (cmd == Evd.DBus.Commands.ERROR) {
            callback (null, this._buildErrorFromArgs ("Proxy failed", args));
        }
        else {
            throw ("Unexpected reply for NEW_PROXY command");
        }
    },

    _signalEmitted: function (subject, args) {
        var proxyData = this._proxies[subject];
        if (! proxyData)
            throw ("Signal emitted for unknown proxy");

        var signalName = args[0];
        var signalArgs = JSON.parse (args[1]);

        if (proxyData.vtable.onSignalEmitted)
            proxyData.vtable.onSignalEmitted.apply (proxyData.proxy,
                                                    [signalName, signalArgs]);
    },

    _onMethodCalled: function (serial, subject, args) {
        var self = this;

        var regObjId = subject;
        var obj = this._regObjs[regObjId];
        if (! obj)
            throw ("Error: Method called on unknown registered object '"+owningId+"'");

        var methodName = args[0];
        if (! obj[methodName]) {
            throw ("Method '"+methodName+"' not implemented in registered object");
        }

        var methodArgs = JSON.parse (args[1]);
        var returnArgs;

        var invObj = {
            _regObjId: regObjId,
            _serial: serial,
            methodName: methodName,
            returnValue: function (outArgs) {
                self._methodCalledReturn (this, outArgs, null);
            },
            returnError: function (err) {
                self._methodCalledReturn (this, null, err);
            }
        };

        var result;
        try {
            result = obj[methodName].apply (obj, [methodArgs, invObj]);
        }
        catch (e) {
            throw ("Method call error: " + e);
            return;
        }
    },

    _methodCalledReturn: function (invObj, outArgs, err) {
        if (! err) {
            var returnArgs = JSON.stringify (outArgs);
            var msgArgs = [returnArgs];
            this._sendMessage (Evd.DBus.Commands.CALL_METHOD_RETURN,
                               invObj._serial,
                               invObj._regObjId,
                               msgArgs);

        }
        else {
            if (! err.code)
                err.code = 0;
            var msgArgs = [err.code, err.toString ()];
            this._sendMessage (Evd.DBus.Commands.ERROR,
                               invObj._serial,
                               invObj._regObjId,
                               msgArgs);
        }
    },

    callProxyMethod: function (proxyId, methodName, args, signature, callback, flags, timeout) {
        var msgArgs = [methodName, JSON.stringify (args), signature, flags, timeout];

        this.sendMessage (Evd.DBus.Commands.CALL_METHOD,
                          proxyId,
                          msgArgs,
                          function (cmd, subject, msgArgs) {
                              this._onProxyMethodCall (cmd,
                                                       subject,
                                                       msgArgs,
                                                       callback);
                          },
                          this);
    },

    _onProxyMethodCall: function (cmd, subject, msgArgs, callback) {
        var proxyData = this._proxies[subject];
        if (! proxyData)
            throw ("Method call reponse for unknown proxy");

        if (! callback)
            return;

        var proxyObj = proxyData.proxy;
        if (cmd == Evd.DBus.Commands.CALL_METHOD_RETURN) {
            var args = JSON.parse (msgArgs[0]);
            callback.apply (proxyObj, [args, null]);
        }
        else if (cmd == Evd.DBus.Commands.ERROR) {
            callback.apply (proxyObj, [null, this._buildErrorFromArgs ("Method call failed", msgArgs)]);
        }
        else {
            throw ("Unexpected reply for NEW_PROXY command");
        }
    },

    ownName: function (name, flags, callback, nameAcquiredCb, nameLostCb) {
        var args = [name, flags];
        var ownerData = {
            name: name,
            nameAcquiredCb: nameAcquiredCb,
            nameLostCb: nameLostCb
        };
        this.sendMessage (Evd.DBus.Commands.OWN_NAME,
                          this._id,
                          args,
                          function (cmd, subject, msgArgs) {
                              this._onOwnNameResponse (cmd,
                                                       subject,
                                                       msgArgs,
                                                       callback,
                                                       ownerData);
                          },
                          this);
    },

    _onOwnNameResponse: function (cmd, subject, msgArgs, callback, ownerData) {
        if (cmd == Evd.DBus.Commands.REPLY) {
            var owningId = msgArgs[0];
            this._nameOwners[owningId] = ownerData;

            if (callback)
                callback (owningId, null);
        }
        else if (cmd == Evd.DBus.Commands.ERROR) {
            if (callback)
                callback (0, this._buildErrorFromArgs ("Own-name failed", msgArgs));
        }
        else {
            throw ("Unexpected reply for OWN_NAME command");
        }
    },

    unownName: function (owningId, callback) {
        var args = [];

        this.sendMessage (Evd.DBus.Commands.UNOWN_NAME,
                          owningId,
                          args,
                          function (cmd, subject, msgArgs) {
                              this._onUnownNameResponse (cmd,
                                                         subject,
                                                         msgArgs,
                                                         owningId,
                                                         callback);
                          },
                          this);
    },

    _onUnownNameResponse: function (cmd, subject, msgArgs, owningId, callback) {
        if (cmd == Evd.DBus.Commands.REPLY) {
            delete (this._nameOwners[owningId]);

            if (callback)
                callback (true, null);
        }
        else if (cmd == Evd.DBus.Commands.ERROR) {
            if (callback)
                callback (false, this._buildErrorFromArgs ("Unown-name failed", msgArgs));
        }
        else {
            throw ("Unexpected reply for UNOWN_NAME command");
        }
    },

    registerObject: function (object, path, iface, callback) {
        var args = [path, iface];
        this.sendMessage (Evd.DBus.Commands.REGISTER_OBJECT,
                          this._id,
                          args,
                          function (cmd, subject, msgArgs) {
                              this._onRegisterObject (cmd,
                                                      subject,
                                                      msgArgs,
                                                      object,
                                                      callback);
                          },
                          this);
    },

    _onRegisterObject: function (cmd, subject, msgArgs, obj, callback) {
        if (cmd == Evd.DBus.Commands.REPLY) {
            var regObjId = msgArgs[0];

            this._regObjs[regObjId] = obj;

            // inject prototype into object's prototype chain
            var regObj = new Evd.DBus.RegisteredObject ({
                id: regObjId,
                connection: this
            });

            var tmpProto = obj.__proto__;
            obj.__proto__ = regObj.__proto__;
            obj.__proto__.__proto__ = tmpProto;

            if (callback)
                callback (regObjId, null);
        }
        else if (cmd == Evd.DBus.Commands.ERROR) {
            if (callback)
                callback (0, this._buildErrorFromArgs ("Object registering failed", msgArgs));
        }
        else {
            throw ("Unexpected reply for REGISTER_OBJECT command");
        }
    },

    unregisterObject: function (object, callback) {
        var regObjId = object._regObjId;

        var args = [regObjId];
        this.sendMessage (Evd.DBus.Commands.UNREGISTER_OBJECT,
                          regObjId,
                          args,
                          function (cmd, subject, msgArgs) {
                              this._onUnregisterObject (cmd,
                                                        subject,
                                                        msgArgs,
                                                        object,
                                                        callback);
                          },
                          this);
    },

    _onUnregisterObject: function (cmd, subject, msgArgs, obj, callback) {
        if (cmd == Evd.DBus.Commands.REPLY) {
            var regObjId = obj._regObjId;
            delete (this._regObjs[regObjId]);

            // remove the previously injected prototype
            obj.__proto__ = obj.__proto__.__proto__;

            if (callback)
                callback (true, null);
        }
        else if (cmd == Evd.DBus.Commands.ERROR) {
            if (callback)
                callback (0, this._buildErrorFromArgs ("Object unregistering failed", msgArgs));
        }
        else {
            throw ("Unexpected reply for UNREGISTER_OBJECT command");
        }
    },

    emitSignal: function (object, signalName, args, signature) {
        var argsStr = JSON.stringify (args);
        var subject = object._regObjId;
        var msgArgs = [signalName, argsStr, signature];
        this._sendMessage (Evd.DBus.Commands.EMIT_SIGNAL,
                           0,
                           subject,
                           msgArgs);
    },

    close: function () {
        if (! this._peer)
            return;

        this._peer.transport.removeEventListener ("close", this._peerOnClose);
        this._peer.transport.removeEventListener ("receive", this._peerOnReceive);
        this._peer = null;

        this._fireEvent ("close", []);
    }
});

// Evd.DBus.RegisteredObject
Evd.DBus.RegisteredObject = new Evd.Constructor ();

Evd.Object.extend (Evd.DBus.RegisteredObject.prototype, {
    _init: function (args) {
        this.__proto__._regObjId = args.id;
        this.__proto__._dbusConn = args.connection;
    },

    emitDBusSignal: function (signalName, args, signature) {
        var conn = this._dbusConn;
        conn.emitSignal (this, signalName, args, signature);
    },

    unregister: function (callback) {
        var conn = this._dbusConn;
        conn.unregisterObject (this, callback);
    }
});

// Evd.DBus.Proxy
Evd.DBus.Proxy = new Evd.Constructor ();
Evd.DBus.Proxy.prototype = new Evd.Object (Evd.DBus.Proxy);

Evd.Object.extend (Evd.DBus.Proxy.prototype, {
    _init: function (args) {
        // @TODO: validate arguments */

        this.name = args.name;
        this.objectPath = args.objectPath;
        this.interfaceName = args.interfaceName;
        this.flags = args.flags;

        this._id = 0;
        this.connection = args.connection;
        this._callback = args.callback;
        this._defaultTimeout = 60;

        this._vtable = {
            onNewProxy: this._onNewProxy,
            onSignalEmitted: this._onSignalEmitted
        };

        this.connection.newProxy (this.name,
                                  this.objectPath,
                                  this.interfaceName,
                                  this.flags,
                                  this,
                                  this._vtable);
    },

    _onNewProxy: function (proxyId, err) {
        if (! this._callback)
            return;

        if (err == null) {
            this._id = proxyId;
            this._callback (this, null);
        }
        else {
            this._callback (null, err);
        }
        delete (this._callback);
    },

    _onSignalEmitted: function (name, args) {
        this._fireEvent ("signal-emitted", [name, args]);
        this._fireEvent (name, [args]);
    },

    call: function (methodName, args, signature, callback, flags, timeout) {
        if (flags == undefined)
            flags = Evd.DBus.CallMethodFlags.NONE;
        if (timeout == undefined)
            timeout = this._defaultTimeout;
        this.connection.callProxyMethod (this._id,
                                         methodName,
                                         args,
                                         signature,
                                         callback,
                                         flags,
                                         timeout);
    }
});

if (this["define"] !== undefined) {
    if (this["exports"] === undefined)
        var exports = {};

    exports.Connection = Evd.DBus.Connection;
    exports.Proxy = Evd.DBus.Proxy;
    exports.RegisteredObject = Evd.DBus.RegisteredObject;

    define (exports);
}
