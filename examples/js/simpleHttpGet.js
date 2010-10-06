/*
 * simpleHttpGet.js
 *
 * EventDance project - An event distribution framework <http://eventdance.org>
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 */

// A simple http GET client, supporting TLS upgrade
// Use it passing any http URL as first argument
// (e.g 'gjs-console simpleHttpGet.js http://foo.bar').

const MainLoop = imports.mainloop;
const Evd = imports.gi.Evd;
const Lang = imports.lang;

const BLOCK_SIZE  = 2048;

let uri = ARGV[0];
let schema, resource, domain, port;

function performRequest (req) {
    // initialize TLS
    Evd.tls_init ();

    // create socket
    let socket = new Evd.Socket ();

    socket.connect ("error",
        function (socket, domain, code, msg) {
            log ("socket error: " + msg);
        });

    function receiveResponse (inputStream, result) {
        try {
            let [data, len] = inputStream.read_str_finish (result);
            if (data)
                print (data);
        }
        catch (e) {
            log (e);
            return;
        }

        if (this.is_connected ()) {
            inputStream.read_str_async (BLOCK_SIZE,
                                        0,
                                        null,
                                        Lang.bind (this, receiveResponse),
                                        0);
        }
    }

    // connect to server
    socket.connect_async (domain + ":" + port, null,
        function (socket, result, userData) {
            let conn;
            try {
                socket.connect_finish (result);
                conn = new Evd.HttpConnection ({socket: socket});
            }
            catch (e) {
                log (e);
                MainLoop.quit ("main");
                return;
            }

            log ("socket connected");

            conn.input_throttle.bandwidth = 0.0;
            conn.input_throttle.latency = 0.0;

            conn.output_throttle.bandwidth = 0.0;
            conn.output_throttle.latency = 0.0;

            conn.connect ("close",
                function (socket) {
                    log ("connection closed");
                    MainLoop.quit ("main");
                });

            if (schema == "https") {
                conn.tls.credentials.cert_file = "../../tests/certs/x509-server.pem";
                conn.tls.credentials.key_file = "../../tests/certs/x509-server-key.pem";
                conn.starttls_async (Evd.TlsMode.CLIENT, null,
                    function (conn, result, userData) {
                        try {
                            conn.starttls_finish (result);
                            log ("TLS started");
                        }
                        catch (e) {
                            log (e);
                        }
                    }, null, null);
            }

            conn.output_stream.write_str_async (req, 0, null, null, null);

            conn.read_response_headers_async (null,
                function (conn, result) {
                    try {
                        let [headers, ver, code, reason]
                            = conn.read_response_headers_finish (result);
                        log ("response: " + code + " " + reason);
                        if (code == 200)
                            conn.input_stream.read_str_async (BLOCK_SIZE,
                                                              0,
                                                              null,
                                                              Lang.bind (conn, receiveResponse),
                                                              null);
                        else
                            conn.close (null);

                    }
                    catch (e) {
                        log (e);
                    }
                });

        }, null, null);

    // start the main event loop
    MainLoop.run ("main");

    // deinitialize TLS
    Evd.tls_deinit ();
}

if (! uri) {
    log ("Usage: simpleHttpGet.js <url>");
}
else {
    let uriTokens = uri.match ("(?:([^:/?#]+):)?(?://([^/?#:]*))(:([^/?#]*))?([^?#]*)(?:\\?([^#]*))?(?:#(.*))?");

    try {
        schema = uriTokens[1];
        domain = uriTokens[2];
        port = uriTokens[4];

        resource = uriTokens[5];
        if (uriTokens[6])
            resource += "?" + uriTokens[6];
        if (! resource)
            resource = "/";

        if (! schema)
            schema = "http";

        if (! port)
            if (schema == "https")
                port = 443;
            else
                port = 80;

        let req = "GET "+resource+" HTTP/1.1\r\n"+
            "Host: "+domain+"\r\n"+
            "Connection: close\r\n\r\n";

        performRequest (req);
    }
    catch (e) {
        log ("error parsing url: " + e);
    }
}
