//
// Eduardo Lima Mitev <elima@igalia.com>
//
// Specification
//   http://wiki.commonjs.org/wiki/Unit_Testing/1.0
//
// Code pieces adapted from
//   http://github.com/280north/narwhal/blob/master/lib/test.js
//

const Assert = imports.common.assert;

function run (test) {
    if (typeof test === "string")
        test = imports[test];

    if (! test)
        throw ("Nothing to run");

    let failures = 0;

    for (let property in test) {
        if (property.match (/^test/)) {
            if (typeof test[property] == "function") {
                if (typeof test.setup === "function")
                    test.setup ();

                try {
                    test[property] (Assert);
                    failures++;
                }
                catch (e) {
                    if (e.name === "AssertionError") {
                        Assert.fail (e);
                    } else {
                        throw (e);
                    }
                }
                finally {
                    if (typeof test.teardown === "function")
                        test.teardown ();
                }
            } else {
                failures += run (test[property]);
            }
        }
    }

    return failures;
};
