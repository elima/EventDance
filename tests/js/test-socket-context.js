const Gio = imports.gi.Gio;
const MainLoop = imports.mainloop;
const Evd = imports.gi.Evd;
const Lang = imports.lang;

const CLIENT_SOCKETS = 10000;
const DATA_SIZE = 100;
const BLOCK_SIZE = 100;

const BANDWIDTH_MIN = 50;
const BANDWIDTH_RANGE = 0;

let total_data_read = 0;
let active_sockets = 0;
let connections = 0;
let last_second_read = 0;
let last_second = 0;

/* generate random data */
let data = "";
for (let i=0; i<DATA_SIZE; i++)
  data += String.fromCharCode (32 + Math.random () * 96);

function terminate () {
  MainLoop.quit ("main");
}

function check_bandwidth (len) {
  let second = new Date().getTime () / 1000;

  if (second != last_second) {
    //    log ("bandwidth in: " + last_second_read + "\n");
    last_second = second;
    last_second_read = len;
  }
  else {
    last_second_read += len;
  }
}

function read_data () {
  if ( (this.get_status () == Evd.SocketState.CONNECTED) &&
       ( (this.data_read < DATA_SIZE) || (this.data_written < DATA_SIZE) ) ) {

    let [data, len] = this.read (BLOCK_SIZE+1);

    if (len > 0) {
      total_data_read += len;
      this.data_read += len;
      //      log ("Read " + this.data_read + "/" + DATA_SIZE + ", waiting " + wait + " for more");
      check_bandwidth (len);
    }

    if (this.data_read == DATA_SIZE) {
      if (this.data_written == DATA_SIZE) {
	//	this.close ();
	active_sockets--;
	log ("active sockets: " + active_sockets);
      }
    }

    if (total_data_read == DATA_SIZE * CLIENT_SOCKETS * 2)
      {
	window.server.disconnect (sig_id);
	window.server.close ();
	window.server = null;

	MainLoop.timeout_add (1, terminate);
      }

    this.bandwidth_in = Math.random () * BANDWIDTH_RANGE + BANDWIDTH_MIN;
  }

  return false;
}

function client_write () {
  let size = BLOCK_SIZE;
  if (DATA_SIZE - this.data_written < size)
    size = DATA_SIZE - this.data_written;

  let [len] = this.write_len (this.data, size);

  if (len > 0) {
    this.data_written += len;

    if (! this.auto_write)
      if (this.data_written < DATA_SIZE)
	MainLoop.idle_add (Lang.bind (this, client_write));
  }

  return false;
}

function client_on_read (socket) {
  Lang.bind (socket, read_data) ();
}

/* server socket */
window.server = new Evd.InetSocket ({"family": Gio.SocketFamily.IPV4});

let sig_id = server.connect ('new-connection', function (socket, client) {
    client.set_on_read (client_on_read);
    client.bandwidth_in = Math.random () * BANDWIDTH_RANGE + BANDWIDTH_MIN;
    client.data = data;
    client.data_read = 0;
    client.data_written = 0;

    active_sockets += 2;
    connections++;
    //    log ("new-connection: " + connections);
    client.auto_write = true;
    Lang.bind (client, client_write) ();
});

server.connect ('error', function (socket, error) {
    log ("ERROR: in server socket: " + error.message);
  });

server.listen ("*", 6666);


/* client sockets */
for (let i=0; i<CLIENT_SOCKETS; i++) {
    let socket = new Evd.InetSocket ({"family": Gio.SocketFamily.IPV4});

    socket.connect ('state-changed', function (self, new_state, old_state) {
        if (new_state == Evd.SocketState.CONNECTED) {
            //	log ("client connected!");
            self.auto_write = true;
            Lang.bind (self, client_write) ();
        }
    });

    socket.data_read = 0;
    socket.data_written = 0;
    socket.data = data;
    socket.set_on_read (client_on_read);
    socket.bandwidth_in = Math.random () * BANDWIDTH_RANGE + BANDWIDTH_MIN;
    socket.connect_to ("127.0.0.1", 6666);
}

MainLoop.run ("main");
