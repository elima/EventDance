/*
 * evd-web-dir.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009/2010, Igalia S.L.
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

#include "evd-web-dir.h"

#define EVD_WEB_DIR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                      EVD_TYPE_WEB_DIR, \
                                      EvdWebDirPrivate))

G_DEFINE_TYPE (EvdWebDir, evd_web_dir, EVD_TYPE_WEB_SERVICE)

#define DEFAULT_ROOT_PATH "."

#define DEFAULT_ALLOW_PUT FALSE

#define BLOCK_SIZE 0xFFFF

/* private data */
struct _EvdWebDirPrivate
{
  gchar *root;

  gboolean allow_put;
};

typedef struct
{
  EvdWebDir *web_dir;
  GFile *file;
  GIOStream *io_stream;
  EvdHttpConnection *conn;
  void *buffer;
  gsize size;
} EvdWebDirBinding;

/* properties */
enum
{
  PROP_0,
  PROP_ROOT,
  PROP_ALLOW_PUT
};

static void     evd_web_dir_class_init           (EvdWebDirClass *class);
static void     evd_web_dir_init                 (EvdWebDir *self);

static void     evd_web_dir_finalize             (GObject *obj);
static void     evd_web_dir_dispose              (GObject *obj);

static void     evd_web_dir_set_property         (GObject      *obj,
                                                  guint         prop_id,
                                                  const GValue *value,
                                                  GParamSpec   *pspec);
static void     evd_web_dir_get_property         (GObject    *obj,
                                                  guint       prop_id,
                                                  GValue     *value,
                                                  GParamSpec *pspec);

static void     evd_web_dir_request_handler      (EvdWebService     *self,
                                                  EvdHttpConnection *conn,
                                                  EvdHttpRequest    *request);

static void     evd_web_dir_file_read_block      (EvdWebDirBinding *binding);

static void     evd_web_dir_conn_on_write        (EvdConnection *conn,
                                                  gpointer       user_data);

static void
evd_web_dir_class_init (EvdWebDirClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdWebServiceClass *web_service_class = EVD_WEB_SERVICE_CLASS (class);

  obj_class->dispose = evd_web_dir_dispose;
  obj_class->finalize = evd_web_dir_finalize;
  obj_class->get_property = evd_web_dir_get_property;
  obj_class->set_property = evd_web_dir_set_property;

  web_service_class->request_handler = evd_web_dir_request_handler;

  g_object_class_install_property (obj_class, PROP_ROOT,
                                   g_param_spec_string ("root",
                                                        "Document root",
                                                        "The root path to serve files from",
                                                        DEFAULT_ROOT_PATH,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_ALLOW_PUT,
                                   g_param_spec_boolean ("allow-put",
                                                         "Allow PUT method",
                                                         "Sets/gets whether to allow HTTP PUT method",
                                                         DEFAULT_ALLOW_PUT,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdWebDirPrivate));
}

static void
evd_web_dir_init (EvdWebDir *self)
{
  EvdWebDirPrivate *priv;

  priv = EVD_WEB_DIR_GET_PRIVATE (self);
  self->priv = priv;

  priv->allow_put = DEFAULT_ALLOW_PUT;

  evd_service_set_io_stream_type (EVD_SERVICE (self), EVD_TYPE_HTTP_CONNECTION);
}

static void
evd_web_dir_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_web_dir_parent_class)->dispose (obj);
}

static void
evd_web_dir_finalize (GObject *obj)
{
  EvdWebDir *self = EVD_WEB_DIR (obj);

  g_free (self->priv->root);

  G_OBJECT_CLASS (evd_web_dir_parent_class)->finalize (obj);
}

static void
evd_web_dir_set_property (GObject      *obj,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  EvdWebDir *self;

  self = EVD_WEB_DIR (obj);

  switch (prop_id)
    {
    case PROP_ROOT:
      self->priv->root = g_value_dup_string (value);
      break;

    case PROP_ALLOW_PUT:
      self->priv->allow_put = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_web_dir_get_property (GObject    *obj,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  EvdWebDir *self;

  self = EVD_WEB_DIR (obj);

  switch (prop_id)
    {
    case PROP_ROOT:
      g_value_set_string (value, self->priv->root);
      break;

    case PROP_ALLOW_PUT:
      g_value_set_boolean (value, self->priv->allow_put);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_web_dir_finish_request (EvdWebDirBinding *binding)
{
  EvdWebDir *self;
  EvdHttpConnection *conn;

  /* @TODO: consider caching the GFile for future requests */

  self = binding->web_dir;
  conn = binding->conn;
  g_signal_handlers_disconnect_by_func (conn,
                                        evd_web_dir_conn_on_write,
                                        binding);

  g_object_unref (binding->file);
  if (binding->io_stream != NULL)
    g_object_unref (binding->io_stream);
  g_slice_free1 (BLOCK_SIZE, binding->buffer);

  g_slice_free (EvdWebDirBinding, binding);

  EVD_WEB_SERVICE_CLASS (evd_web_dir_parent_class)->
    return_connection (EVD_WEB_SERVICE (self), conn);

  g_object_unref (conn);
}

static void
evd_web_dir_handle_content_error (EvdWebDirBinding *binding,
                                  GError           *error)
{
  guint status_code;

  switch (error->code)
    {
    case G_IO_ERROR_NOT_FOUND:
      status_code = SOUP_STATUS_NOT_FOUND;
      break;

    default:
      status_code = SOUP_STATUS_IO_ERROR;
      break;
    }

  EVD_WEB_SERVICE_CLASS (evd_web_dir_parent_class)->
    respond (EVD_WEB_SERVICE (binding->web_dir),
             binding->conn,
             status_code,
             NULL,
             NULL,
             0,
             NULL);

  evd_web_dir_finish_request (binding);
}

static void
evd_web_dir_file_on_block_read (GObject      *object,
                                GAsyncResult *res,
                                gpointer      user_data)
{
  EvdWebDirBinding *binding = (EvdWebDirBinding *) user_data;
  gssize size;
  GError *error = NULL;

  if ( (size = g_input_stream_read_finish (G_INPUT_STREAM (object),
                                           res,
                                           &error)) > 0)
    {
      if (! evd_http_connection_write_content (binding->conn,
                                               binding->buffer,
                                               size,
                                               NULL,
                                               &error))
        {
          g_debug ("failed writing file to socket!");
        }
      else if (size < BLOCK_SIZE) /* EOF */
        {
          evd_web_dir_finish_request (binding);
        }
      else
        {
          evd_web_dir_file_read_block (binding);
        }
    }
  else if (size == 0) /* EOF */
    {
      evd_web_dir_finish_request (binding);
    }
  else
    {
      evd_web_dir_handle_content_error (binding, error);
      g_error_free (error);
    }
}

static void
evd_web_dir_file_read_block (EvdWebDirBinding *binding)
{
  GInputStream *stream;

  if (evd_connection_get_max_writable (EVD_CONNECTION (binding->conn)) > 0)
    {
      stream = g_io_stream_get_input_stream (binding->io_stream);

      g_input_stream_read_async (stream,
                                 binding->buffer,
                                 BLOCK_SIZE,
                                 evd_connection_get_priority (EVD_CONNECTION (binding->conn)),
                                 NULL,
                                 evd_web_dir_file_on_block_read,
                                 binding);
    }
}

static void
evd_web_dir_file_on_open (GObject      *object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  EvdWebDirBinding *binding = (EvdWebDirBinding *) user_data;
  GFile *file = G_FILE (object);
  GFileIOStream *io_stream;
  GError *error = NULL;

  if ( (io_stream = g_file_open_readwrite_finish (file,
                                                  res,
                                                  &error)) != NULL)
    {
      binding->io_stream = G_IO_STREAM (io_stream);
      evd_web_dir_file_read_block (binding);
    }
  else
    {
      evd_web_dir_handle_content_error (binding, error);
      g_error_free (error);
    }
}

static void
evd_web_dir_file_on_info (GObject      *object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  EvdWebDirBinding *binding = (EvdWebDirBinding *) user_data;
  GFile *file = G_FILE (object);
  EvdHttpConnection *conn = binding->conn;
  GError *error = NULL;
  GFileInfo *info;
  SoupHTTPVersion ver;
  EvdHttpRequest *request;

  request = evd_http_connection_get_current_request (conn);
  ver = evd_http_message_get_version (EVD_HTTP_MESSAGE (request));

  if ( (info = g_file_query_info_finish (G_FILE (object),
                                         res,
                                         &error)) != NULL)
    {
      SoupMessageHeaders *headers;

      headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);

      soup_message_headers_set_content_type (headers,
                                            g_file_info_get_content_type (info),
                                            NULL);

      soup_message_headers_set_content_length (headers,
                                               g_file_info_get_size (info));

      if (evd_http_connection_write_response_headers (conn,
                                                      ver,
                                                      SOUP_STATUS_OK,
                                                      NULL,
                                                      headers,
                                                      NULL,
                                                      &error))
        {
          /* now open file */
          g_file_open_readwrite_async (file,
                            evd_connection_get_priority (EVD_CONNECTION (conn)),
                            NULL,
                            evd_web_dir_file_on_open,
                            binding);
        }
      else
        {
          g_debug ("error sending response headers: %s", error->message);
          g_error_free (error);
        }

      soup_message_headers_free (headers);
      g_object_unref (info);
    }
  else
    {
      evd_web_dir_handle_content_error (binding, error);
      g_error_free (error);
    }
}

static void
evd_web_dir_conn_on_write (EvdConnection *conn, gpointer user_data)
{
  EvdWebDirBinding *binding = (EvdWebDirBinding *) user_data;

  if (binding->io_stream != NULL)
    evd_web_dir_file_read_block (binding);
}

static gboolean
evd_web_dir_method_allowed (EvdWebDir *self, const gchar *method)
{
  return g_strcmp0 (method, "GET") == 0
    || (g_strcmp0 (method, "PUT") == 0 && self->priv->allow_put);
}

static void
evd_web_dir_request_handler (EvdWebService     *web_service,
                             EvdHttpConnection *conn,
                             EvdHttpRequest    *request)
{
  EvdWebDir *self = EVD_WEB_DIR (web_service);
  gchar *filename = NULL;
  GFile *file;
  EvdWebDirBinding *binding;
  SoupURI *uri;
  const gchar *FILE_ATTRS =
    "standard::fast-content-type,standard::content-type,standard::size";

  if (! evd_web_dir_method_allowed (self,
                                    evd_http_request_get_method (request)))
    {
      EVD_WEB_SERVICE_CLASS (evd_web_dir_parent_class)->
        respond (web_service,
                 conn,
                 SOUP_STATUS_METHOD_NOT_ALLOWED,
                 NULL,
                 NULL,
                 0,
                 NULL);

      return;
    }

  uri = evd_http_request_get_uri (request);
  filename = g_strconcat (self->priv->root,
                          "/",
                          uri->path,
                          NULL);
  file = g_file_new_for_path (filename);
  g_free (filename);

  binding = g_slice_new0 (EvdWebDirBinding);

  binding->web_dir = self;
  binding->file = file;
  binding->buffer = g_slice_alloc (BLOCK_SIZE);

  g_object_ref (conn);
  binding->conn = conn;
  g_signal_connect (conn,
                    "write",
                    G_CALLBACK (evd_web_dir_conn_on_write),
                    binding);

  g_file_query_info_async (file,
                           FILE_ATTRS,
                           G_FILE_QUERY_INFO_NONE,
                           evd_connection_get_priority (EVD_CONNECTION (conn)),
                           NULL,
                           evd_web_dir_file_on_info,
                           binding);
}

/* public methods */

EvdWebDir *
evd_web_dir_new ()
{
  EvdWebDir *self;

  self = g_object_new (EVD_TYPE_WEB_DIR, NULL);

  return self;
}

void
evd_web_dir_set_root (EvdWebDir *self, const gchar *root)
{
  g_return_if_fail (EVD_IS_WEB_DIR (self));
  g_return_if_fail (root != NULL);

  if (self->priv->root != NULL)
    g_free (self->priv->root);

  self->priv->root = g_strdup (root);
}

const gchar *
evd_web_dir_get_root (EvdWebDir *self)
{
  g_return_val_if_fail (EVD_IS_WEB_DIR (self), NULL);

  return self->priv->root;
}
