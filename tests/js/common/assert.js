//
// Eduardo Lima Mitev <elima@igalia.com>
//
// Specification
//   http://wiki.commonjs.org/wiki/Unit_Testing/1.0
//
// Code pieces adapted from
//   http://github.com/280north/narwhal/blob/master/lib/assert.js
//

let assert = this;

assert.AssertionError = function (options) {
    if (typeof options == "string")
        options = {"message": options};
    this.name = "AssertionError";
    this.message = options.message;
    this.actual = options.actual;
    this.expected = options.expected;
    this.operator = options.operator;
};

assert.AssertionError.prototype = {
    __proto__: Error.prototype,

    toString: function () {
        if (this.message) {
            return [
                this.name + ":",
                this.message
            ].join (" ");
        } else {
            return [
                this.name + ":",
                this.expected,
                this.operator,
                this.actual
            ].join (" ");
        }
    },

    toSource: function () {
        return "new AssertionError " +
            Object.prototype.toSource.call (this) + "";
    }
};

assert.pass = function () {
};

assert.error = function () {
};

assert.fail = function (options) {
    throw (new assert.AssertionError (options));
};

assert.ok = function (value, message) {
    if (!!!value)
        (this.fail || assert.fail) ({
            "actual": value,
            "expected": true,
            "message": message,
            "operator": "=="
        });
    else
        (this.pass || assert.pass) (message);
};

assert.equal = function (actual, expected, message) {
    if (actual != expected)
        (this.fail || assert.fail) ({
            "actual": actual,
            "expected": expected,
            "message": message,
            "operator": "=="
        });
    else
        (this.pass || assert.pass) (message);
};

assert.notEqual = function (actual, expected, message) {
    if (actual == expected)
        (this.fail || assert.fail) ({
            "actual": actual,
            "expected": expected,
            "message": message,
            "operator": "!="
        });
    else
        (this.pass || assert.pass) (message);
};

assert.strictEqual = function (actual, expected, message) {
    if (actual !== expected)
        (this.fail || assert.fail) ({
            "actual": actual,
            "expected": expected,
            "message": message,
            "operator": "==="
        });
    else
        (this.pass || assert.pass) (message);
};

assert.notStrictEqual = function (actual, expected, message) {
    if (actual === expected)
        (this.fail || assert.fail) ({
            "actual": actual,
            "expected": expected,
            "message": message,
            "operator": "!=="
        });
    else
        (this.pass || assert.pass) (message);
};

assert["throws"] = function (block, Error, message) {
    let threw = false;
    let exception = null;

    if (typeof Error == "string") {
        message = Error;
        Error = undefined;
    }

    try {
        block ();
    } catch (e) {
        threw = true;
        exception = e;
    }

    if (! threw) {
        (this.fail || assert.fail) ({
            "message": message,
            "operator": "throw"
        });
    } else if (Error) {
        if (exception instanceof Error)
            (this.pass || assert.pass) (message);
        else
            throw exception;
    } else {
        (this.pass || assert.pass) (message);
    }
};
