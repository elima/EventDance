/*
 * echoServerTls.js
 *
 * EventDance project - An event distribution framework <http://eventdance.org>
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 */

// A simple http GET client, supporting TLS upgrade
// Use it passing any domain name as first argument

const MainLoop = imports.mainloop;
const Evd = imports.gi.Evd;
const Lang = imports.lang;

const BLOCK_SIZE  = 2048;

let uri = ARGV[0];
let schema, resource, domain;

// initialize TLS
Evd.tls_init ();

// create socket
let socket = new Evd.Socket ();

socket.connect ("close",
    function (socket) {
        log ("socket closed");
        MainLoop.quit ("main");
    });

socket.connect ("error",
    function (socket, code, msg) {
        log ("socket error: " + msg);
        socket.close ();
    });

socket.connect ("state-changed",
    function (socket, newState, oldState) {
        if (newState == Evd.SocketState.CONNECTED) {
            let addr = socket.get_remote_address ();

            if (schema == "https" && ! socket.tls_active) {
                // do TLS upgrade
                socket.tls.credentials.cert_file = "../../tests/certs/x509-server.pem";
                socket.tls.credentials.key_file = "../../tests/certs/x509-server-key.pem";
                socket.starttls (Evd.TlsMode.CLIENT);
            }
            else {
                // perform request
                socket.write ("GET "+resource+" HTTP/1.1\r\n"+
                              "Host: "+domain+"\r\n"+
                              "Connection: close\r\n\r\n");
            }
        }
    });

function read_block (socket) {
    let [data, len] = socket.input_stream.read_str (BLOCK_SIZE);
    if (len > 0) {
        print (data);
    }
    return len;
}

function read_another_block () {
    if (this.status == Evd.SocketState.CONNECTED)
        if (read_block (this) == BLOCK_SIZE)
            return true;

    return false;
}

socket.set_on_read (
    function (socket) {
        if (read_block (socket) == BLOCK_SIZE)
            MainLoop.idle_add (Lang.bind (socket, read_another_block));
    });

if (! uri) {
    log ("Usage: simpleHttpGet.js <url>");
}
else {
    let uriTokens = uri.match ("(?:([^:/?#]+):)?(?://([^/?#:]*))(:([^/?#]*))?([^?#]*)(?:\\?([^#]*))?(?:#(.*))?");

    try {
        schema = uriTokens[1];
        domain = uriTokens[2];
        let port = uriTokens[4];

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

        // connect to server
        socket.connect_to (domain + ":" + port);

        // start the main event loop
        MainLoop.run ("main");
    }
    catch (e) {
        log ("error parsing url");
    }
}

// deinitialize TLS
Evd.tls_deinit ();
