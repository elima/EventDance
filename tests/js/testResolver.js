const MainLoop = imports.mainloop;
const Gio = imports.gi.Gio;
const Evd = imports.gi.Evd;
const Test = imports.common.test;

function abort_test_by_timeout () {
    MainLoop.quit ("test");

    Test.Assert.fail ("test timeout");
}

function testGetDefault (Assert) {
    let resolver = Evd.Resolver.get_default ();
    Assert.ok (resolver);

    let resolver1 = Evd.Resolver.get_default ();
    Assert.strictEqual (resolver, resolver1);
}

function testIPv4NoResolve (Assert) {
    const ADDR = "127.0.0.1";
    const PORT = 80;

    let timeout_src_id = MainLoop.timeout_add (1000, abort_test_by_timeout);

    let resolver = Evd.Resolver.get_default ();
    let request = resolver.resolve_with_closure (ADDR + ":" + PORT,
        function (res, req) {
            Assert.equal (req.is_active (), false);

            MainLoop.source_remove (timeout_src_id);
            MainLoop.quit ("test");

            Assert.strictEqual (resolver, res);
            Assert.strictEqual (request, req);

            let addresses = request.get_result ();
            Assert.ok (addresses);
            Assert.strictEqual (typeof (addresses), "object");
            Assert.strictEqual (addresses.constructor, Array);
            Assert.strictEqual (addresses.length, 1);

            let addr = addresses[0];
            Assert.strictEqual (addr.constructor, Gio.InetSocketAddress);
            Assert.strictEqual (addr.get_port (), PORT);

            let inet_addr = addresses[0].get_address ();
            Assert.strictEqual (inet_addr.to_string (), ADDR);
        });
    Assert.equal (request.is_active (), true);

    MainLoop.run ("test");
}

function testResolveLocalhost (Assert) {
    const ADDR = "localhost";
    const PORT = 22;
    const NEW_PORT = 23;

    let timeout_src_id = MainLoop.timeout_add (1000, abort_test_by_timeout);

    let resolver = Evd.Resolver.get_default ();
    let request = resolver.resolve_with_closure (ADDR + ":" + PORT,
        function (res, req) {
            MainLoop.source_remove (timeout_src_id);
            MainLoop.quit ("test");

            Assert.strictEqual (resolver, res);
            Assert.strictEqual (request, req);

            let addresses = request.get_result ();
            Assert.ok (addresses);
            Assert.strictEqual (typeof (addresses), "object");
            Assert.strictEqual (addresses.constructor, Array);
            Assert.ok (addresses.length >= 1);

            for each (let addr in addresses) {
                Assert.strictEqual (addr.constructor, Gio.InetSocketAddress);
                Assert.strictEqual (addr.get_port (), NEW_PORT);

                if (addr.family == Gio.SocketFamily.IPV4) {
                    let inet_addr = addr.get_address ();
                    Assert.strictEqual (inet_addr.to_string (), "127.0.0.1");
                }
                else if (addr.family == Gio.SocketFamily.IPV6) {
                    let inet_addr = addr.get_address ();
                    Assert.strictEqual (inet_addr.to_string (), "::1");
                }
            }
        });
    request.port = NEW_PORT;

    MainLoop.run ("test");
}

function testCancel (Assert) {
    const ADDR = "localhost:1024";

    let resolver = Evd.Resolver.get_default ();
    let request = resolver.resolve_with_closure (ADDR,
        function (res, req) {
            MainLoop.source_remove (timeout_src_id);
            MainLoop.quit ("test");

            Assert.fail ("request cancellation failed");
        });
    request.cancel ();

    Assert.equal (request.is_active (), false);

    let timeout_src_id = MainLoop.timeout_add (500,
        function () {
            MainLoop.quit ("test");
        });
    MainLoop.run ("test");
}

Test.run (this);
