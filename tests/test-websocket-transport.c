/*
 * test-websocket.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2012-2013, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 */

#include <evd.h>

#define LISTEN_ADDR "0.0.0.0:%d"
#define WS_ADDR     "ws://127.0.0.1:%d/"

static gint listen_port = 54321;

typedef struct
{
  gchar *test_name;
  gchar *msg;
  gssize msg_len;
  EvdMessageType msg_type;
} TestCase;

typedef struct
{
  EvdWebsocketClient *ws_client;
  EvdWebsocketServer *ws_server;
  const TestCase *test_case;

  GMainLoop *main_loop;
  gboolean client_new_peer;
} Fixture;

static const TestCase test_cases[] =
  {
    {
      "/text-message",
      "Hello World!",
      -1,
      EVD_MESSAGE_TYPE_TEXT
    },

    {
      "/binary-message",
      "Hello\0World!\0",
      13,
      EVD_MESSAGE_TYPE_BINARY
    }
  };

static void
fixture_setup (Fixture       *f,
               gconstpointer  data)
{
  f->ws_client = evd_websocket_client_new ();
  f->ws_server = evd_websocket_server_new ();

  f->main_loop = g_main_loop_new (NULL, FALSE);

  f->client_new_peer = FALSE;
}

static void
fixture_teardown (Fixture       *f,
                  gconstpointer  data)
{
  g_object_unref (f->ws_client);
  g_object_unref (f->ws_server);

  g_main_loop_unref (f->main_loop);
}

static void
test_new (Fixture       *f,
          gconstpointer  data)
{
  g_assert (EVD_IS_WEBSOCKET_CLIENT (f->ws_client));
  g_assert (EVD_IS_TRANSPORT (f->ws_client));

  g_assert (EVD_IS_WEBSOCKET_SERVER (f->ws_server));
  g_assert (EVD_IS_WEB_SERVICE (f->ws_server));
  g_assert (EVD_IS_TRANSPORT (f->ws_server));
}

static gboolean
quit_main_loop (gpointer user_data)
{
  Fixture *f = user_data;

  g_main_loop_quit (f->main_loop);

  return FALSE;
}

static void
on_client_open (GObject      *obj,
                GAsyncResult *res,
                gpointer      user_data)
{
  GError *error = NULL;
  gboolean ok;

  ok = evd_transport_open_finish (EVD_TRANSPORT (obj), res, &error);
  g_assert_no_error (error);
  g_assert (ok);
}

static void
on_server_open (GObject *obj,
                GAsyncResult *res,
                gpointer      user_data)
{
  Fixture *f = user_data;
  GError *error = NULL;
  gboolean ok;
  gchar *addr;

  ok = evd_transport_open_finish (EVD_TRANSPORT (obj), res, &error);
  g_assert_no_error (error);
  g_assert (ok);

  /* open client transport */
  addr = g_strdup_printf (WS_ADDR, listen_port);
  listen_port++;

  evd_transport_open (EVD_TRANSPORT (f->ws_client),
                      addr,
                      NULL,
                      on_client_open,
                      f);
  g_free (addr);
}

static void
on_new_peer (EvdTransport *transport,
             EvdPeer      *peer,
             gpointer      user_data)
{
  Fixture *f = user_data;
  GError *error = NULL;
  gboolean ok;

  g_assert (EVD_IS_TRANSPORT (transport));

  g_assert (EVD_IS_PEER (peer));
  g_assert (! evd_peer_is_closed (peer));

  if (EVD_IS_WEBSOCKET_SERVER (transport))
    {
      if (f->test_case->msg_type == EVD_MESSAGE_TYPE_TEXT)
        {
          ok = evd_transport_send_text (transport,
                                        peer,
                                        f->test_case->msg,
                                        &error);
        }
      else
        {
          ok = evd_transport_send (transport,
                                   peer,
                                   f->test_case->msg,
                                   f->test_case->msg_len,
                                   &error);
        }

      g_assert_no_error (error);
      g_assert (ok);
    }
  else
    {
      f->client_new_peer = TRUE;
    }
}

static void
on_receive (EvdTransport *transport,
            EvdPeer      *peer,
            gpointer      user_data)
{
  Fixture *f = user_data;
  const gchar *msg;
  gsize msg_len;
  gboolean ok;
  GError *error = NULL;

  g_assert (EVD_IS_TRANSPORT (transport));

  g_assert (EVD_IS_PEER (peer));
  g_assert (! evd_peer_is_closed (peer));

  if (f->test_case->msg_type == EVD_MESSAGE_TYPE_TEXT)
    {
      msg = evd_transport_receive_text (transport, peer);
      g_assert_cmpstr (msg, ==, f->test_case->msg);

      if (EVD_IS_WEBSOCKET_CLIENT (transport))
        {
          ok = evd_peer_send_text (peer, msg, &error);
          g_assert_no_error (error);
          g_assert (ok);
        }
    }
  else
    {
      msg = evd_transport_receive (transport, peer, &msg_len);
      g_assert_cmpuint (msg_len, ==, f->test_case->msg_len);

      if (EVD_IS_WEBSOCKET_CLIENT (transport))
        {
          ok = evd_peer_send (peer, msg, msg_len, &error);
          g_assert_no_error (error);
          g_assert (ok);
        }
    }

  if (EVD_IS_WEBSOCKET_CLIENT (transport))
    {
      evd_transport_close_peer (transport, peer, TRUE, &error);
      g_assert_no_error (error);
    }
}

static void
on_peer_closed (EvdTransport *transport,
                EvdPeer      *peer,
                gboolean      gracefully,
                gpointer      user_data)
{
  Fixture *f = user_data;

  g_assert (EVD_IS_TRANSPORT (transport));

  g_assert (EVD_IS_PEER (peer));
  g_assert (evd_peer_is_closed (peer));
  g_assert (gracefully);

  g_assert (f->client_new_peer);

  if (EVD_IS_WEBSOCKET_SERVER (transport))
    g_timeout_add (1, quit_main_loop, f);
}

static void
test_func (Fixture       *f,
           gconstpointer  data)
{
  gchar *addr;

  f->test_case = (const TestCase *) data;

  g_signal_connect (f->ws_server,
                    "new-peer",
                    G_CALLBACK (on_new_peer),
                    f);
  g_signal_connect (f->ws_client,
                    "new-peer",
                    G_CALLBACK (on_new_peer),
                    f);

  g_signal_connect (f->ws_client,
                    "receive",
                    G_CALLBACK (on_receive),
                    f);
  g_signal_connect (f->ws_server,
                    "receive",
                    G_CALLBACK (on_receive),
                    f);

  g_signal_connect (f->ws_server,
                    "peer-closed",
                    G_CALLBACK (on_peer_closed),
                    f);
  g_signal_connect (f->ws_client,
                    "peer-closed",
                    G_CALLBACK (on_peer_closed),
                    f);

  evd_websocket_server_set_standalone (f->ws_server, TRUE);

  /* open server transport */
  addr = g_strdup_printf (LISTEN_ADDR, listen_port);

  evd_transport_open (EVD_TRANSPORT (f->ws_server),
                      addr,
                      NULL,
                      on_server_open,
                      f);
  g_free (addr);

  g_main_loop_run (f->main_loop);
}

gint
main (gint argc, gchar *argv[])
{
  gint exit_code;
  gint i;

  g_test_init (&argc, &argv, NULL);
  evd_tls_init (NULL);

  g_test_add ("/evd/websocket/transport/basic",
              Fixture,
              NULL,
              fixture_setup,
              test_new,
              fixture_teardown);

  for (i=0; i<sizeof (test_cases) / sizeof (TestCase); i++)
    {
      gchar *test_name;

      test_name = g_strdup_printf ("/evd/websocket/transport/%s",
                                   test_cases[i].test_name);

      g_test_add (test_name,
                  Fixture,
                  &test_cases[i],
                  fixture_setup,
                  test_func,
                  fixture_teardown);

      g_free (test_name);
    }

  exit_code = g_test_run ();

  evd_tls_deinit ();

  return exit_code;
}
