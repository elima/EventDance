/*
 * dbusBridge.js
 *
 * EventDance examples
 *
 * Copyright (C) 2011-2012, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

imports.searchPath.unshift ("../common");

const Gio = imports.gi.Gio;
const Evd = imports.gi.Evd;

const LISTEN_PORT = 8080;
const DBUS_ADDR = "alias:abstract=/org/eventdance/lib/examples/dbus-bridge";

// Evd daemon
var daemon = new Evd.Daemon.get_default (null, null);

// Session bus addr
var sessionBusAddr = Gio.dbus_address_get_for_bus_sync(Gio.BusType.SESSION, null);

// Web transport
var transport = new Evd.WebTransportServer ();

function onNewPeer (transport, peer) {
    // This is to send a virtual DBus address to the peer instead of the real DBus daemon
    // address, for consistency and security reasons.
    Evd.dbus_agent_create_address_alias (peer, sessionBusAddr, DBUS_ADDR);
}

if (transport["signal"])
    transport.signal.new_peer.connect (onNewPeer);
else
    transport.connect ("new-peer", onNewPeer);

// DBus bridge
var dbusBridge = new Evd.DBusBridge ();
dbusBridge.add_transport (transport);

// Web dir
var webDir = new Evd.WebDir ({ root: "../common" });

// Web selector
var selector = new Evd.WebSelector ();
selector.set_default_service (webDir);
transport.set_selector (selector);

// start listening
selector.listen ("0.0.0.0:" + LISTEN_PORT, null,
    function (service, result) {
        try {
            service.listen_finish (result);
            print ("Listening on port " + LISTEN_PORT + ", now point your browser to any of the DBus example web pages");
        }
        catch (e) {
            print ("Error: " + e);
            daemon.quit (-1);
        }
    }, null);

// start the show
daemon.run ();
