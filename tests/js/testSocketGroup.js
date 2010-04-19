const MainLoop = imports.mainloop;
const Lang = imports.lang;
const Evd = imports.gi.Evd;
const Test = imports.common.test;
const Assert = imports.common.assert;

function testInitialState (Assert) {
    let group = new Evd.SocketGroup ();
    Assert.ok (group);
    Assert.ok (group instanceof Evd.Stream);
    Assert.strictEqual (group.read_closure, null);
    Assert.strictEqual (group.get_on_read (), null);
    Assert.strictEqual (group.write_closure, null);
    Assert.strictEqual (group.get_on_write (), null);

    let read_handler = function () {};
    let write_handler = function () {};

    group.set_on_read (read_handler);
    Assert.ok (group.get_on_read ());
    Assert.ok (group.read_closure);

    group.set_on_write (write_handler);
    Assert.ok (group.get_on_write ());
    Assert.ok (group.write_closure);
}

function testAddRemoveSocket (Assert) {
    let group = new Evd.SocketGroup ();
    let socket = new Evd.Socket ();

    // socket can be added to a group by invoking 'add' method of group.
    group.add (socket);
    Assert.strictEqual (socket.group, group);
    Assert.ok (socket.get_on_read ());
    Assert.ok (socket.read_closure);
    Assert.ok (socket.get_on_write ());
    Assert.ok (socket.write_closure);

    // socket can be removed from a group by invoking 'remove' method
    // of group.
    group.remove (socket);
    Assert.strictEqual (socket.group, null);
    Assert.strictEqual (socket.get_on_read (), null);
    Assert.strictEqual (socket.read_closure, null);
    Assert.strictEqual (socket.get_on_write (), null);
    Assert.strictEqual (socket.write_closure, null);

    // a socket can be added to a group assigning the 'group' property
    // to the group.
    socket.group = group;
    Assert.strictEqual (socket.group, group);
    Assert.ok (socket.get_on_read ());
    Assert.ok (socket.read_closure);
    Assert.ok (socket.get_on_write ());
    Assert.ok (socket.write_closure);

    // a socket can be removed from a group assigning 'group' property
    // to null.
    socket.group = null;
    Assert.strictEqual (socket.group, null);
    Assert.strictEqual (socket.get_on_read (), null);
    Assert.strictEqual (socket.read_closure, null);
    Assert.strictEqual (socket.get_on_write (), null);
    Assert.strictEqual (socket.write_closure, null);

    // setting the read/write handler of a socket while in a group,
    // removes the socket from the group.
    socket.group = group;
    // @TODO: using 'set_read_handler' here makes JS object leak. Why?
    // using 'set_on_read' instead.
    // socket.set_read_handler (function () {}, null);
    socket.set_on_read (function () {});
    Assert.strictEqual (socket.group, null);
    socket.set_read_handler (null, null);

    socket.group = group;
    socket.set_on_write (function () {});
    Assert.strictEqual (socket.group, null);
    socket.set_write_handler (null, null);
}

function testDataTransfer (Assert) {
    const CLIENT_SOCKETS = 120;
    const DATA_SIZE = 1024;
    const BLOCK_SIZE = 512;
    const PORT = 6666;

    /* generate random data */
    let data = "";
    for (let i=0; i<DATA_SIZE; i++)
        data += String.fromCharCode (32 + Math.random () * 96);

    let active_sockets = 0;

    let group = new Evd.SocketGroup ();

    let test_timeout = function () {
        Assert.fail ("Test timeout");

        MainLoop.quit ("test");
    };

    let do_write = function () {
        let size = BLOCK_SIZE;
        if (DATA_SIZE - this.data_written < size)
            size = DATA_SIZE - this.data_written;

        let len = this.write_len (data.substr (this.data_written, size), size);

        if (len > 0) {
            this.data_written += len;

            if (this.data_written < DATA_SIZE)
                return true;
        }

        return false;
    };

    group.set_on_write (function (group, socket) {
        Assert.ok (group instanceof Evd.SocketGroup);
        Assert.ok (socket instanceof Evd.Socket);
        Assert.ok (socket.can_write ());

        MainLoop.idle_add (Lang.bind (socket, do_write));
    });

    let do_read = function () {
        if (this.status != Evd.SocketState.CONNECTED)
            return false;

        let [_data, len] = this.read (BLOCK_SIZE);

        if (len > 0) {
            this.data_read += len;
            this.data += _data;

            if (this.data_read == DATA_SIZE) {
                Assert.strictEqual (this.data, data);

                this.close ();
            }
            else
                if (len == BLOCK_SIZE)
                    return true;
        }

        return false;
    };

    group.set_on_read (function (group, socket) {
        Assert.ok (group instanceof Evd.SocketGroup);
        Assert.ok (socket instanceof Evd.Socket);
        Assert.ok (socket.can_read ());

        MainLoop.idle_add (Lang.bind (socket, do_read));
    });

    let on_close = function (socket) {
        active_sockets--;
        if (active_sockets == 0)
            MainLoop.quit ("test");
    };

    let on_error = function (socket, code, msg) {
        Assert.fail ("Socket error " + code + " :" + msg);
    };

    let setup_socket = function (socket) {
        socket.connect ("close", on_close);
        socket.connect ("error", on_error);

        socket.data_read = 0;
        socket.data_written = 0;
        socket.group = group;
        socket.data = "";

        active_sockets++;
    };

    let server = new Evd.Socket ();

    server.group = group;
    server.listen ("0.0.0.0:" + PORT);

    server.connect ("state-changed",
        function (socket, new_state, old_state) {
            if (new_state == Evd.SocketState.LISTENING) {
                /* client sockets */
                for (let i=0; i<CLIENT_SOCKETS; i++) {
                    let socket = new Evd.Socket ();

                    setup_socket (socket);

                    socket.connect_to ("0.0.0.0:" + PORT);
                }
            }
        });

    server.connect ("new-connection",
        function (listener, socket) {
            Assert.strictEqual (listener.group, group);
            Assert.strictEqual (socket.group, group);
            setup_socket (socket);
        });

    MainLoop.timeout_add (3000, test_timeout);

    MainLoop.run ("test");
}

Test.run (this);
