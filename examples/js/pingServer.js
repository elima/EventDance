/*
 * pingServer.js
 *
 * EventDance examples
 *
 * Copyright (C) 2010-2012, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

imports.searchPath.unshift ("../common");

const Evd = imports.gi.Evd;

const LISTEN_PORT = 8080;

Evd.tls_init ();

/* daemon */
var daemon = new Evd.Daemon.get_default (null, null);
daemon.set_pid_file ("/tmp/ping.pid");

/* web transport */
var transport = new Evd.WebTransportServer ();

function peerOnReceive (t, peer) {
    var data = peer.transport.receive_text (peer);

    transport.send_text (peer, data);
}

if (transport["signal"])
    transport.signal.receive.connect (peerOnReceive);
else
    transport.connect ("receive", peerOnReceive);

/* web dir */
var webDir = new Evd.WebDir ({ root: "../common" });

/* web selector */
var selector = new Evd.WebSelector ();

selector.set_default_service (webDir);
transport.selector = selector;

//selector.tls_autostart = true;
selector.tls_credentials.add_certificate_from_file ("../../tests/certs/x509-server.pem",
                                                    "../../tests/certs/x509-server-key.pem",
                                                    null,
                                                    null,
                                                    null);

/* start listening */
selector.listen ("0.0.0.0:" + LISTEN_PORT, null,
    function (service, result) {
        try {
            service.listen_finish (result);
            print ("Listening, now point your browser to http://localhost:" + LISTEN_PORT + "/ping.html");
        }
        catch (e) {
            if (e["message"])
                print (e.message);
            else
                print (e);

            daemon.quit (-1);
        }
    }, null);

/* start the show */
daemon.run ();

Evd.tls_deinit ();
