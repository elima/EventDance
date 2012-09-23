/*
 * evd-web-service.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009/2010/2011, Igalia S.L.
 *
 * Authors:
 *   Eduardo Lima Mitev <elima@igalia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 3, or (at your option) any later version as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 */

#include "evd-web-service.h"

#include "evd-error.h"
#include "evd-marshal.h"

G_DEFINE_TYPE (EvdWebService, evd_web_service, EVD_TYPE_SERVICE)

#define EVD_WEB_SERVICE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                          EVD_TYPE_WEB_SERVICE, \
                                          EvdWebServicePrivate))

#define RETURN_DATA_KEY "org.eventdance.lib.WebService.RETURN_TO"

#define DEFAULT_ORIGIN_POLICY EVD_POLICY_DENY

#define DEFAULT_CORS_PREFLIGHT_MAX_AGE "600" /* in seconds */

typedef struct _EvdWebServicePrivate EvdWebServicePrivate;

struct _EvdWebServicePrivate
{
  GHashTable *origins;
  EvdPolicy origin_policy;
};

/* signals */
enum
{
  SIGNAL_REQUEST_HEADERS,
  SIGNAL_LOG_ENTRY,
  SIGNAL_LAST
};

static guint evd_web_service_signals[SIGNAL_LAST] = { 0 };

static void     evd_web_service_class_init                  (EvdWebServiceClass *class);
static void     evd_web_service_init                        (EvdWebService *self);

static void     evd_web_service_finalize                    (GObject *obj);

static void     evd_web_service_connection_accepted         (EvdService    *service,
                                                             EvdConnection *conn);

static void     evd_web_service_return_connection           (EvdWebService     *self,
                                                             EvdHttpConnection *conn);

static gboolean evd_web_service_respond_internal            (EvdWebService       *self,
                                                             EvdHttpConnection   *conn,
                                                             guint                status_code,
                                                             SoupMessageHeaders  *headers,
                                                             const gchar         *content,
                                                             gsize                size,
                                                             GError             **error);

static void     evd_web_service_flush_and_return_connection (EvdWebService     *self,
                                                             EvdHttpConnection *conn);

static gboolean evd_web_service_log                         (EvdWebService      *self,
                                                             EvdHttpConnection  *conn,
                                                             EvdHttpRequest     *request,
                                                             guint               status_code,
                                                             gsize               content_size,
                                                             GError            **error);

static void
evd_web_service_class_init (EvdWebServiceClass *class)
{
  EvdServiceClass *service_class = EVD_SERVICE_CLASS (class);
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_web_service_finalize;

  class->return_connection = evd_web_service_return_connection;
  class->respond = evd_web_service_respond_internal;
  class->flush_and_return_connection = evd_web_service_flush_and_return_connection;
  class->log = evd_web_service_log;

  service_class->connection_accepted = evd_web_service_connection_accepted;

  evd_web_service_signals[SIGNAL_REQUEST_HEADERS] =
    g_signal_new ("request-headers",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdWebServiceClass, signal_request_headers),
                  NULL, NULL,
                  evd_marshal_VOID__OBJECT_OBJECT,
                  G_TYPE_NONE, 2,
                  EVD_TYPE_HTTP_CONNECTION,
                  EVD_TYPE_HTTP_REQUEST);

  evd_web_service_signals[SIGNAL_LOG_ENTRY] =
    g_signal_new ("log-entry",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdWebServiceClass, signal_log_entry),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__STRING,
                  G_TYPE_NONE, 1,
                  G_TYPE_STRING);

  g_type_class_add_private (obj_class, sizeof (EvdWebServicePrivate));
}

static void
evd_web_service_init (EvdWebService *self)
{
  EvdWebServicePrivate *priv = EVD_WEB_SERVICE_GET_PRIVATE (self);

  evd_service_set_io_stream_type (EVD_SERVICE (self), EVD_TYPE_HTTP_CONNECTION);

  priv->origin_policy = DEFAULT_ORIGIN_POLICY;
  priv->origins = g_hash_table_new_full (g_str_hash,
                                         g_str_equal,
                                         g_free,
                                         g_free);
}

static void
evd_web_service_finalize (GObject *obj)
{
  EvdWebService *self = EVD_WEB_SERVICE (obj);
  EvdWebServicePrivate *priv = EVD_WEB_SERVICE_GET_PRIVATE (self);

  g_hash_table_unref (priv->origins);

  G_OBJECT_CLASS (evd_web_service_parent_class)->finalize (obj);
}

static void
evd_web_service_invoke_request_handler (EvdWebService     *self,
                                        EvdHttpConnection *conn,
                                        EvdHttpRequest    *request)
{
  EvdWebServiceClass *class;

  class = EVD_WEB_SERVICE_GET_CLASS (self);
  if (class->request_handler != NULL)
    {
      (* class->request_handler) (self, conn, request);
    }

  g_signal_emit (self,
                 evd_web_service_signals[SIGNAL_REQUEST_HEADERS],
                 0,
                 conn,
                 request,
                 NULL);
}

static void
evd_web_service_respond_cors_preflight (EvdWebService     *self,
                                        EvdHttpConnection *conn,
                                        EvdHttpRequest    *request)
{
  EvdWebServiceClass *class;

  SoupMessageHeaders *req_headers;
  SoupMessageHeaders *res_headers;

  const gchar *request_headers;
  const gchar *request_method;

  req_headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));
  res_headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);

  /* @TODO: check that the actual method and headers are allowed.
     By now just allow all */

  request_headers =
    soup_message_headers_get_one (req_headers, "Access-Control-Request-Headers");
  if (request_headers != NULL)
    soup_message_headers_replace (res_headers,
                                  "Access-Control-Allow-Headers",
                                  request_headers);

  request_method =
    soup_message_headers_get_one (req_headers, "Access-Control-Request-Method");
  if (request_method != NULL)
    soup_message_headers_replace (res_headers,
                                  "Access-Control-Allow-Methods",
                                  request_method);

  soup_message_headers_replace (res_headers,
                                "Access-Control-Max-Age",
                                DEFAULT_CORS_PREFLIGHT_MAX_AGE);

  class = EVD_WEB_SERVICE_GET_CLASS (self);
  class->respond (self,
                  conn,
                  SOUP_STATUS_OK,
                  res_headers,
                  NULL,
                  0,
                  NULL);
}

static gboolean
evd_web_service_validate_request (EvdWebService     *self,
                                  EvdHttpConnection *conn,
                                  EvdHttpRequest    *request)
{
  EvdWebServiceClass *class;
  gboolean result;

  /* check for cross-origin */
  if (evd_http_request_is_cross_origin (request))
    {
      const gchar *origin;

      origin = evd_http_request_get_origin (request);

      if (! evd_web_service_origin_allowed (self, origin))
        {
          result = FALSE;
          goto reject;
        }
      else if (evd_http_request_is_cors_preflight (request))
        {
          evd_web_service_respond_cors_preflight (self, conn, request);

          result = FALSE;
          goto out;
        }
    }

  /* @TODO: continue validation */

  result = TRUE;
  goto out;

 reject:
  class = EVD_WEB_SERVICE_GET_CLASS (self);
  class->respond (self,
                  conn,
                  SOUP_STATUS_FORBIDDEN,
                  NULL,
                  NULL,
                  0,
                  NULL);

  goto out;

 out:
  return result;
}

static void
evd_web_service_conn_on_headers_read (GObject      *obj,
                                      GAsyncResult *res,
                                      gpointer      user_data)
{
  EvdWebService *self = EVD_WEB_SERVICE (user_data);
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (obj);
  EvdHttpRequest *request;
  GError *error = NULL;

  if ( (request =
        evd_http_connection_read_request_headers_finish (conn,
                                                         res,
                                                         &error)) != NULL)
    {
      if (evd_web_service_validate_request (self, conn, request))
        evd_web_service_invoke_request_handler (self, conn, request);
    }
  else
    {
      if (! g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CLOSED))
        {
          g_debug ("error reading request headers: %s", error->message);
          g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
        }

      g_error_free (error);
    }
}

static void
evd_web_service_connection_accepted (EvdService *service, EvdConnection *conn)
{
  EvdWebService *self = EVD_WEB_SERVICE (service);
  EvdHttpRequest *request;

  request = evd_http_connection_get_current_request (EVD_HTTP_CONNECTION (conn));

  if (request != NULL)
    {
      if (evd_web_service_validate_request (self,
                                            EVD_HTTP_CONNECTION (conn),
                                            request))
        {
          evd_web_service_invoke_request_handler (self,
                                                  EVD_HTTP_CONNECTION (conn),
                                                  request);
        }
    }
  else
    {
      evd_http_connection_read_request_headers (EVD_HTTP_CONNECTION (conn),
                                           NULL,
                                           evd_web_service_conn_on_headers_read,
                                           self);
    }
}

static void
evd_web_service_on_service_destroyed (gpointer  data,
                                      GObject  *where_the_object_was)
{
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (data);

  g_object_set_data (G_OBJECT (conn), RETURN_DATA_KEY, NULL);
}

static void
evd_web_service_on_conn_destroyed (gpointer  data,
                                   GObject  *where_the_object_was)
{
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (where_the_object_was);
  EvdService *service;

  service = EVD_SERVICE (g_object_get_data (G_OBJECT (conn), RETURN_DATA_KEY));

  if (service != NULL)
    g_object_weak_unref (G_OBJECT (service),
                         evd_web_service_on_service_destroyed,
                         conn);
}

static void
evd_web_service_return_connection (EvdWebService     *self,
                                   EvdHttpConnection *conn)
{
  if (g_io_stream_is_closed (G_IO_STREAM (conn)))
    return;

  evd_http_connection_set_current_request (conn, NULL);

  if (evd_http_connection_get_keepalive (conn))
    {
      EvdService *return_to;

      return_to = EVD_SERVICE (g_object_get_data (G_OBJECT (conn),
                                                  RETURN_DATA_KEY));
      if (return_to != NULL)
        {
          g_object_set_data (G_OBJECT (conn), RETURN_DATA_KEY, NULL);

          g_object_weak_unref (G_OBJECT (conn),
                               evd_web_service_on_conn_destroyed,
                               NULL);
          g_object_weak_unref (G_OBJECT (return_to),
                               evd_web_service_on_service_destroyed,
                               conn);

          evd_io_stream_group_add (EVD_IO_STREAM_GROUP (return_to),
                                   G_IO_STREAM (conn));
        }
      else
        {
          EVD_SERVICE_GET_CLASS (self)->
            connection_accepted (EVD_SERVICE (self), EVD_CONNECTION (conn));
        }
    }
  else
    {
      evd_socket_shutdown (evd_connection_get_socket (EVD_CONNECTION (conn)),
                           TRUE,
                           TRUE,
                           NULL);
    }
}

static void
evd_web_service_connection_on_flush (GObject      *obj,
                                     GAsyncResult *res,
                                     gpointer      user_data)
{
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (user_data);
  EvdIoStreamGroup *group;

  g_output_stream_flush_finish (G_OUTPUT_STREAM (obj), res, NULL);

  group = evd_connection_get_group (EVD_CONNECTION (conn));

  if (group != NULL && EVD_IS_WEB_SERVICE (group))
    {
      EvdWebService *self;

      self = EVD_WEB_SERVICE (group);
      EVD_WEB_SERVICE_GET_CLASS (group)->return_connection (self, conn);
    }

  g_object_unref (conn);
}

static void
evd_web_service_flush_and_return_connection (EvdWebService     *self,
                                             EvdHttpConnection *conn)
{
  GOutputStream *stream;

  stream = g_io_stream_get_output_stream (G_IO_STREAM (conn));

  g_object_ref (conn);
  g_output_stream_flush_async (stream,
                            evd_connection_get_priority (EVD_CONNECTION (conn)),
                            NULL,
                            evd_web_service_connection_on_flush,
                            conn);
}

static gboolean
evd_web_service_respond_internal (EvdWebService       *self,
                                  EvdHttpConnection   *conn,
                                  guint                status_code,
                                  SoupMessageHeaders  *headers,
                                  const gchar         *content,
                                  gsize                size,
                                  GError             **error)
{
  EvdHttpRequest *request;
  SoupHTTPVersion ver;
  gboolean free_headers = FALSE;
  gboolean result;

  request = evd_http_connection_get_current_request (conn);
  if (request == NULL)
    ver = SOUP_HTTP_1_1;
  else
    ver = evd_http_message_get_version (EVD_HTTP_MESSAGE (request));

  if (evd_http_connection_get_keepalive (conn))
    {
      if (headers == NULL)
        {
          headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
          free_headers = TRUE;
        }

      soup_message_headers_replace (headers, "Connection", "keep-alive");
    }

  /* check cross origin */
  if (status_code != SOUP_STATUS_FORBIDDEN &&
      request != NULL &&
      evd_http_request_is_cross_origin (request))
    {
      const gchar *origin;

      origin = evd_http_request_get_origin (request);

      /* check if this origin is allowed */
      if (evd_web_service_origin_allowed (self, origin))
        {
          if (headers == NULL)
            {
              headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);
              free_headers = TRUE;
            }

          soup_message_headers_replace (headers,
                                        "Access-Control-Allow-Origin",
                                        origin);
        }
    }

  if (evd_http_connection_respond (conn,
                                   ver,
                                   status_code,
                                   NULL,
                                   headers,
                                   content,
                                   size,
                                   ! evd_http_connection_get_keepalive (conn),
                                   error))
    {
      EVD_WEB_SERVICE_GET_CLASS (self)->flush_and_return_connection (self, conn);

      result = TRUE;
    }
  else
    {
      evd_connection_flush_and_shutdown (EVD_CONNECTION (conn), NULL);

      result = FALSE;
    }

  if (free_headers)
    soup_message_headers_free (headers);

  return result;
}

static gchar *
evd_web_service_build_log_entry (EvdWebService      *self,
                                 const gchar        *format,
                                 EvdHttpConnection  *conn,
                                 EvdHttpRequest     *request,
                                 guint               status_code,
                                 gsize               content_size,
                                 GError            **error)
{
  gchar *entry;
  SoupMessageHeaders *headers;
  const gchar *user_agent;
  const gchar *referer;
  gchar *remote_addr;
  GDateTime *date;
  gchar *date_str;
  gchar *user;
  gchar *path;

  /* @TODO: format is currently ignored, and a standard Apache log entry format
     is used */

  g_return_val_if_fail (EVD_IS_WEB_SERVICE (self), NULL);

  if (! EVD_IS_HTTP_CONNECTION (conn))
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "Cannot build log entry, invalid HTTP connection");
      return NULL;
    }

  if (! EVD_IS_HTTP_REQUEST (request))
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "Cannot build log entry, invalid HTTP request");
      return NULL;
    }

  remote_addr =
    evd_connection_get_remote_address_as_string (EVD_CONNECTION (conn), NULL);
  if (remote_addr == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_DATA,
                   "Cannot build log entry, unable to determine remote address");
      return NULL;
    }

  headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));

  user_agent = soup_message_headers_get_one (headers, "user-agent");
  if (user_agent == NULL)
    user_agent = "-";

  referer = soup_message_headers_get_one (headers, "referer");
  if (referer == NULL)
    referer = "-";

  date = g_date_time_new_now_local ();
  date_str = g_date_time_format (date, "%d/%b/%Y:%H:%M:%S %z");
  g_date_time_unref (date);

  if (! evd_http_request_get_basic_auth_credentials (request, &user, NULL))
    user = g_strdup ("-");

  path = evd_http_request_get_path (request);

  entry = g_strdup_printf ("%s - %s [%s] \"%s %s HTTP/1.%d\" %u %" G_GSIZE_FORMAT " \"%s\" \"%s\"",
                           remote_addr,
                           user,
                           date_str,
                           evd_http_request_get_method (request),
                           path,
                           evd_http_message_get_version (EVD_HTTP_MESSAGE (request)),
                           status_code,
                           content_size,
                           referer,
                           user_agent);

  g_free (remote_addr);
  g_free (date_str);
  g_free (user);
  g_free (path);

  return entry;
}

static gboolean
evd_web_service_log (EvdWebService      *self,
                     EvdHttpConnection  *conn,
                     EvdHttpRequest     *request,
                     guint               status_code,
                     gsize               content_size,
                     GError            **error)
{
  gchar *log_entry;

  /* @TODO: provide a 'log-format' property to EvdWebService to use here */
  log_entry = evd_web_service_build_log_entry (self,
                                               NULL,
                                               conn,
                                               request,
                                               status_code,
                                               content_size,
                                               error);

  if (log_entry == NULL)
    return FALSE;

  g_signal_emit (self,
                 evd_web_service_signals[SIGNAL_LOG_ENTRY],
                 0,
                 log_entry,
                 NULL);

  g_free (log_entry);

  return TRUE;
}

/* public methods */

EvdWebService *
evd_web_service_new (void)
{
  EvdWebService *self;

  self = g_object_new (EVD_TYPE_WEB_SERVICE, NULL);

  return self;
}

gboolean
evd_web_service_add_connection_with_request (EvdWebService     *self,
                                             EvdHttpConnection *conn,
                                             EvdHttpRequest    *request,
                                             EvdService        *return_to)
{
  EvdService *old_service;

  g_return_val_if_fail (EVD_IS_WEB_SERVICE (self), FALSE);
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (conn), FALSE);
  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (request), FALSE);

  evd_http_connection_set_current_request (conn, request);

  old_service = EVD_SERVICE (g_object_get_data (G_OBJECT (conn),
                                                RETURN_DATA_KEY));

  if (old_service == NULL && return_to != NULL)
    {
      g_object_set_data (G_OBJECT (conn), RETURN_DATA_KEY, return_to);

      g_object_weak_ref (G_OBJECT (conn),
                         evd_web_service_on_conn_destroyed,
                         NULL);

      g_object_weak_ref (G_OBJECT (return_to),
                         evd_web_service_on_service_destroyed,
                         conn);
    }

  return evd_io_stream_group_add (EVD_IO_STREAM_GROUP (self),
                                  G_IO_STREAM (conn));
}

/**
 * evd_web_service_respond:
 * @headers: (allow-none):
 *
 **/
gboolean
evd_web_service_respond (EvdWebService       *self,
                         EvdHttpConnection   *conn,
                         guint                status_code,
                         SoupMessageHeaders  *headers,
                         const gchar         *content,
                         gsize                size,
                         GError             **error)
{
  EvdWebServiceClass *class;

  g_return_val_if_fail (EVD_IS_WEB_SERVICE (self), FALSE);
  g_return_val_if_fail (EVD_IS_HTTP_CONNECTION (conn), FALSE);

  class = EVD_WEB_SERVICE_GET_CLASS (self);

  g_assert (class->respond != NULL);
  return class->respond (self,
                         conn,
                         status_code,
                         headers,
                         content,
                         size,
                         error);
}

void
evd_web_service_set_origin_policy (EvdWebService *self, EvdPolicy policy)
{
  EvdWebServicePrivate *priv;

  g_return_if_fail (EVD_IS_WEB_SERVICE (self));

  priv = EVD_WEB_SERVICE_GET_PRIVATE (self);

  priv->origin_policy = policy;
}

EvdPolicy
evd_web_service_get_origin_policy (EvdWebService *self)
{
  EvdWebServicePrivate *priv;

  g_return_val_if_fail (EVD_IS_WEB_SERVICE (self), 0);

  priv = EVD_WEB_SERVICE_GET_PRIVATE (self);

  return priv->origin_policy;
}

void
evd_web_service_allow_origin (EvdWebService *self, const gchar *origin)
{
  EvdWebServicePrivate *priv;
  gboolean *allowed;

  g_return_if_fail (EVD_IS_WEB_SERVICE (self));
  g_return_if_fail (origin != NULL);

  priv = EVD_WEB_SERVICE_GET_PRIVATE (self);

  allowed = g_new (gboolean, 1);
  *allowed = TRUE;

  g_hash_table_insert (priv->origins, g_strdup (origin), allowed);
}

void
evd_web_service_deny_origin (EvdWebService *self, const gchar *origin)
{
  EvdWebServicePrivate *priv;
  gboolean *allowed;

  g_return_if_fail (EVD_IS_WEB_SERVICE (self));
  g_return_if_fail (origin != NULL);

  priv = EVD_WEB_SERVICE_GET_PRIVATE (self);

  allowed = g_new (gboolean, 1);
  *allowed = FALSE;

  g_hash_table_insert (priv->origins, g_strdup (origin), allowed);
}

gboolean
evd_web_service_origin_allowed (EvdWebService *self, const gchar *origin)
{
  EvdWebServicePrivate *priv;
  gboolean *allowed;

  g_return_val_if_fail (EVD_IS_WEB_SERVICE (self), FALSE);
  g_return_val_if_fail (origin != NULL, FALSE);

  priv = EVD_WEB_SERVICE_GET_PRIVATE (self);

  allowed = g_hash_table_lookup (priv->origins, origin);
  if (allowed == NULL)
    return priv->origin_policy == EVD_POLICY_ALLOW;
  else
    return (gboolean) (*allowed);
}
