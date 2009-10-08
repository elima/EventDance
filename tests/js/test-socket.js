const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const MainLoop = imports.mainloop;
const Evd = imports.gi.Evd;
const Lang = imports.lang;


function read_handler () {
  let [data, len] = this.read (1024);
  let real_data = unescape (data);

  log (len + " bytes read from socket: " + data);
}

function on_socket_closed (socket) {
  log ("socket closed!");
}


/* ============ server socket =================== */

let socket = new Evd.InetSocket ({"family": Gio.SocketFamily.IPV4});

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
  });

socket.connect ('listen', function (socket) {
    log ("socket listening");
});

socket.listen ("*", 6666);


/* ============ client socket =================== */

let client = new Evd.InetSocket ({family: Gio.SocketFamily.IPV4});

client.connect_timeout = 3;

client.connect ('error', function (socket, code, message) {
    log ("ERROR on socket: " + code + "('" + message + "')");
  });

client.connect ('connect-timeout', Lang.bind (client, function (socket) {
      log ("connection timeout");
    }));

client.connect ('connect', function (socket) {
    log ("client socket connected");

    socket.set_read_closure (Lang.bind (socket, read_handler));

    let data = "hey dude, what's up?";

    socket.send (data, data.length);
    });

client.connect ('close', on_socket_closed);

client.connect_to ("kaka", 6666);

MainLoop.timeout_add (1000, Lang.bind (socket, function () {
      MainLoop.quit ("main");
    }));

MainLoop.run ("main");
