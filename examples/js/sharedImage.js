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

const Evd = imports.gi.Evd;
const SharedImageServer = imports.sharedImageServer.SharedImageServer;

const PORT = 8080;

// Evd daemon */
var daemon = new Evd.Daemon.get_default (null, null);

/* shared image server */
var sis = new SharedImageServer ({rotate: true});

sis.setPeerOnUpdate (
    function (peer, updateObj) {
        var cmd = ["update", updateObj];
        var msg = JSON.stringify (cmd);
        peer.transport.send_text (peer, msg);
    }, null);


/* web transport */
var transport = new Evd.WebTransportServer ();

function peerOnReceive (t, peer) {
    var data = peer.transport.receive_text (peer);

    var cmd = JSON.parse (data, true);
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
    var index = sis.acquireViewport (peer);
    print ("New peer " + peer.id + " acquired viewport " + index);
}

function peerOnClose (transport, peer) {
    var index = sis.releaseViewport (peer);
    print ("Release viewport " +  index + " by peer " + peer.id);
}

if (transport["signal"]) {
    transport.signal.receive.connect (peerOnReceive);
    transport.signal.new_peer.connect (peerOnOpen);
    transport.signal.peer_closed.connect (peerOnClose);
}
else {
    transport.connect ("receive", peerOnReceive);
    transport.connect ("new-peer", peerOnOpen);
    transport.connect ("peer-closed", peerOnClose);
}

/* web dir */
var webDir = new Evd.WebDir ({ root: "../common" });

/* web selector */
var selector = new Evd.WebSelector ();

selector.set_default_service (webDir);
transport.selector = selector;

selector.listen ("0.0.0.0:" + PORT, null,
    function (service, result) {
        if (service.listen_finish (result))
            print ("Listening, now point your browser to http://localhost:" + PORT + "/shared_image.html");
    }, null);

/* start the show */
daemon.run ();
