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

#include <string.h>
#include <libsoup/soup.h>

#include "evd-web-dir.h"

#define EVD_WEB_DIR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                      EVD_TYPE_WEB_DIR, \
                                      EvdWebDirPrivate))

G_DEFINE_TYPE (EvdWebDir, evd_web_dir, EVD_TYPE_WEB_SERVICE)

#define DEFAULT_ROOT_PATH "."

#define DEFAULT_ALLOW_PUT FALSE

#define BLOCK_SIZE 0x0FFF

#define DEFAULT_DIRECTORY_INDEX "index.html"

/* private data */
struct _EvdWebDirPrivate
{
  gchar *root;
  gchar *alias;
  gboolean allow_put;
  gchar *dir_index;
};

typedef struct
{
  EvdWebDir *web_dir;
  GFile *file;
  GFileInputStream *file_input_stream;
  EvdHttpConnection *conn;
  EvdHttpRequest *request;
  void *buffer;
  gsize size;
  gchar *filename;
  gsize response_content_size;
  guint response_status_code;
  SoupMessageHeaders *response_headers;
  gboolean response_headers_sent;
} EvdWebDirBinding;

/* properties */
enum
{
  PROP_0,
  PROP_ROOT,
  PROP_ALIAS,
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

static void     evd_web_dir_request_file         (EvdWebDir        *self,
                                                  const gchar      *filename,
                                                  EvdWebDirBinding *binding);

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

  g_object_class_install_property (obj_class, PROP_ALIAS,
                                   g_param_spec_string ("alias",
                                                        "Document alias",
                                                        "The alias path to serve files from",
                                                        NULL,
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

  priv->dir_index = g_strdup (DEFAULT_DIRECTORY_INDEX);

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
  g_free (self->priv->alias);
  g_free (self->priv->dir_index);

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

    case PROP_ALIAS:
      evd_web_dir_set_alias (self, g_value_dup_string (value));
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

    case PROP_ALIAS:
      g_value_set_string (value, evd_web_dir_get_alias (self));
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

  EVD_WEB_SERVICE_LOG (EVD_WEB_SERVICE (binding->web_dir),
                       binding->conn,
                       binding->request,
                       binding->response_status_code,
                       binding->response_content_size,
                       NULL);

  self = binding->web_dir;
  conn = binding->conn;
  g_signal_handlers_disconnect_by_func (conn,
                                        evd_web_dir_conn_on_write,
                                        binding);

  g_object_unref (binding->request);

  g_object_unref (binding->file);
  if (binding->file_input_stream != NULL)
    g_object_unref (binding->file_input_stream);

  if (binding->buffer != NULL)
    g_slice_free1 (BLOCK_SIZE, binding->buffer);

  if (binding->response_headers != NULL)
    soup_message_headers_free (binding->response_headers);

  g_free (binding->filename);

  g_slice_free (EvdWebDirBinding, binding);

  EVD_WEB_SERVICE_CLASS (evd_web_dir_parent_class)->
    flush_and_return_connection (EVD_WEB_SERVICE (self), conn);

  g_object_unref (conn);
}

static void
evd_web_dir_handle_content_error (EvdWebDirBinding *binding,
                                  GError           *error)
{
  switch (error->code)
    {
    case G_IO_ERROR_NOT_FOUND:
      binding->response_status_code = SOUP_STATUS_NOT_FOUND;
      break;

    case G_IO_ERROR_PERMISSION_DENIED:
      binding->response_status_code = SOUP_STATUS_FORBIDDEN;
      break;

    default:
      binding->response_status_code = SOUP_STATUS_IO_ERROR;
      break;
    }

  if (! binding->response_headers_sent)
    EVD_WEB_SERVICE_CLASS (evd_web_dir_parent_class)->
      respond (EVD_WEB_SERVICE (binding->web_dir),
               binding->conn,
               binding->response_status_code,
               NULL,
               NULL,
               0,
               NULL);
  else
    g_io_stream_close (G_IO_STREAM (binding->conn), NULL, NULL);

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
                                               TRUE,
                                               &error))
        {
          evd_web_dir_handle_content_error (binding, error);
          g_error_free (error);
        }
      else
        {
          binding->response_content_size += size;

          evd_web_dir_file_read_block (binding);
        }
    }
  else if (size == 0) /* EOF */
    {
      evd_web_dir_finish_request (binding);
    }
  else
    {
      /* @TODO: do proper logging */
      g_debug ("Error reading file block: %s", error->message);
      evd_web_dir_handle_content_error (binding, error);
      g_error_free (error);
    }
}

static void
evd_web_dir_file_read_block (EvdWebDirBinding *binding)
{
  GInputStream *stream;

  stream = G_INPUT_STREAM (binding->file_input_stream);

  if (! g_input_stream_has_pending (stream) &&
      evd_connection_get_max_writable (EVD_CONNECTION (binding->conn)) > 0)
    {
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
  GError *error = NULL;
  SoupHTTPVersion ver;

  if ( (binding->file_input_stream = g_file_read_finish (file,
                                                         res,
                                                         &error)) == NULL)
    {
      /* @TODO: do proper logging */
      g_print ("Error opening file: %s\n", error->message);
      evd_web_dir_handle_content_error (binding, error);
      g_error_free (error);

      return;
    }

  /* file opened successfully */

  /* now it is ok to send response headers */
  ver = evd_http_message_get_version (EVD_HTTP_MESSAGE (binding->request));
  if (! evd_http_connection_write_response_headers (binding->conn,
                                                    ver,
                                                    SOUP_STATUS_OK,
                                                    NULL,
                                                    binding->response_headers,
                                                    &error))
    {
      g_print ("Error sending response headers: %s\n", error->message);
      evd_web_dir_handle_content_error (binding, error);
      g_error_free (error);

      return;
    }

  /* headers successfully sent */
  binding->response_headers_sent = TRUE;
  binding->response_status_code = SOUP_STATUS_OK;

  /* start reading */
  binding->buffer = g_slice_alloc (BLOCK_SIZE);
  evd_web_dir_file_read_block (binding);
}

static gboolean
evd_web_dir_check_not_modified (EvdWebDir          *self,
                                EvdHttpConnection  *conn,
                                EvdHttpRequest     *request,
                                SoupMessageHeaders *response_headers,
                                SoupHTTPVersion     http_version,
                                guint64             file_last_modified_time)
{
  gboolean result = FALSE;
  SoupMessageHeaders *req_headers;
  const gchar *modified_date_st;
  SoupDate *modified_date;

  req_headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (request));

  modified_date_st = soup_message_headers_get_one (req_headers,
                                                   "If-Modified-Since");
  if (modified_date_st == NULL)
    return FALSE;

  modified_date = soup_date_new_from_string (modified_date_st);
  if (modified_date != NULL)
    {
      guint64 modified_date_int;

      modified_date_int = soup_date_to_time_t (modified_date);

      if (modified_date_int >= file_last_modified_time)
        {
          GError *error = NULL;

          if (! evd_web_service_respond (EVD_WEB_SERVICE (self),
                                         conn,
                                         SOUP_STATUS_NOT_MODIFIED,
                                         response_headers,
                                         NULL,
                                         0,
                                         &error))
            {
              g_debug ("Error sending NOT-MODIFIED response headers: %s",
                       error->message);
              g_error_free (error);
            }

          result = TRUE;
        }
    }

  soup_date_free (modified_date);

  return result;
}

static void
evd_web_dir_file_on_info (GObject      *object,
                          GAsyncResult *res,
                          gpointer      user_data)
{
  EvdWebDirBinding *binding = user_data;
  EvdWebDir *self = binding->web_dir;
  GFile *file = G_FILE (object);
  EvdHttpConnection *conn = binding->conn;
  GError *error = NULL;
  GFileInfo *info;
  SoupHTTPVersion ver;
  EvdHttpRequest *request;
  GFileType file_type;
  SoupMessageHeaders *headers = NULL;

  guint64 file_modified_date_int;
  SoupDate *sdate;
  gchar *date;

  request = binding->request;
  ver = evd_http_message_get_version (EVD_HTTP_MESSAGE (request));

  info = g_file_query_info_finish (G_FILE (object), res, &error);
  if (info == NULL)
    {
      evd_web_dir_handle_content_error (binding, error);
      g_error_free (error);
      return;
    }

  file_type = g_file_info_get_file_type (info);

  /* file is a directory */
  if (file_type == G_FILE_TYPE_DIRECTORY)
    {
      if (self->priv->dir_index != NULL)
        {
          gchar *new_filename;

          new_filename = g_strdup_printf ("%s/%s",
                                          binding->filename,
                                          self->priv->dir_index);

          evd_web_dir_request_file (self, new_filename, binding);

          g_free (new_filename);
        }
      else
        {
          /* @TODO: respond with 404 Not Found */
        }

      goto out;
    }
  /* file is a symbolic link */
  else if (file_type == G_FILE_TYPE_SYMBOLIC_LINK)
    {
      /* @TODO: check if we allow following symlinks */
      goto out;
    }
  /* file is not a regular file */
  else if (file_type != G_FILE_TYPE_REGULAR)
    {
      /* @TODO: respond with 404 Not Found */
      goto out;
    }

  /* file is a regular file */
  headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_RESPONSE);

  if (evd_http_connection_get_keepalive (conn))
    soup_message_headers_replace (headers, "Connection", "keep-alive");
  else
    soup_message_headers_replace (headers, "Connection", "close");

  /* obtain last-modified value from file info */
  file_modified_date_int =
    g_file_info_get_attribute_uint64 (info, "time::modified");

  /* check last-modified time */
  if (evd_web_dir_check_not_modified (self,
                                      conn,
                                      request,
                                      headers,
                                      ver,
                                      file_modified_date_int))
    {
      evd_web_dir_finish_request (binding);

      goto out;
    }

  /* set 'last-modified' header in response */
  sdate = soup_date_new_from_time_t (file_modified_date_int);
  date = soup_date_to_string (sdate, SOUP_DATE_HTTP);
  soup_message_headers_replace (headers, "Last-Modified", date);
  g_free (date);
  soup_date_free (sdate);

  /* check cross origin */
  if (evd_http_request_is_cross_origin (request))
    {
      const gchar *origin;

      origin = evd_http_request_get_origin (request);

      /* check if this origin is allowed */
      if (evd_web_service_origin_allowed (EVD_WEB_SERVICE (self), origin))
        {
          soup_message_headers_replace (headers,
                                        "Access-Control-Allow-Origin",
                                        origin);
        }
    }

  soup_message_headers_set_content_type (headers,
                                         g_file_info_get_content_type (info),
                                         NULL);
  soup_message_headers_set_content_length (headers,
                                           g_file_info_get_size (info));

  /* now open file */
  g_file_read_async (file,
                     evd_connection_get_priority (EVD_CONNECTION (conn)),
                     NULL,
                     evd_web_dir_file_on_open,
                     binding);

  binding->response_headers = headers;
  headers = NULL;

 out:
  if (headers != NULL)
    soup_message_headers_free (headers);
  g_object_unref (info);
}

static void
evd_web_dir_conn_on_write (EvdConnection *conn, gpointer user_data)
{
  EvdWebDirBinding *binding = (EvdWebDirBinding *) user_data;

  if (binding->file_input_stream != NULL)
    evd_web_dir_file_read_block (binding);
}

static gboolean
evd_web_dir_method_allowed (EvdWebDir *self, const gchar *method)
{
  return g_strcmp0 (method, "GET") == 0
    || (g_strcmp0 (method, "PUT") == 0 && self->priv->allow_put);
}

static void
evd_web_dir_request_file (EvdWebDir        *self,
                          const gchar      *filename,
                          EvdWebDirBinding *binding)
{
  GFile *file;
  const gchar *FILE_ATTRS =
    "standard::content-type,standard::size,standard::type,time::modified";

  g_free (binding->filename);
  binding->filename = g_strdup (filename);

  file = g_file_new_for_path (filename);

  if (binding->file != NULL)
    g_object_unref (binding->file);
  binding->file = file;

  g_file_query_info_async (file,
                           FILE_ATTRS,
                           G_FILE_QUERY_INFO_NONE,
                           evd_connection_get_priority (EVD_CONNECTION (binding->conn)),
                           NULL,
                           evd_web_dir_file_on_info,
                           binding);
}

static void
evd_web_dir_request_handler (EvdWebService     *web_service,
                             EvdHttpConnection *conn,
                             EvdHttpRequest    *request)
{
  EvdWebDir *self = EVD_WEB_DIR (web_service);
  gchar *filename = NULL;
  EvdWebDirBinding *binding;
  SoupURI *uri;
  const gchar *path_without_alias = "";

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

      EVD_WEB_SERVICE_LOG (web_service,
                           conn,
                           request,
                           SOUP_STATUS_METHOD_NOT_ALLOWED,
                           0,
                           NULL);

      return;
    }

  uri = evd_http_request_get_uri (request);

  if (self->priv->alias != NULL)
    {
      if (g_strstr_len (uri->path, -1, self->priv->alias) == uri->path)
        {
          path_without_alias = uri->path + strlen (self->priv->alias);
        }
      else
        {
          EVD_WEB_SERVICE_CLASS (evd_web_dir_parent_class)->
            respond (EVD_WEB_SERVICE (self),
                     conn,
                     SOUP_STATUS_NOT_FOUND,
                     NULL,
                     NULL,
                     0,
                     NULL);

          EVD_WEB_SERVICE_LOG (web_service,
                               conn,
                               request,
                               SOUP_STATUS_NOT_FOUND,
                               0,
                               NULL);

          return;
        }
    }
  else
    {
      path_without_alias = uri->path;
    }

  filename = g_strconcat (self->priv->root,
                          "/",
                          path_without_alias,
                          NULL);

  binding = g_slice_new0 (EvdWebDirBinding);
  binding->web_dir = self;

  g_object_ref (conn);
  binding->conn = conn;
  g_signal_connect (conn,
                    "write",
                    G_CALLBACK (evd_web_dir_conn_on_write),
                    binding);

  g_object_ref (request);
  binding->request = request;

  evd_web_dir_request_file (self, filename, binding);

  g_free (filename);
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
  gchar *_root;

  g_return_if_fail (EVD_IS_WEB_DIR (self));
  g_return_if_fail (root != NULL);

  if (self->priv->root != NULL)
    g_free (self->priv->root);

  if (! g_path_is_absolute (root))
    {
      gchar *current_dir;

      current_dir = g_get_current_dir ();
      _root = g_strdup_printf ("%s/%s", current_dir, root);
      g_free (current_dir);
    }
  else
    {
      _root = g_strdup (root);
    }

  self->priv->root = _root;
}

const gchar *
evd_web_dir_get_root (EvdWebDir *self)
{
  g_return_val_if_fail (EVD_IS_WEB_DIR (self), NULL);

  return self->priv->root;
}

void
evd_web_dir_set_alias (EvdWebDir *self, const gchar *alias)
{
  g_return_if_fail (EVD_IS_WEB_DIR (self));

  if (self->priv->alias != NULL)
    g_free (self->priv->alias);

  self->priv->alias = g_strdup (alias);
}

const gchar *
evd_web_dir_get_alias (EvdWebDir *self)
{
  g_return_val_if_fail (EVD_IS_WEB_DIR (self), NULL);

  return self->priv->alias;
}
