/*
 * pingServer.js
 *
 * EventDance examples
 *
 * Copyright (C) 2010, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

imports.searchPath.unshift ("../common");

const MainLoop = imports.mainloop;
const Evd = imports.gi.Evd;

const LISTEN_PORT = 8080;

Evd.tls_init ();

/* web transport */
let transport = new Evd.WebTransport ();

function peerOnReceive (t, peer) {
    let data = peer.transport.receive_text (peer);

    transport.send_text (peer, data);
}

transport.connect ("receive", peerOnReceive);

/* web dir */
let webDir = new Evd.WebDir ({ root: "../common" });

/* web selector */
let selector = new Evd.WebSelector ();

selector.set_default_service (webDir);
transport.selector = selector;

//selector.tls_autostart = true;
selector.tls_credentials.cert_file = "../../tests/certs/x509-server.pem";
selector.tls_credentials.key_file = "../../tests/certs/x509-server-key.pem";

/* start listening */
selector.listen_async ("0.0.0.0:" + LISTEN_PORT, null,
    function (service, result) {
        try {
            service.listen_finish (result);
            print ("Listening, now point your browser to http://localhost:" + LISTEN_PORT + "/ping.html");
        }
        catch (e) {
            print (e);
            MainLoop.quit ("main");
        }
    }, null);

  /* start the show */
MainLoop.run ("main");

Evd.tls_deinit ();
