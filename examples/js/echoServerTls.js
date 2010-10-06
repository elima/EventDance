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
const Lang = imports.lang;
const Evd = imports.gi.Evd;

const LISTEN_PORT = 5556;
const BLOCK_SIZE  = 8192;

// initialize TLS
Evd.tls_init ();

// create socket
let socket = new Evd.Socket ();

// setup TLS credentials
let cred = new Evd.TlsCredentials ();
cred.dh_bits = 1024;
cred.cert_file = "../../tests/certs/x509-server.pem";
cred.key_file = "../../tests/certs/x509-server-key.pem";
cred.trust_file = "../../tests/certs/x509-ca.pem";

// hook-up new-connection event
socket.connect ("new-connection",
    function (listener, conn) {
        log ("Client connected");

        conn.socket.connect ("close",
            function (socket) {
                log ("Client closed connection");
            });

        conn.socket.connect ("error",
            function (socket, code, msg) {
                log ("Socket error: " + msg);
            });

        conn.tls.credentials = cred;
        conn.tls.require_peer_cert = true;

        conn.starttls_async (Evd.TlsMode.SERVER, 0, null,
            function (conn, res, userData) {
                try {
                    conn.starttls_finish (res);

                    let certificates = conn.tls.get_peer_certificates ();
                    log ("Peer sent " + certificates.length + " certificate(s):");
                    for (let i=0; i<certificates.length; i++) {
                        let cert = certificates[i];
                        log ("  * Certificate (" + i + "):");
                        log ("     - Subject: " + cert.get_dn ());
                        log ("     - Activation time: " + cert.get_activation_time ());
                        log ("     - Expiration time: " + cert.get_expiration_time ());
                    }

                    let result = conn.tls.verify_peer (0);
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
                catch (e) {
                    log ("TLS upgrade failed: " + e);
                }
            }, null);

        function on_read (inputStream, result) {
            try {
                let [data, len] = inputStream.read_str_finish (result);

                this.output_stream.write_str (data, null);

                if (! inputStream.is_closed ()) {
                    inputStream.read_str_async (BLOCK_SIZE,
                                                0,
                                                null,
                                                Lang.bind (this, on_read),
                                                0);
                }
            }
            catch (e) {
                log ("Reading error: " + e);
            }
        }

        conn.input_stream.read_str_async (BLOCK_SIZE,
                                          0,
                                          null,
                                          Lang.bind (conn, on_read),
                                          null);
    });

// listen on all IPv4 interfaces
socket.listen_async ("0.0.0.0:" + LISTEN_PORT, null,
    function (socket, result, userData) {
        try {
            socket.listen_finish (result);
            log ("Socket listening");
        }
        catch (e) {
            log (e);
            MainLoop.quit ("main");
        }
    }, null);

// start the main event loop
MainLoop.run ("main");

// deinitialize TLS
Evd.tls_deinit ();
