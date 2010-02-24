const MainLoop = imports.mainloop;
const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const Evd = imports.gi.Evd;
const Test = imports.common.test;
const Assert = imports.common.assert;

const TCP_PORT = 7777;
const UDP_PORT1 = 7777;
const UDP_PORT2 = 7778;

let timeout_src_id = 0;

function abort_test_by_timeout () {
    MainLoop.quit ("test");

    Assert.fail ("test timeout");
}

function testInitialState (Assert) {
    let socket = new Evd.Socket ();
    Assert.ok (socket);

    Assert.equal (socket.socket, null);
    Assert.equal (socket.family, Gio.SocketFamily.INVALID);
    Assert.equal (socket.type, Gio.SocketType.INVALID);
    Assert.equal (socket.protocol, Gio.SocketProtocol.UNKNOWN);

    // TODO: check with GJS team why closures don't work as properties
    // Assert.equal (socket.read_handler, null);

    Assert.equal (socket.connect_timeout, 0);
    Assert.equal (socket.group, null);
    Assert.equal (socket.auto_write, false);
    Assert.equal (socket.priority, 0);

    Assert.equal (socket.bandwidth_in, 0);
    Assert.equal (socket.bandwidth_out, 0);
    Assert.equal (socket.latency_in, 0);
    Assert.equal (socket.latency_out, 0);
    Assert.equal (socket.status, Evd.SocketState.CLOSED);

    Assert.equal (socket.get_socket (), socket.socket);
    Assert.equal (socket.get_context (), null);
    Assert.equal (socket.get_family (), socket.family);
    Assert.equal (socket.get_status (), Evd.SocketState.CLOSED);
    Assert.equal (socket.get_group (), socket.group);
    Assert.equal (socket.get_priority (), socket.priority);

    Assert.equal (socket.get_max_readable (), 0xffff);
    Assert.equal (socket.get_max_writable (), 0xffff);
    Assert.equal (socket.can_read (), false);
    Assert.equal (socket.can_write (), false);

    Assert.equal (socket.get_remote_address (), null);
    Assert.equal (socket.get_local_address (), null);
}

function testBindWhileActive (Assert) {
    let socket = new Evd.Socket ();

    socket.bind ("127.0.0.1:" + TCP_PORT, true);

    let error = null;
    try {
        socket.bind ("127.0.0.1:" + TCP_PORT);
    }
    catch (e) {
        error = e;
    }
    Assert.ok (error);

    socket.close ();
}

function testListenWhileActive (Assert) {
    let socket = new Evd.Socket ();

    socket.bind ("127.0.0.1:" + TCP_PORT, true);

    let error = null;
    try {
        socket.listen ("127.0.0.1:" + TCP_PORT);
    }
    catch (e) {
        error = e;
    }
    Assert.ok (error);

    socket.close ();
}

function testListenAfterBound (Assert) {
    let socket = new Evd.Socket ();

    socket.connect ("state-changed",
        function (socket, new_state, old_state) {
            if (new_state == Evd.SocketState.BOUND) {
                socket.listen (null);
            }
            else if (new_state == Evd.SocketState.LISTENING) {
                socket.close ();

                MainLoop.source_remove (timeout_src_id);
                MainLoop.quit ("test");
            }
        });
    socket.bind ("127.0.0.1:" + TCP_PORT, true);

    timeout_src_id = MainLoop.timeout_add (1000, abort_test_by_timeout);
    MainLoop.run ("test");
}

function testConnectWhileActive (Assert) {
    let socket = new Evd.Socket ();

    socket.bind ("127.0.0.1:" + TCP_PORT, true);

    let error = null;
    try {
        socket.connect_to ("127.0.0.1:" + TCP_PORT);
    }
    catch (e) {
        error = e;
    }
    Assert.ok (error);

    socket.close ();
}

function testCancelBind (Assert) {
    let socket = new Evd.Socket ();

    socket.connect ("close",
        function (socket) {
            MainLoop.source_remove (timeout_src_id);
            MainLoop.quit ("test");
        });

    socket.connect ("state-changed",
        function (socket, new_state, old_state) {
            if (new_state == Evd.SocketState.BOUND)
                Assert.fail ("cancel bind failed");
        });

    timeout_src_id = MainLoop.timeout_add (1000, abort_test_by_timeout);
    MainLoop.idle_add (
        function () {
            socket.bind ("127.0.0.1:" + TCP_PORT, true);
            socket.close ();
        });
    MainLoop.run ("test");
}

function testCancelListen (Assert) {
    let socket = new Evd.Socket ();

    socket.connect ("close",
        function (socket) {
            MainLoop.source_remove (timeout_src_id);
            MainLoop.quit ("test");
        });

    socket.connect ("state-changed",
        function (socket, new_state, old_state) {
            if (new_state == Evd.SocketState.BOUND) {
                socket.close ();
            }
            else if (new_state == Evd.SocketState.LISTENING) {
                Assert.fail ("cancel listen failed");
            }
        });

    socket.listen ("127.0.0.1:" + TCP_PORT);

    timeout_src_id = MainLoop.timeout_add (1000, abort_test_by_timeout);
    MainLoop.run ("test");
}

// common test greeting functions

const GREETING = "Hello world!";
const READ_BLOCK_SIZE = 1024;

let socket1, socket2;
let sockets_closed;
let expected_sockets_closed;
let bytes_read;

function on_socket_read (socket) {
    Assert.strictEqual (socket.constructor, Evd.Socket);
    Assert.ok (socket.can_read ());

    let [data, len] = socket.read (READ_BLOCK_SIZE);

    if (len > 0) {
        Assert.equal (len, GREETING.length);
        Assert.equal (data, GREETING);

        bytes_read += len;

        if (bytes_read == GREETING.length * 2) {
            socket1.close ();
            socket2.close ();
        }
    }
}

function on_socket_write (socket) {
    Assert.strictEqual (socket.constructor, Evd.Socket);
    Assert.ok (socket.can_write ());

    let len = socket.write (GREETING);
    Assert.equal (len, GREETING.length);
}

function on_socket_close (socket) {
    Assert.strictEqual (socket.constructor, Evd.Socket);
    Assert.equal (socket.get_status (), Evd.SocketState.CLOSED);
    Assert.equal (socket.status, Evd.SocketState.CLOSED);
    Assert.equal (socket.socket, null);

    Assert.equal (socket.can_read (), false);
    Assert.equal (socket.can_write (), false);

    Assert.equal (socket.get_remote_address (), null);
    Assert.equal (socket.get_local_address (), null);

    sockets_closed ++;
    if (sockets_closed == expected_sockets_closed) {
        MainLoop.source_remove (timeout_src_id);
        MainLoop.quit ("test");
    }
}

function on_socket_state_changed (socket, new_state, old_state) {
    if (new_state == Evd.SocketState.LISTENING) {
        Assert.equal (socket, socket1);
        Assert.equal (old_state, Evd.SocketState.BOUND);

        socket2.connect_addr (socket.get_local_address ());
    }
    else if (new_state == Evd.SocketState.CONNECTED) {
        Assert.equal (old_state, Evd.SocketState.CONNECTING);
    }
    else if (new_state == Evd.SocketState.BOUND) {
        if (socket.protocol == Gio.SocketProtocol.UDP) {
            Assert.equal (old_state, Evd.SocketState.CLOSED);
            socket.connect_to (socket.other_addr);
        }
    }
}

function on_socket_error (socket, code, msg) {
    Assert.fail ("Socket error " + code + ": " + msg);
}

function setup_greeting_sockets (socket1, socket2) {
    Assert.ok (socket1);
    Assert.ok (socket2);

    socket1.connect ("state-changed", on_socket_state_changed);
    socket2.connect ("state-changed", on_socket_state_changed);

    socket1.connect ("close", on_socket_close);
    socket2.connect ("close", on_socket_close);

    socket1.set_on_read (on_socket_read);
    socket1.set_on_write (on_socket_write);
    socket1.connect ("error", on_socket_error);

    socket2.set_on_read (on_socket_read);
    socket2.set_on_write (on_socket_write);
    socket2.connect ("error", on_socket_error);
}

function on_new_connection (server, client) {
    Assert.ok (server);
    Assert.strictEqual (server, socket1);
    Assert.equal (server.status, Evd.SocketState.LISTENING);

    Assert.ok (client);
    Assert.equal (client.status, Evd.SocketState.CONNECTED);

    client.set_on_read (on_socket_read);
    client.set_on_write (on_socket_write);
    client.connect ("close", on_socket_close);
}

function launchTcpTest (addr) {
    socket1 = new Evd.Socket ();
    socket2 = new Evd.Socket ();

    setup_greeting_sockets (socket1, socket2);

    socket1.connect ("new-connection", on_new_connection);
    socket1.listen (addr);
    Assert.equal (socket1.status, Evd.SocketState.RESOLVING);

    timeout_src_id = MainLoop.timeout_add (1000, abort_test_by_timeout);

    expected_sockets_closed = 3;
    sockets_closed = 0;
    bytes_read = 0;

    MainLoop.run ("test");

    Assert.equal (sockets_closed, 3);
    Assert.equal (bytes_read, GREETING.length * 2);
}

function testUnixTcp (Assert) {
    let ADDR = "/tmp/evd-socket-test-js";
    GLib.unlink (ADDR);
    launchTcpTest (ADDR);
}

function testInetIpv4Tcp (Assert) {
    launchTcpTest ("127.0.0.1:" + TCP_PORT);
}

function testInetIpv6Tcp (Assert) {
    launchTcpTest ("::1:" + TCP_PORT);
}

function launchUdpTest (addr1, addr2) {
    socket1 = new Evd.Socket ({protocol: Gio.SocketProtocol.UDP});
    socket2 = new Evd.Socket ({protocol: Gio.SocketProtocol.UDP});

    setup_greeting_sockets (socket1, socket2);

    socket1.bind (addr1, true);
    socket1.other_addr = addr2;
    Assert.equal (socket1.status, Evd.SocketState.RESOLVING);

    socket2.bind (addr2, true);
    socket2.other_addr = addr1;
    Assert.equal (socket2.status, Evd.SocketState.RESOLVING);

    timeout_src_id = MainLoop.timeout_add (1000, abort_test_by_timeout);

    expected_sockets_closed = 2;
    sockets_closed = 0;
    bytes_read = 0;

    MainLoop.run ("test");

    Assert.equal (sockets_closed, 2);
    Assert.equal (bytes_read, GREETING.length * 2);
}

function testUnixUdp (Assert) {
    let ADDR = "/tmp/evd-socket-test-js";
    GLib.unlink (ADDR);
//  Gio Unix socket don't seem to support UDP protocol. Bypassing.
//  launchUdpTest (ADDR, ADDR);
}

function testIpv4Udp (Assert) {
    let ADDR1 = "127.0.0.1:" + UDP_PORT1;
    let ADDR2 = "127.0.0.1:" + UDP_PORT2;
    launchUdpTest (ADDR1, ADDR2);
}

function testIpv6Udp (Assert) {
    let ADDR1 = "::1:" + UDP_PORT1;
    let ADDR2 = "::1:" + UDP_PORT2;
    launchUdpTest (ADDR1, ADDR2);
}

Test.run (this);
