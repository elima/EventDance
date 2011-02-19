#
# dbusBridge.js
#
# EventDance examples
#
# Copyright (C) 2011, Igalia S.L.
#
# Authors:
#   Eduardo Lima Mitev <elima@igalia.com>
#

import gobject
from gi.repository import Gio
from gi.repository import Evd

LISTEN_PORT = '8080'
DBUS_ADDR = 'alias:abstract=/org/eventdance/lib/examples/dbus-bridge'

# Session bus address
session_bus_addr = Gio.dbus_address_get_for_bus_sync(Gio.BusType.SESSION, None)

# Web transport
transport = Evd.WebTransport()

def on_new_peer(transport, peer, main_loop):
    # This is to send a virtual DBus address to the peer instead of the real DBus daemon
    # address, for consistency and security reasons.
    Evd.dbus_agent_create_address_alias (peer, session_bus_addr, DBUS_ADDR);

transport.connect('new-peer', on_new_peer, None)

# DBus bridge
dbus_bridge = Evd.DBusBridge()
dbus_bridge.add_transport(transport)

# Web dir
web_dir = Evd.WebDir()
web_dir.set_root ('../common')

# Main loop
main_loop = gobject.MainLoop();

# Web selector
selector = Evd.WebSelector()
selector.set_default_service(web_dir)
transport.set_selector(selector);

def on_listen(self, result, main_loop):
    try:
        self.listen_finish(result)
        print ('Listening on port ' + LISTEN_PORT + ', now point your browser to any of the DBus example web pages')
    except Exception as e:
        print('Error: ' + str(e))
        main_loop.quit()

# start listening
selector.listen_async('0.0.0.0:' + LISTEN_PORT, None, on_listen, main_loop)

# start the show
main_loop.run();
