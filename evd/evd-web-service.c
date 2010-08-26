/*
 * evd-web-service.c
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

#include "evd-web-service.h"

G_DEFINE_ABSTRACT_TYPE (EvdWebService, evd_web_service, EVD_TYPE_SERVICE)

static void     evd_web_service_class_init          (EvdWebServiceClass *class);
static void     evd_web_service_init                (EvdWebService *self);

static void     evd_web_service_connection_accepted (EvdService    *service,
                                                     EvdConnection *conn);

static void
evd_web_service_class_init (EvdWebServiceClass *class)
{
  EvdServiceClass *service_class = EVD_SERVICE_CLASS (class);

  service_class->connection_accepted = evd_web_service_connection_accepted;
}

static void
evd_web_service_init (EvdWebService *self)
{
  evd_service_set_io_stream_type (EVD_SERVICE (self), EVD_TYPE_HTTP_CONNECTION);
}

static void
evd_web_service_conn_on_headers_read (GObject      *obj,
                                      GAsyncResult *res,
                                      gpointer      user_data)
{
  EvdHttpConnection *conn = EVD_HTTP_CONNECTION (obj);

  SoupMessageHeaders *headers;
  SoupHTTPVersion ver;
  gchar *method = NULL;
  gchar *path = NULL;
  GError *error = NULL;

  if ( (headers =
        evd_http_connection_read_request_headers_finish (conn,
                                                         res,
                                                         &ver,
                                                         &method,
                                                         &path,
                                                         &error)) != NULL)
    {
      EvdWebService *self = EVD_WEB_SERVICE (user_data);
      EvdWebServiceClass *class;

      class = EVD_WEB_SERVICE_GET_CLASS (self);
      if (class->headers_read != NULL)
        {
          (* class->headers_read) (self, conn, ver, method, path, headers);
        }
      else
        {
          soup_message_headers_free (headers);
          g_free (method);
          g_free (path);
        }
    }
  else
    {
      g_debug ("error reading request headers: %s", error->message);
      g_error_free (error);

      g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
    }

  g_object_unref (conn);
}

static void
evd_web_service_connection_accepted (EvdService *service, EvdConnection *conn)
{
  EvdWebService *self = EVD_WEB_SERVICE (service);

  g_object_ref (conn);
  evd_http_connection_read_request_headers_async (EVD_HTTP_CONNECTION (conn),
                                          NULL,
                                          evd_web_service_conn_on_headers_read,
                                          self);
}

/* public methods */

EvdWebService *
evd_web_service_new (void)
{
  EvdWebService *self;

  self = g_object_new (EVD_TYPE_WEB_SERVICE, NULL);

  return self;
}
