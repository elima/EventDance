/*
 * dbus-bridge.c
 *
 * EventDance examples
 *
 * Copyright (C) 2010, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

#include <evd.h>

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
      g_debug ("%s", error->message);
      g_error_free (error);

      g_main_loop_quit (main_loop);
    }
}

static void
transport_on_new_peer (EvdTransport *transport,
                       EvdPeer      *peer,
                       gpointer      user_data)
{
  evd_dbus_agent_create_address_alias (G_OBJECT (peer),
                                       (gchar *) user_data,
                                       DBUS_ADDR);
}

gint
main (gint argc, gchar *argv[])
{
  EvdDBusBridge *dbus_bridge;
  const gchar *session_bus_addr;
  EvdWebTransport *transport;
  EvdWebDir *web_dir;
  EvdWebSelector *selector;
  EvdTlsCredentials *cred;
  gchar *addr;

  g_type_init ();
  evd_tls_init (NULL);

  session_bus_addr = g_dbus_address_get_for_bus_sync (G_BUS_TYPE_SESSION,
                                                      NULL,
                                                      NULL);

  /* web transport */
  transport = evd_web_transport_new ();
  g_signal_connect (transport,
                    "new-peer",
                    G_CALLBACK (transport_on_new_peer),
                    (gpointer) session_bus_addr);

  /* DBus bridge */
  dbus_bridge = evd_dbus_bridge_new ();

  evd_dbus_bridge_add_transport (dbus_bridge, EVD_TRANSPORT (transport));

  /* web dir */
  web_dir = evd_web_dir_new ();
  evd_web_dir_set_root (web_dir, "./common");

  /* web selector */
  selector = evd_web_selector_new ();

  evd_web_selector_set_default_service (selector, EVD_SERVICE (web_dir));
  evd_web_transport_set_selector (transport, selector);

  /* evd_service_set_tls_autostart (EVD_SERVICE (selector), TRUE); */
  cred = evd_service_get_tls_credentials (EVD_SERVICE (selector));
  g_object_set (cred,
                "cert-file", "../tests/certs/x509-server.pem",
                "key-file", "../tests/certs/x509-server-key.pem",
                NULL);

  /* start listening */
  addr = g_strdup_printf ("0.0.0.0:%d", LISTEN_PORT);
  evd_service_listen_async (EVD_SERVICE (selector),
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

  g_object_unref (dbus_bridge);
  g_object_unref (transport);
  g_object_unref (selector);
  g_object_unref (web_dir);

  evd_tls_deinit ();

  return 0;
}
