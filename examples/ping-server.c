/*
 * ping-server.c
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

static EvdDaemon *evd_daemon;

static void
on_listen (GObject      *service,
           GAsyncResult *result,
           gpointer      user_data)
{
  GError *error = NULL;

  if (evd_service_listen_finish (EVD_SERVICE (service), result, &error))
    {
      gchar *url;

      url = g_strdup_printf ("http://localhost:%d/ping.html", LISTEN_PORT);
      g_debug ("Listening, now point your browser to %s", url);
      g_free (url);
    }
  else
    {
      g_debug ("%s", error->message);
      g_error_free (error);

      evd_daemon_quit (evd_daemon, -1);
    }
}

static void
transport_on_receive (EvdTransport *transport,
                      EvdPeer      *peer,
                      gpointer      user_data)
{
  const gchar *buf;

  buf = evd_transport_receive_text (transport, peer);

  evd_transport_send_text (transport, peer, buf, NULL);
}

gint
main (gint argc, gchar *argv[])
{
  EvdWebTransportServer *transport;
  EvdWebDir *web_dir;
  EvdWebSelector *selector;
  EvdTlsCredentials *cred;
  gchar *addr;

  g_type_init ();
  evd_tls_init (NULL);

  /* daemon */
  evd_daemon = evd_daemon_get_default (&argc, &argv);

  /* web transport */
  transport = evd_web_transport_server_new (NULL);
  g_signal_connect (transport,
                    "receive",
                    G_CALLBACK (transport_on_receive),
                    NULL);

  /* web dir */
  web_dir = evd_web_dir_new ();
  evd_web_dir_set_root (web_dir, EXAMPLES_COMMON_DIR);

  /* web selector */
  selector = evd_web_selector_new ();

  evd_web_selector_set_default_service (selector, EVD_SERVICE (web_dir));
  evd_web_transport_server_set_selector (transport, selector);

  /*  evd_service_set_tls_autostart (EVD_SERVICE (selector), TRUE); */
  cred = evd_service_get_tls_credentials (EVD_SERVICE (selector));
  evd_tls_credentials_add_certificate_from_file (cred,
                                          "../tests/certs/x509-server.pem",
                                          "../tests/certs/x509-server-key.pem",
                                          NULL,
                                          NULL,
                                          NULL);

  /* start listening */
  addr = g_strdup_printf ("0.0.0.0:%d", LISTEN_PORT);
  evd_service_listen (EVD_SERVICE (selector),
                      addr,
                      NULL,
                      on_listen,
                      NULL);
  g_free (addr);

  /* start the show */
  evd_daemon_run (evd_daemon, NULL);

  /* free stuff */
  g_object_unref (transport);
  g_object_unref (selector);
  g_object_unref (web_dir);
  g_object_unref (evd_daemon);

  evd_tls_deinit ();

  return 0;
}
