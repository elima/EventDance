/*
 * evd-long-polling.c
 *
 * EventDance project - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 3 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 */

#include <string.h>
#include <stdio.h>
#include <libsoup/soup-headers.h>

#include "evd-long-polling.h"
#include "evd-service-protected.h"
#include "evd-http-connection.h"

G_DEFINE_TYPE (EvdLongPolling, evd_long_polling, EVD_TYPE_TRANSPORT)

#define EVD_LONG_POLLING_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                           EVD_TYPE_LONG_POLLING, \
                                           EvdLongPollingPrivate))

#define PEER_ID_COOKIE_NAME "X-evd-peer-id"

#define PEER_DATA_KEY       "org.eventdance.transports.long-polling"
#define CONN_PEER_KEY       PEER_DATA_KEY

/* private data */
struct _EvdLongPollingPrivate
{
  GHashTable *listeners;
};

typedef struct _EvdLongPollingPeerData EvdLongPollingPeerData;
struct _EvdLongPollingPeerData
{
  GQueue *conns;
  gboolean send_cookies;
};

/* signals */
enum
{
  SIGNAL_LAST
};

//static guint evd_long_polling_signals[SIGNAL_LAST] = { 0 };

static void     evd_long_polling_class_init         (EvdLongPollingClass *class);
static void     evd_long_polling_init               (EvdLongPolling *self);

static void     evd_long_polling_finalize           (GObject *obj);
static void     evd_long_polling_dispose            (GObject *obj);

static gssize   evd_long_polling_send               (EvdTransport  *self,
                                                     EvdPeer       *peer,
                                                     const gchar   *buffer,
                                                     gsize          size,
                                                     GError       **error);

static void
evd_long_polling_class_init (EvdLongPollingClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdServiceClass *service_class = EVD_SERVICE_CLASS (class);
  EvdTransportClass *transport_class = EVD_TRANSPORT_CLASS (class);

  obj_class->dispose = evd_long_polling_dispose;
  obj_class->finalize = evd_long_polling_finalize;

  service_class->new_connection = evd_long_polling_new_connection_protected;
  service_class->tls_started = evd_long_polling_tls_started_protected;
  service_class->connection_closed = evd_long_polling_connection_closed_protected;

  transport_class->send = evd_long_polling_send;

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdLongPollingPrivate));
}

static void
evd_long_polling_init (EvdLongPolling *self)
{
  EvdLongPollingPrivate *priv;

  priv = EVD_LONG_POLLING_GET_PRIVATE (self);
  self->priv = priv;

  evd_service_set_io_stream_type (EVD_SERVICE (self), EVD_TYPE_HTTP_CONNECTION);
}

static void
evd_long_polling_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_long_polling_parent_class)->dispose (obj);
}

static void
evd_long_polling_finalize (GObject *obj)
{
  //  EvdLongPolling *self = EVD_LONG_POLLING (obj);

  G_OBJECT_CLASS (evd_long_polling_parent_class)->finalize (obj);
}

static void
evd_long_polling_free_peer_data (gpointer  _data,
                                 GObject  *where_the_obj_was)
{
  EvdLongPollingPeerData *data = (EvdLongPollingPeerData *) _data;

  while (g_queue_get_length (data->conns) > 0)
    {
      EvdHttpConnection *conn;

      conn = EVD_HTTP_CONNECTION (g_queue_pop_head (data->conns));

      /* @TODO: close conn with HTTP response */

      g_object_unref (conn);
    }
  g_queue_free (data->conns);

  g_free (data);
}

static EvdPeer *
evd_long_polling_setup_new_peer (EvdLongPolling *self)
{
  EvdPeer *peer;
  EvdLongPollingPeerData *data;

  peer = evd_transport_get_new_peer_protected (EVD_TRANSPORT (self));

  data = g_new0 (EvdLongPollingPeerData, 1);
  data->conns = g_queue_new ();

  g_object_set_data (G_OBJECT (peer), PEER_DATA_KEY, data);
  g_object_weak_ref (G_OBJECT (peer), evd_long_polling_free_peer_data, data);

  return peer;
}

static void
evd_long_polling_respond_conn (EvdLongPolling    *self,
                               EvdHttpConnection *conn,
                               EvdPeer           *peer,
                               SoupHTTPVersion    ver,
                               guint              status_code,
                               gchar             *reason_phrase,
                               gboolean           send_cookies,
                               gboolean           keep_alive)
{
  GError *error = NULL;
  SoupMessageHeaders *headers;

  headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);

  if (peer != NULL && send_cookies)
    {
      const gchar *id;
      gchar *st;

      id = evd_peer_get_id (peer);
      st = g_strdup_printf ("%s = %s", PEER_ID_COOKIE_NAME, id);
      soup_message_headers_append (headers, "Set-Cookie", st);
      g_free (st);
    }

  if (keep_alive)
    soup_message_headers_replace (headers, "Connection", "keep-alive");
  else
    soup_message_headers_replace (headers, "Connection", "close");

  if (! evd_http_connection_write_response_headers (conn,
                                                    ver,
                                                    status_code,
                                                    reason_phrase,
                                                    headers,
                                                    &error))
    {
      /* @TODO: handle error */
      g_debug ("error sending HTTP response in EvdLongPolling: %s", error->message);

      g_error_free (error);

      g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
    }
  else
    {
      if (! keep_alive)
        evd_socket_shutdown (evd_connection_get_socket (EVD_CONNECTION (conn)),
                             TRUE,
                             TRUE,
                             NULL);
    }

  soup_message_headers_free (headers);
}

gchar *
evd_long_polling_resolve_peer_id_from_headers (SoupMessageHeaders *headers)
{
  gchar *peer_id = NULL;
  const gchar *cookies;
  gchar *start;

  cookies = soup_message_headers_get_list (headers, "Cookie");
  if (cookies == NULL)
    return NULL;

  start = g_strrstr (cookies, PEER_ID_COOKIE_NAME);
  if (start != NULL)
    {
      gsize size;

      size = ((guintptr) cookies) + strlen (cookies) -
        ((guintptr) start) + strlen (PEER_ID_COOKIE_NAME) - 1;

      peer_id = g_new (gchar, size + 1);
      peer_id[size] = '\0';

      g_memmove (peer_id, (gchar *) ((guintptr) start + strlen (PEER_ID_COOKIE_NAME) + 1), size);
    }

  return peer_id;
}

static void
evd_long_polling_conn_on_headers_read (GObject      *obj,
                                       GAsyncResult *res,
                                       gpointer      user_data)
{
  EvdLongPolling *self = EVD_LONG_POLLING (user_data);
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (obj);

  SoupMessageHeaders *headers;
  SoupHTTPVersion ver;
  gchar *method;
  gchar *path;
  GError *error = NULL;
  gboolean send_cookies = FALSE;

  if ( (headers =
        evd_http_connection_read_request_headers_finish (conn,
                                                         res,
                                                         &ver,
                                                         &method,
                                                         &path,
                                                         &error)) != NULL)
    {
      gchar *peer_id = NULL;
      EvdPeer *peer;

      /* resolve peer object */
      peer_id = evd_long_polling_resolve_peer_id_from_headers (headers);
      if (peer_id == NULL ||
          (peer = evd_transport_lookup_peer (EVD_TRANSPORT (self), peer_id)) == NULL)
        {
          peer = evd_long_polling_setup_new_peer (self);
          send_cookies = TRUE;
        }

      if (g_strcmp0 (method, "GET") == 0)
        {
          if (send_cookies)
            {
              evd_long_polling_respond_conn (self,
                                             conn,
                                             peer,
                                             ver,
                                             200,
                                             "OK",
                                             TRUE,
                                             FALSE);
            }
          else
            {
              EvdLongPollingPeerData *data;

              data = (EvdLongPollingPeerData *) g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);

              g_object_set_data (G_OBJECT (conn), CONN_PEER_KEY, peer);
              g_queue_push_tail (data->conns, conn);

              g_object_ref (conn);

              g_debug ("caching conn for peer %s", evd_peer_get_id (peer));
              /* @TODO: if there is data in the backlog, send it all now */
            }
        }
      else if (g_strcmp0 (method, "POST") == 0)
        {
          /* @TODO */
        }
      else
        {
          /* @TODO: respond with error 405 (Method Not Allowed) */
        }

      g_free (peer_id);
      soup_message_headers_free (headers);
      g_free (method);
      g_free (path);
    }
  else
    {
      /* @TODO: handle error */
      g_debug ("error reading request headers: %s", error->message);

      g_error_free (error);
    }
}

static void
evd_long_polling_read_headers (EvdLongPolling     *self,
                               EvdHttpConnection  *conn)
{
  evd_http_connection_read_request_headers_async (conn,
                                          NULL,
                                          evd_long_polling_conn_on_headers_read,
                                          self);
}

static gssize
evd_long_polling_send (EvdTransport  *self,
                       EvdPeer       *peer,
                       const gchar   *buffer,
                       gsize          size,
                       GError       **error)
{
  EvdLongPollingPeerData *data;

  data = (EvdLongPollingPeerData *) g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);
  g_return_val_if_fail (data != NULL, -1);

  /* @TODO: if backlog is not empty, append data to backlog */

  if (g_queue_get_length (data->conns) > 0)
    {
      EvdHttpConnection *conn;
      SoupMessageHeaders *headers;
      GError *error = NULL;
      gchar *_data;
      gsize _size;

      conn = EVD_HTTP_CONNECTION (g_queue_pop_head (data->conns));

      _data = g_strdup_printf ("document.write (\"%s\");", buffer);
      _size = strlen (_data);

      headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
      soup_message_headers_replace (headers, "Content-type", "text/javascript");
      soup_message_headers_replace (headers, "Connection", "close");

      if (evd_http_connection_write_response_headers (conn,
                                                      SOUP_HTTP_1_1,
                                                      SOUP_STATUS_OK,
                                                      "OK",
                                                      headers,
                                                      &error))
        {
          GOutputStream *stream;

          stream = g_io_stream_get_output_stream (G_IO_STREAM (conn));
          if (! g_output_stream_write (stream, _data, _size, NULL, &error))
            {
              /* @TODO: handle error */
            }

          evd_socket_shutdown (evd_connection_get_socket (EVD_CONNECTION (conn)),
                               TRUE,
                               TRUE,
                               NULL);
        }
      else
        {
          /* @TODO: handle error */
        }

      g_free (_data);
    }
  else
    {
      /* @TODO: add data to backlog */
    }

  return size;
}

/* protected methods */

gboolean
evd_long_polling_new_connection_protected (EvdService     *service,
                                           EvdConnection  *conn)
{
  EvdLongPolling *self = EVD_LONG_POLLING (service);

  g_return_val_if_fail (EVD_IS_LONG_POLLING (self), FALSE);
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (conn), FALSE);

  EVD_SERVICE_CLASS (evd_long_polling_parent_class)->new_connection (service,
                                                                     conn);

  if (! evd_service_get_tls_autostart (service))
    evd_long_polling_read_headers (self, EVD_HTTP_CONNECTION (conn));

  return TRUE;
}

gboolean
evd_long_polling_tls_started_protected (EvdService     *service,
                                        EvdConnection  *conn)
{
  EvdLongPolling *self = EVD_LONG_POLLING (service);

  g_return_val_if_fail (EVD_IS_LONG_POLLING (self), FALSE);
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (conn), FALSE);

  evd_long_polling_read_headers (self, EVD_HTTP_CONNECTION (conn));

  return TRUE;
}

gboolean
evd_long_polling_connection_closed_protected (EvdService    *service,
                                              EvdConnection *conn)
{
  EvdLongPolling *self = EVD_LONG_POLLING (service);
  EvdPeer *peer;

  g_return_val_if_fail (EVD_IS_LONG_POLLING (self), FALSE);
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (conn), FALSE);

  /* remove conn from Peer's list of conns */
  peer = g_object_get_data (G_OBJECT (conn), CONN_PEER_KEY);
  if (peer != NULL)
    {
      EvdLongPollingPeerData *data;

      data = (EvdLongPollingPeerData *) g_object_get_data (G_OBJECT (peer), PEER_DATA_KEY);
      if (data != NULL)
        {
          g_queue_remove (data->conns, conn);
          g_object_unref (conn);
        }
    }

  g_debug ("http conn closed! (%X) (ref count: %d", (guintptr) conn, G_OBJECT (conn)->ref_count);

  return TRUE;
}

/* public methods */

EvdLongPolling *
evd_long_polling_new (void)
{
  EvdLongPolling *self;

  self = g_object_new (EVD_TYPE_LONG_POLLING, NULL);

  return self;
}
