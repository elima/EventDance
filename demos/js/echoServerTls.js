/*
 * echoServerTls.js
 *
 * EventDance project - An event distribution framework <http://eventdance.org>
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 */

// A simple echo server over TLS.
// Use any TLS echo client (e.g 'gnutls-cli') to test this.

const MainLoop = imports.mainloop;
const Evd = imports.gi.Evd;

const LISTEN_PORT = 5556;
const BLOCK_SIZE  = 8192;

// initialize TLS
Evd.tls_init ();

// create socket
let socket = new Evd.Socket ();

// setup TLS on socket
socket.tls_autostart = true;
socket.tls.credentials.cert_file = "../../tests/certs/x509-server.pem";
socket.tls.credentials.key_file = "../../tests/certs/x509-server-key.pem";
socket.tls.credentials.trust_file = "../../tests/certs/x509-ca.pem";
socket.tls.require_peer_cert = true;

// hook-up new-connection event
socket.connect ("new-connection",
    function (listener, client) {
        log ("client connected");

        client.connect ("close",
            function (socket) {
                log ("client closed connection");
            });

        client.connect ("error",
            function (socket, code, msg) {
                log ("socket error: " + msg);
            });

        client.set_on_read (
            function (socket) {
                let [data, len] = socket.read (BLOCK_SIZE);
                if (len > 0)
                    socket.write (data);
            });
    });

// listen on all IPv4 interfaces
socket.listen ("0.0.0.0:" + LISTEN_PORT);

// start the main event loop
MainLoop.run ("main");

// deinitialize TLS
Evd.tls_deinit ();
