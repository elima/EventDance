/*
 * dbus-bridge.c
 *
 * EventDance examples
 *
 * Copyright (C) 2010-2013 Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

#include <evd.h>
#include "evd-dbus-agent.h"

#define LISTEN_PORT 8080
#define DBUS_ADDR "alias:abstract=/org/eventdance/lib/examples/dbus-bridge"

static GMainLoop *main_loop;

static void
on_listen (GObject      *service,
           GAsyncResult *result,
           gpointer      user_data)
{
  GError *error = NULL;

  if (evd_service_listen_finish (EVD_SERVICE (service), result, &error))
    {
      g_debug ("Listening on port %d, now point your browser to any of the DBus example web pages",
               LISTEN_PORT);
    }
  else
    {
      g_debug ("Error: %s", error->message);
      g_error_free (error);
      g_main_loop_quit (main_loop);
    }
}

static void
transport_on_new_peer (EvdTransport *transport,
                       EvdPeer      *peer,
                       gpointer      user_data)
{
  /* This is to send a virtual DBus address to the peer instead of the real DBus daemon
     address, for consistency and security reasons. */
  evd_dbus_agent_create_address_alias (G_OBJECT (peer),
                                       (gchar *) user_data,
                                       DBUS_ADDR);
}

gint
main (gint argc, gchar *argv[])
{
  EvdDBusBridge *dbus_bridge;
  const gchar *session_bus_addr;
  EvdWebTransportServer *transport;
  EvdWebDir *web_dir;
  EvdWebSelector *selector;
  gchar *addr;

#ifndef GLIB_VERSION_2_36
  g_type_init ();
#endif

  /* Session bus address */
  session_bus_addr = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION,
                                                      NULL,
                                                      NULL);

  /* web transport */
  transport = evd_web_transport_server_new (NULL);
  g_signal_connect (transport,
                    "new-peer",
                    G_CALLBACK (transport_on_new_peer),
                    (gpointer) session_bus_addr);

  /* DBus bridge */
  dbus_bridge = evd_dbus_bridge_new ();
  evd_ipc_mechanism_use_transport (EVD_IPC_MECHANISM (dbus_bridge),
                                   EVD_TRANSPORT (transport));

  /* web dir */
  web_dir = evd_web_dir_new ();
  evd_web_dir_set_root (web_dir, EXAMPLES_COMMON_DIR);

  /* web selector */
  selector = evd_web_selector_new ();
  evd_web_selector_set_default_service (selector, EVD_SERVICE (web_dir));
  evd_web_transport_server_use_selector (transport, selector);

  /* start listening */
  addr = g_strdup_printf ("0.0.0.0:%d", LISTEN_PORT);
  evd_service_listen (EVD_SERVICE (selector),
                      addr,
                      NULL,
                      on_listen,
                      NULL);
  g_free (addr);

  /* start the show */
  main_loop = g_main_loop_new (NULL, FALSE);

  g_main_loop_run (main_loop);

  /* free stuff */
  g_main_loop_unref (main_loop);
  g_object_unref (selector);
  g_object_unref (web_dir);
  g_object_unref (dbus_bridge);
  g_object_unref (transport);

  return 0;
}
