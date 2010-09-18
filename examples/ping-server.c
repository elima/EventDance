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

static GMainLoop *main_loop;

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

      g_main_loop_quit (main_loop);
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
  EvdWebTransport *transport;
  EvdWebDir *web_dir;
  EvdWebSelector *selector;
  EvdTlsCredentials *cred;
  gchar *addr;

  g_type_init ();
  evd_tls_init (NULL);

  /* web transport */
  transport = evd_web_transport_new ();

  g_signal_connect (transport,
                    "receive",
                    G_CALLBACK (transport_on_receive),
                    NULL);

  /* web dir */
  web_dir = evd_web_dir_new ("./common");

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

  g_object_unref (transport);
  g_object_unref (selector);
  g_object_unref (web_dir);

  evd_tls_deinit ();

  return 0;
}
