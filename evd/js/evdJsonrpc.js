// Evd.Jsonrpc
Evd.Jsonrpc = new Evd.Constructor ();
Evd.Jsonrpc.prototype = new Evd.Object ();

Evd.Object.extend (Evd.Jsonrpc.prototype, {
    _init: function (args) {
        this._invocationCounter = 0;

        /* @TODO: validate args */
        this._transportWriteCb = args.transportWriteCb;
        this._methodCallCb = args.methodCallCb;

        this._invocationsIn = {};
        this._invocationsOut = {};
    },

    transportRead: function (data) {
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
            /* A JSON-RPC response */

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
            /* A JSON-RPC request */

            var self = this;
            var invObj = this._newInvocationObj (msg.id,
                                                 msg.method,
                                                 msg.params,
                                                 null);
            var key = invObj.id.toString ();
            this._invocationsIn[key] = invObj;

            if (this._methodCallCb)
                this._methodCallCb (invObj.method, invObj.params, key);
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

    callMethod: function (methodName, params, callback) {
        /* @TODO: validate params to be an array */

        var msg = {
            method: methodName,
            params: params
        };

        this._invocationCounter++;
        msg.id = this._invocationCounter + "";

        var invObj = this._newInvocationObj (msg.id, methodName, params, callback);

        this._invocationsOut[msg.id] = invObj;

        var msgSt = JSON.stringify (msg);

        if (this._transportWriteCb)
            this._transportWriteCb (this, msgSt);

        return invObj;
    },

    _respond: function (invocationId, result, error) {
        if (this._invocationsIn[invocationId] === undefined)
            throw ("No active method invocation with such id");

        var invObj = this._invocationsIn[invocationId];
        var msg = {
            id: invObj.id,
            result: result,
            error: null
        };

        var msgSt = JSON.stringify (msg);

        if (this._transportWriteCb)
            this._transportWriteCb (this, msgSt);
    },

    respond: function (invocationId, result) {
        return this._respond (invocationId, result, null);
    },

    respondError: function (invocationId, error) {
        return this._respond (invocationId, null, error);
    }
});
