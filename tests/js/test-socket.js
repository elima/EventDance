const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const MainLoop = imports.mainloop;
const Evd = imports.gi.Evd;
const Lang = imports.lang;
const GObject = imports.gi.GObject;


function read_handler () {
  let [data, len] = this.read (1024);
  let real_data = unescape (data);

  log (len + " bytes read from socket: " + data);
}

function on_socket_closed (socket) {
  log ("socket closed!");
}

let socket = new Evd.InetSocket ();

socket.connect ('listen', function (socket) {
    log ("socket listening");
});

function read_handler () {
  let [data, len] = this.read (1024);
  let real_data = unescape (data);

  log (len + " bytes read from socket: " + data);
}

socket.connect ('close', on_socket_closed);

socket.connect ('new-connection', function (socket, client) {
    log ("new client connected from address " +
	 client.socket.remote_address.address.to_string () +
	 " and port " + client.socket.remote_address.port);

    client.set_read_closure (Lang.bind (client, window.read_handler));
    //    client.read_handler = read_handler;

    let data = "pepe" + String.fromCharCode (0) + "kaka";
    data = escape (data);
    client.send (data, data.length);
});

socket.connect ('bind', function (socket, address) {
    log ("socket bound to " + address.address.to_string () + ":" +
	 address.get_port ());
    socket.listen ();
  });

socket.bind ("*", 6666, true);


/* ============ client socket =================== */

let client1 = new Evd.InetSocket ({family: Gio.SocketFamily.IPV4});

client1.connect_timeout = 3;

client1.connect ('connect-timeout', Lang.bind (client1, function (socket) {
      log ("connection timeout");
    }));

client1.connect ('connect', function (socket) {
    log ("client socket connected");

    socket.set_read_closure (Lang.bind (socket, read_handler));

    let data = "hey dude, what's up?";

    socket.send (data, data.length);
    });

client1.connect ('close', function (socket) {
    log ("client socket closed");
  });

client1.connect_to ("localhost", 6666);

MainLoop.timeout_add (3500, Lang.bind (client1, function () {
      //      this.cancel_connect ();
    }));

MainLoop.run ("main");
