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
socket.tls.credentials.dh_bits = 1024;
socket.tls.credentials.cert_file = "../../tests/certs/x509-server.pem";
socket.tls.credentials.key_file = "../../tests/certs/x509-server-key.pem";
socket.tls.credentials.trust_file = "../../tests/certs/x509-ca.pem";
socket.tls.require_peer_cert = true;

// hook-up new-connection event
socket.connect ("new-connection",
    function (listener, client) {
        log ("Client connected");

        client.connect ("close",
            function (socket) {
                log ("Client closed connection");
            });

        client.connect ("error",
            function (socket, code, msg) {
                log ("Socket error: " + msg);
            });

        client.connect ("state-changed",
            function (socket, newState, oldState) {
                if (newState == Evd.SocketState.CONNECTED &&
                    socket.tls_active) {
                    log ("Handshake completed");

                    let certificates = socket.tls.get_peer_certificates ();
                    log ("Peer sent " + certificates.length + " certificate(s):");
                    for (let i=0; i<certificates.length; i++) {
                        let cert = certificates[i];
                        log ("  * Certificate (" + i + "):");
                        log ("     - Subject: " + cert.get_dn ());
                        log ("     - Activation time: " + cert.get_activation_time ());
                        log ("     - Expiration time: " + cert.get_expiration_time ());
                    }

                    let result = socket.tls.verify_peer (0);
                    if (result == Evd.TlsVerifyState.OK)
                        log ("Peer verification: OK");
                    else {
                        log ("Peer verification: INSECURE");
                        if (result & Evd.TlsVerifyState.NO_CERT)
                            log ("  - Peer did not send any certificate");
                        if (result & Evd.TlsVerifyState.SIGNER_NOT_FOUND)
                            log ("  - The certificate's issuer is not known");
                        if (result & Evd.TlsVerifyState.INVALID)
                            log ("  - One of the peer certificates is not signed by any of the known CAs, or its signature is invalid");
                        if (result & Evd.TlsVerifyState.REVOKED)
                            log ("  - One of the peer certificates has been revoked by its CA");
                        if (result & Evd.TlsVerifyState.SIGNER_NOT_CA)
                            log ("  - The signer of one of the peer certificates is not a CA");
                        if (result & Evd.TlsVerifyState.INSECURE_ALG)
                            log ("  - One of the peer certificates was signed using an insecure algorithm such as MD2 or MD5");
                        if (result & Evd.TlsVerifyState.EXPIRED)
                            log ("  - One of the peer certificates has expired");
                        if (result & Evd.TlsVerifyState.NOT_ACTIVE)
                            log ("  - One of the peer certificates is not active yet");
                    }
                }
            });

        client.set_on_read (
            function (socket) {
                let [data, len] = socket.input_stream.read_str (BLOCK_SIZE);
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
