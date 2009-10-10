const GLib = imports.gi.GLib;
const Gio = imports.gi.Gio;
const MainLoop = imports.mainloop;
const Evd = imports.gi.Evd;
const Lang = imports.lang;

let socket1, socket2, socket3;

let greeting = "Hello world!";
let sockets_closed = 0;
let bytes_read = 0;

function terminate () {
  MainLoop.quit ("main");
}

function read_handler (socket) {
  let [data, len] = socket.read (1024);

  log (len + " bytes read from socket: " + data);

  bytes_read += len;

  if (bytes_read == greeting.length * 2)
    {
      MainLoop.idle_add (terminate);
      //      socket1.close ();
      //      socket2.close ();
    }
}

function read_handler_group (group, socket) {
  let [data, len] = socket.read (1024);

  log (len + " bytes read from socket: " + data);

  bytes_read += len;

  if (bytes_read == greeting.length * 2)
    {
      MainLoop.idle_add (terminate);
    }
}

function on_socket_closed (socket) {
  log ("socket closed!");

  sockets_closed ++;

  socket.set_read_closure (NULL);

  /*
  if (sockets_closed == 2)
    MainLoop.idle_add (terminate);
  */
}

/* Socket group =================================== */

let group = new Evd.SocketGroup ();

group.set_read_closure (read_handler_group);

/* ============ socket1 =================== */

socket1 = new Evd.InetSocket ({"family": Gio.SocketFamily.IPV4});

socket1.connect ('close', on_socket_closed);

socket1.connect ('new-connection', function (socket, client) {
    client.connect ('close', on_socket_closed);
    client.group = group;

    log ("new client connected from address " +
	 client.socket.remote_address.address.to_string () +
	 " and port " + client.socket.remote_address.port);

    client.send (greeting, greeting.length);

    socket3 = client;
});

socket1.connect ('bind', function (socket, address) {
    log ("socket bound to " + address.address.to_string () + ":" +
	 address.get_port ());
  });

socket1.connect ('listen', function (socket) {
    log ("socket listening");
});

socket1.listen ("*", 6666);


/* ============ socket2 =================== */

socket2 = new Evd.InetSocket ({family: Gio.SocketFamily.IPV4});

socket2.connect_timeout = 3;

socket2.connect ('close', on_socket_closed);
socket2.group = group;

socket2.connect ('error', function (socket, code, message) {
    log ("ERROR on socket: " + code + "('" + message + "')");
  });

socket2.connect ('connect-timeout', function (socket) {
      log ("connection timeout");
    });

socket2.connect ('connect', function (socket) {
    log ("client socket connected");

    socket.send (greeting, greeting.length);
  });

socket2.connect_to ("localhost", 6666);

/*
MainLoop.timeout_add (1000, function () {
    MainLoop.quit ("main");
    return false;
  });
*/

MainLoop.run ("main");

