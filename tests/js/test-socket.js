const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const MainLoop = imports.mainloop;
const Evd = imports.gi.Evd;
const Lang = imports.lang;
const GObject = imports.gi.GObject;

let socket = new Evd.Socket ({
    family: Gio.SocketFamily.IPV4,
    type: Gio.SocketType.STREAM,
    protocol: Gio.SocketProtocol.TCP
});

socket.connect ('listen', function (socket) {
        log ("socket listening");
});

function read_handler (socket, user_data) {
  log ("data read from socket!" + socket);
}

socket.connect ('close', function (socket) {
    log ("socket closed!");
    MainLoop.quit ("main");
});

socket.connect ('new-connection', function (socket, client) {
    log ("new connection!");

    client.set_read_closure (Lang.bind (client, window.read_handler));
    //    client.read_handler = read_handler;
});

let addr = new Gio.InetSocketAddress ({
    "address": Gio.InetAddress.new_any (Gio.SocketFamily.IPV4),
    "port": 6666,
});

socket.bind (addr, true);
socket.listen ();

MainLoop.run ("main");
