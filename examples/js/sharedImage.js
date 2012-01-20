/*
 * sharedImage.js
 *
 * EventDance examples
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

imports.searchPath.unshift (".");
imports.searchPath.unshift ("../common");

const MainLoop = imports.mainloop;
const Lang = imports.lang;
const Evd = imports.gi.Evd;
const SharedImageServer = imports.sharedImageServer.SharedImageServer;

const PORT = 8080;

Evd.tls_init ();

/* shared image server */
let sis = new SharedImageServer ({rotate: true});

sis.setPeerOnUpdate (
    function (peer, updateObj) {
        let cmd = ["update", updateObj];
        let msg = JSON.stringify (cmd);
        peer.transport.send_text (peer, msg);
    }, null);


/* web transport */
let transport = new Evd.WebTransportServer ();

function peerOnReceive (t, peer) {
    let data = peer.transport.receive_text (peer);

    let cmd = JSON.parse (data, true);
    if (cmd === null)
        return;

    switch (cmd[0]) {
    case "req-update":
        sis.updatePeer (peer, sis.UPDATE_ALL);
        break;

    case "grab":
        sis.grabImage (peer, cmd[1]);
        break;

    case "ungrab":
        sis.ungrabImage (peer);
        break;

    case "move":
        sis.moveImage (peer, cmd[1]);
        break;
    }
}

function peerOnOpen (transport, peer) {
    let index = sis.acquireViewport (peer);
    print ("New peer " + peer.id + " acquired viewport " + index);

/*
    let msg = '["set-index", '+index+']';
    peer.transport.send_text (peer, msg);
*/
}

function peerOnClose (transport, peer) {
    let index = sis.releaseViewport (peer);
    print ("Release viewport " +  index + " by peer " + peer.id);
}

transport.connect ("receive", peerOnReceive);
transport.connect ("new-peer", peerOnOpen);
transport.connect ("peer-closed", peerOnClose);

/* web dir */
let webDir = new Evd.WebDir ({ root: "../common" });

/* web selector */
let selector = new Evd.WebSelector ();

//selector.tls_autostart = true;
//selector.tls_credentials.dh_bits = 1024;
selector.tls_credentials.cert_file = "../../tests/certs/x509-server.pem";
selector.tls_credentials.key_file = "../../tests/certs/x509-server-key.pem";

selector.set_default_service (webDir);

transport.selector = selector;

selector.listen ("0.0.0.0:" + PORT, null,
    function (service, result) {
        if (service.listen_finish (result))
            print ("Listening, now point your browser to http://localhost:" + PORT + "/shared_image.html");
    }, null);

/* start the show */
MainLoop.run ("main");

Evd.tls_deinit ();
