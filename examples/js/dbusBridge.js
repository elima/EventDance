/*
 * dbusBridge.js
 *
 * EventDance examples
 *
 * Copyright (C) 2011, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

imports.searchPath.unshift ("../common");

const MainLoop = imports.mainloop;
const Gio = imports.gi.Gio;
const Evd = imports.gi.Evd;

const LISTEN_PORT = 8080;
const DBUS_ADDR = "alias:abstract=/org/eventdance/lib/examples/dbus-bridge";

// Session bus addr
let sessionBusAddr = Gio.dbus_address_get_for_bus_sync(Gio.BusType.SESSION, null);

// Web transport
let transport = new Evd.WebTransport ();

transport.connect ("new-peer",
    function onNewPeer (transport, peer) {
        // This is to send a virtual DBus address to the peer instead of the real DBus daemon
        // address, for consistency and security reasons.
        Evd.dbus_agent_create_address_alias (peer, sessionBusAddr, DBUS_ADDR);
    });

// DBus bridge
let dbusBridge = new Evd.DBusBridge ();
dbusBridge.add_transport (transport);

// Web dir
let webDir = new Evd.WebDir ({ root: "../common" });

// Web selector
let selector = new Evd.WebSelector ();
selector.set_default_service (webDir);
transport.set_selector (selector);

// start listening
selector.listen_async ("0.0.0.0:" + LISTEN_PORT, null,
    function (service, result) {
        try {
            service.listen_finish (result);
            print ("Listening on port " + LISTEN_PORT + ", now point your browser to any of the DBus example web pages");
        }
        catch (e) {
            print ("Error: " + e);
            MainLoop.quit ("main");
        }
    }, null);

// start the show
MainLoop.run ("main");
