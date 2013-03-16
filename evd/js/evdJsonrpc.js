(function () {

function defineJsonRpc (Evd) {

// Evd.Jsonrpc
var Jsonrpc = new Evd.Constructor ();
Jsonrpc.prototype = new Evd.Object ();

Evd.Object.extend (Jsonrpc.prototype, {
    _init: function (args) {
        this._invocationCounter = 0;

        this._transportWriteCb = args.transportWriteCb;
        this._methodCallCb = args.methodCallCb;

        this._invocationsIn = {};
        this._invocationsOut = {};

        this._registeredMethods = {};

        this._transports = [];

        var self = this;
        this._transportOnReceive = function (peer) {
            var data = peer.receiveText ();
            self.transportRead (data, peer);
        };
    },

    transportRead: function (data, context) {
        try {
            var msg = JSON.parse (data);
        }
        catch (e) {
            throw ("Malformed JSON-RPC msg");
        }

        if (typeof (msg) != "object" || msg.constructor != Object ||
            msg["id"] === undefined) {
            throw ("Received invalid JSON-RPC msg");
        }

        if (msg["result"] !== undefined && msg["error"] !== undefined) {
            /* a JSON-RPC response */

            if (this._invocationsOut[msg.id] === undefined) {
                /* unexpected response, discard silently? */
                return;
            }
            else {
                var invObj = this._invocationsOut[msg.id];
                delete (this._invocationsOut[msg.id]);

                if (invObj.callback)
                    invObj.callback (msg.result, msg.error);
            }
        }
        else if (msg["method"] !== undefined && msg["params"] !== undefined) {
            /* a JSON-RPC request */

            if (msg.id === null) {
                /* a JSON-RPC notification */
                this._fireEvent (msg.method, [msg.params, context]);
            }
            else {
                /* a JSON-RPC method call */

                var self = this;
                var invObj = this._newInvocationObj (msg.id,
                                                     msg.method,
                                                     msg.params,
                                                     null);
                var key = invObj.id.toString ();
                this._invocationsIn[key] = invObj;

                if (this._registeredMethods[invObj.method]) {
                    this._registeredMethods[invObj.method] (this,
                                                            invObj.params,
                                                            key,
                                                            context);
                }
                else if (this._methodCallCb)
                    this._methodCallCb (invObj.method, invObj.params, key, context);
                else {
                    // method not handled, respond call with error
                    this.respondError (key, "Method '"+invObj.method+"' not handled", context);
                }
            }
        }
        else {
            throw ("Invalid JSON-RPC message");
        }
    },

    _newInvocationObj: function (id, methodName, params, callback) {
        var invObj = {
            id: id,
            method: methodName,
            params: params,
            callback: callback
        };

        return invObj;
    },

    _transportWrite: function (msg, context) {
        if (context && typeof (context) == "object" &&
            context.__proto__.constructor == Evd.Peer) {
            context.sendText (msg);
        }
        else if (this._transportWriteCb) {
            this._transportWriteCb (this, msg);
        }
        else {
            throw ("No transport to send message over");
        }
    },

    callMethod: function (methodName, params, callback, context) {
        // @TODO: validate params to be an array

        var msg = {
            method: methodName,
            params: params
        };

        this._invocationCounter++;
        msg.id = this._invocationCounter + "";

        var invObj = this._newInvocationObj (msg.id, methodName, params, callback);

        this._invocationsOut[msg.id] = invObj;

        var msgSt = JSON.stringify (msg);

        this._transportWrite (msgSt, context);

        return invObj;
    },

    _respond: function (invocationId, result, error, context) {
        if (this._invocationsIn[invocationId] === undefined)
            throw ("No active method invocation with such id");

        var invObj = this._invocationsIn[invocationId];
        var msg = {
            id: invObj.id,
            result: result,
            error: null
        };

        var msgSt = JSON.stringify (msg);

        this._transportWrite (msgSt, context);
    },

    respond: function (invocationId, result, context) {
        return this._respond (invocationId, result, null, context);
    },

    respondError: function (invocationId, error, context) {
        return this._respond (invocationId, null, error, context);
    },

    registerMethod: function (methodName, callback) {
        this._registeredMethods[methodName] = callback;
    },

    unregisterMethod: function (methodName) {
        delete (this._registeredMethods[methodName]);
    },

    useTransport: function (transport) {
        transport.addEventListener ("receive", this._transportOnReceive);
    },

    unuseTransport: function (transport) {
        transport.removeEventListener ("receive", this._transportOnReceive);
    },

    sendNotification: function (notificationName, params, context) {
        // @TODO: validate params to be an array

        var msg = {
            id: null,
            method: methodName,
            params: params
        };

        var msgSt = JSON.stringify (msg);

        this._transportWrite (msgSt, context);
    }
});

    return Object.freeze (Jsonrpc);
} // defineJsonrpc()

     if (window["define"] && define.constructor == Function && define.amd)
         define (["./evdWebTransport.js"], defineJsonRpc);
     else if (! window["Evd"])
         throw ("Evd namespace not found, you need evdWebTransport.js");
     else
         window.Evd.Jsonrpc = defineJsonrpc (Evd);

}) ();
