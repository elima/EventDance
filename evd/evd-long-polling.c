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
#include "evd-transport.h"

#include "evd-http-connection.h"
#include "evd-peer-manager.h"

#define EVD_LONG_POLLING_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                           EVD_TYPE_LONG_POLLING, \
                                           EvdLongPollingPrivate))

#define PEER_ID_COOKIE_NAME "X-evd-peer-id"

#define PEER_DATA_KEY       "org.eventdance.lib.transports.long-polling"
#define CONN_PEER_KEY_GET   PEER_DATA_KEY ".get"
#define CONN_PEER_KEY_POST  PEER_DATA_KEY ".post"

/* private data */
struct _EvdLongPollingPrivate
{
  EvdPeerManager *peer_manager;
};

typedef struct _EvdLongPollingPeerData EvdLongPollingPeerData;
struct _EvdLongPollingPeerData
{
  GQueue *conns;
  gboolean send_cookies;
};

static void     evd_long_polling_class_init          (EvdLongPollingClass *class);
static void     evd_long_polling_init                (EvdLongPolling *self);

static void     evd_long_polling_transport_iface_init (EvdTransportInterface *iface);

static void     evd_long_polling_finalize            (GObject *obj);
static void     evd_long_polling_dispose             (GObject *obj);

static void     evd_long_polling_headers_read        (EvdWebService      *web_service,
                                                      EvdHttpConnection  *conn,
                                                      SoupHTTPVersion     ver,
                                                      gchar              *method,
                                                      gchar              *path,
                                                      SoupMessageHeaders *headers);

static gboolean evd_long_polling_remove              (EvdIoStreamGroup *io_stream_group,
                                                      GIOStream        *io_stream);

static gssize   evd_long_polling_send                (EvdTransport *transport,
                                                      EvdPeer       *peer,
                                                      const gchar   *buffer,
                                                      gsize          size,
                                                      GError       **error);

static gboolean evd_long_polling_actual_send         (EvdLongPolling     *self,
                                                      EvdPeer            *peer,
                                                      EvdHttpConnection *conn,
                                                      const gchar        *buffer,
                                                      gsize               size,
                                                      GError            **error);

static gboolean evd_long_polling_peer_is_connected   (EvdTransport *transport,
                                                      EvdPeer       *peer);

G_DEFINE_TYPE_WITH_CODE (EvdLongPolling, evd_long_polling, EVD_TYPE_WEB_SERVICE,
                         G_IMPLEMENT_INTERFACE (EVD_TYPE_TRANSPORT,
                                                evd_long_polling_transport_iface_init));

static void
evd_long_polling_class_init (EvdLongPollingClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdIoStreamGroupClass *io_stream_group_class =
    EVD_IO_STREAM_GROUP_CLASS (class);
  EvdWebServiceClass *web_service_class = EVD_WEB_SERVICE_CLASS (class);

  obj_class->dispose = evd_long_polling_dispose;
  obj_class->finalize = evd_long_polling_finalize;

  io_stream_group_class->remove = evd_long_polling_remove;

  web_service_class->headers_read = evd_long_polling_headers_read;

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdLongPollingPrivate));
}

static void
evd_long_polling_transport_iface_init (EvdTransportInterface *iface)
{
  iface->send = evd_long_polling_send;
  iface->peer_is_connected = evd_long_polling_peer_is_connected;
}

static void
evd_long_polling_init (EvdLongPolling *self)
{
  EvdLongPollingPrivate *priv;

  priv = EVD_LONG_POLLING_GET_PRIVATE (self);
  self->priv = priv;

  priv->peer_manager = evd_peer_manager_get_default ();

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
  EvdLongPolling *self = EVD_LONG_POLLING (obj);

  g_object_unref (self->priv->peer_manager);

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

      /* @TODO: close conn with HTTP response? */

      g_object_set_data (G_OBJECT (conn), CONN_PEER_KEY_GET, NULL);

      g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
      g_object_unref (conn);
    }
  g_queue_free (data->conns);

  g_free (data);
}

static EvdPeer *
evd_long_polling_create_new_peer (EvdLongPolling *self)
{
  EvdPeer *peer;
  EvdLongPollingPeerData *data;

  peer = evd_peer_manager_create_new_peer (self->priv->peer_manager,
                                           EVD_TRANSPORT (self));

  data = g_new0 (EvdLongPollingPeerData, 1);
  data->conns = g_queue_new ();

  g_object_set_data (G_OBJECT (peer), PEER_DATA_KEY, data);
  g_object_weak_ref (G_OBJECT (peer), evd_long_polling_free_peer_data, data);

  return peer;
}

static void
evd_long_polling_connection_shutdown_on_flush (GObject      *obj,
                                               GAsyncResult *res,
                                               gpointer      user_data)
{
  EvdConnection *conn = EVD_CONNECTION (user_data);

  g_output_stream_flush_finish (G_OUTPUT_STREAM (obj), res, NULL);

  evd_socket_shutdown (evd_connection_get_socket (conn),
                       TRUE,
                       TRUE,
                       NULL);

  g_object_unref (conn);
}

static void
evd_long_polling_respond_with_cookies (EvdLongPolling    *self,
                                       EvdHttpConnection *conn,
                                       EvdPeer           *peer,
                                       SoupHTTPVersion    ver)
{
  SoupMessageHeaders *headers;
  GError *error = NULL;
  const gchar *id;

  headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);

  id = evd_peer_get_id (peer);
  soup_message_headers_replace (headers, "X-Org-Eventdance-Peer-Id", id);

  if (! evd_http_connection_respond (conn,
                                     ver,
                                     200,
                                     "OK",
                                     headers,
                                     NULL,
                                     0,
                                     TRUE,
                                     NULL,
                                     &error))
    {
      g_debug ("error responding with cookies: %s", error->message);
      g_error_free (error);
    }

  soup_message_headers_free (headers);
}

static void
evd_long_polling_conn_on_content_read (GObject      *obj,
                                       GAsyncResult *res,
                                       gpointer      user_data)
{
  EvdLongPolling *self = EVD_LONG_POLLING (user_data);
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (obj);

  EvdPeer *peer;
  gchar *content;
  gssize size;
  GError *error = NULL;

  peer = g_object_get_data (G_OBJECT (conn), CONN_PEER_KEY_POST);

  if ( (content = evd_http_connection_read_all_content_finish (conn,
                                                               res,
                                                               &size,
                                                               &error)) != NULL)
    {
      EVD_TRANSPORT_GET_INTERFACE (self)->
        receive (EVD_TRANSPORT (self),
                 peer,
                 content,
                 (gsize) size);
    }
  else
    {
      g_debug ("error reading content: %s", error->message);
      g_error_free (error);
    }

  evd_long_polling_actual_send (self,
                                peer,
                                conn,
                                NULL,
                                0,
                                NULL);
}

static void
evd_long_polling_headers_read (EvdWebService      *web_service,
                               EvdHttpConnection  *conn,
                               SoupHTTPVersion     ver,
                               gchar              *method,
                               gchar              *path,
                               SoupMessageHeaders *headers)
{
  EvdLongPolling *self = EVD_LONG_POLLING (web_service);
  gchar *peer_id = NULL;
  EvdPeer *peer;
  gboolean send_cookies = FALSE;

  /* resolve peer object */
  peer_id = g_strdup (soup_message_headers_get_one (headers,
                                                    "X-Org-Eventdance-Peer-Id"));
  if (peer_id == NULL ||
      (peer = evd_peer_manager_lookup_peer (self->priv->peer_manager,
                                            peer_id)) == NULL)
    {
      peer = evd_long_polling_create_new_peer (self);
      send_cookies = TRUE;
    }

  evd_peer_touch (peer);

  if (send_cookies)
    {
      evd_long_polling_respond_with_cookies (self, conn, peer, ver);
    }
  else if (g_strcmp0 (method, "GET") == 0)
    {
      EvdLongPollingPeerData *data;

      data =
        (EvdLongPollingPeerData *) g_object_get_data (G_OBJECT (peer),
                                                      PEER_DATA_KEY);

      g_object_ref (conn);
      g_object_set_data (G_OBJECT (conn), CONN_PEER_KEY_GET, peer);

      /* send Peer's backlogged frames */
      if (evd_peer_backlog_get_length (peer) > 0)
        evd_long_polling_actual_send (self,
                                      peer,
                                      conn,
                                      NULL,
                                      0,
                                      NULL);
      else
        g_queue_push_tail (data->conns, conn);
    }
  else if (g_strcmp0 (method, "POST") == 0)
    {
      g_object_set_data (G_OBJECT (conn), CONN_PEER_KEY_POST, peer);

      evd_http_connection_read_all_content_async (conn,
                                          NULL,
                                          evd_long_polling_conn_on_content_read,
                                          self);
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

static gboolean
evd_long_polling_write_frame_delivery (EvdLongPolling     *self,
                                       EvdHttpConnection  *conn,
                                       const gchar        *buf,
                                       gsize               size,
                                       GError            **error)
{
  gboolean result = TRUE;
  gchar *_data;
  gsize _size;

  _data = g_strdup_printf ("%s", buf);
  _size = strlen (_data);

  if (! evd_http_connection_write_content (conn,
                                           _data,
                                           _size,
                                           NULL,
                                           error) ||
      ! evd_http_connection_write_content (conn,
                                           "\0",
                                           1,
                                           NULL,
                                           error))
    {
      result = FALSE;
    }

  g_free (_data);

  return result;
}

static gboolean
evd_long_polling_peer_is_connected (EvdTransport *transport,
                                    EvdPeer       *peer)
{
  EvdLongPollingPeerData *data;

  data = (EvdLongPollingPeerData *) g_object_get_data (G_OBJECT (peer),
                                                       PEER_DATA_KEY);

  if (data == NULL || g_queue_get_length (data->conns) == 0)
    return FALSE;
  else
    return TRUE;
}

static gboolean
evd_long_polling_actual_send (EvdLongPolling     *self,
                              EvdPeer            *peer,
                              EvdHttpConnection  *conn,
                              const gchar        *buffer,
                              gsize               size,
                              GError            **error)
{
  SoupMessageHeaders *headers;
  gboolean result = TRUE;

  /* build and send HTTP headers */
  headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
  soup_message_headers_replace (headers, "Content-type", "text/plain; charset=utf-8");
  soup_message_headers_replace (headers, "Connection", "close");

  if (evd_http_connection_write_response_headers (conn,
                                                  SOUP_HTTP_1_1,
                                                  SOUP_STATUS_OK,
                                                  "OK",
                                                  headers,
                                                  NULL,
                                                  error))
    {
      gchar *frame;
      gsize frame_size;
      GOutputStream *stream;

      /* send frames in peer's backlog first */
      while ( result &&
              (frame = evd_peer_backlog_pop_frame (peer, &frame_size)) != NULL)
        {
          if (! evd_long_polling_write_frame_delivery (self,
                                                       conn,
                                                       frame,
                                                       frame_size,
                                                       NULL))
            {
              evd_peer_backlog_unshift_frame (peer, frame, frame_size, NULL);

              result = FALSE;
            }

          g_free (frame);
        }

      /* then send the requested frame */
      if (result && buffer != NULL &&
          ! evd_long_polling_write_frame_delivery (self,
                                                   conn,
                                                   buffer,
                                                   size,
                                                   NULL))
        {
          result = FALSE;
        }

      /* flush connection's buffer, and shutdown connection after */
      g_object_ref (conn);
      stream = g_io_stream_get_output_stream (G_IO_STREAM (conn));
      g_output_stream_flush_async (stream,
                                   evd_connection_get_priority (EVD_CONNECTION (conn)),
                                   NULL,
                                   evd_long_polling_connection_shutdown_on_flush,
                                   conn);
    }

  soup_message_headers_free (headers);

  return result;
}

static gssize
evd_long_polling_select_conn_and_send (EvdLongPolling  *self,
                                       EvdPeer         *peer,
                                       const gchar     *buffer,
                                       gsize            size,
                                       GError         **error)
{
  EvdLongPollingPeerData *data;
  EvdHttpConnection *conn;

  data = (EvdLongPollingPeerData *) g_object_get_data (G_OBJECT (peer),
                                                       PEER_DATA_KEY);
  g_return_val_if_fail (data != NULL, FALSE);

  if (g_queue_get_length (data->conns) == 0)
    return 0;

  conn = EVD_HTTP_CONNECTION (g_queue_pop_head (data->conns));

  if (evd_long_polling_actual_send (self,
                                    peer,
                                    conn,
                                    buffer,
                                    size,
                                    error))
    return size;
  else
    return -1;
}

static gssize
evd_long_polling_send (EvdTransport *transport,
                       EvdPeer       *peer,
                       const gchar   *buffer,
                       gsize          size,
                       GError       **error)
{
  EvdLongPolling *self = EVD_LONG_POLLING (transport);

  return evd_long_polling_select_conn_and_send (self,
                                                peer,
                                                buffer,
                                                size,
                                                error);
}

static gboolean
evd_long_polling_remove (EvdIoStreamGroup *io_stream_group,
                         GIOStream        *io_stream)
{
  EvdConnection *conn = EVD_CONNECTION (io_stream);
  EvdPeer *peer;

  if (! EVD_IO_STREAM_GROUP_CLASS (evd_long_polling_parent_class)->
      remove (io_stream_group, io_stream))
    {
      return FALSE;
    }

  /* remove conn from Peer's list of conns */
  peer = g_object_get_data (G_OBJECT (conn), CONN_PEER_KEY_GET);
  if (peer != NULL)
    {
      EvdLongPollingPeerData *data;

      data = (EvdLongPollingPeerData *) g_object_get_data (G_OBJECT (peer),
                                                           PEER_DATA_KEY);
      if (data != NULL)
        {
          g_queue_remove (data->conns, conn);
          g_object_unref (conn);
        }
    }

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
