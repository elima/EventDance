/*
 * evd-web-selector.c
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
#include <libsoup/soup-form.h>

#include "evd-web-selector.h"

#include "evd-http-connection.h"

G_DEFINE_TYPE (EvdWebSelector, evd_web_selector, EVD_TYPE_SERVICE)

#define EVD_WEB_SELECTOR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                           EVD_TYPE_WEB_SELECTOR, \
                                           EvdWebSelectorPrivate))

/* private data */
struct _EvdWebSelectorPrivate
{
  GList *candidates;

  EvdService *default_service;
};

typedef struct
{
  GRegex *domain_pattern;
  GRegex *path_pattern;
  EvdService *service;
} EvdWebSelectorCandidate;

static void     evd_web_selector_class_init          (EvdWebSelectorClass *class);
static void     evd_web_selector_init                (EvdWebSelector *self);

static void     evd_web_selector_finalize            (GObject *obj);
static void     evd_web_selector_dispose             (GObject *obj);

static gboolean evd_web_selector_remove              (EvdIoStreamGroup *io_stream_group,
                                                      GIOStream        *io_stream);

static void     evd_web_selector_connection_accepted (EvdService    *service,
                                                      EvdConnection *conn);

static void
evd_web_selector_class_init (EvdWebSelectorClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdServiceClass *service_class = EVD_SERVICE_CLASS (class);
  EvdIoStreamGroupClass *io_stream_group_class =
    EVD_IO_STREAM_GROUP_CLASS (class);

  obj_class->dispose = evd_web_selector_dispose;
  obj_class->finalize = evd_web_selector_finalize;

  io_stream_group_class->remove = evd_web_selector_remove;

  service_class->connection_accepted = evd_web_selector_connection_accepted;

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdWebSelectorPrivate));
}

static void
evd_web_selector_init (EvdWebSelector *self)
{
  EvdWebSelectorPrivate *priv;

  priv = EVD_WEB_SELECTOR_GET_PRIVATE (self);
  self->priv = priv;

  priv->candidates = NULL;

  priv->default_service = NULL;

  evd_service_set_io_stream_type (EVD_SERVICE (self), EVD_TYPE_HTTP_CONNECTION);
}

static void
evd_web_selector_dispose (GObject *obj)
{
  EvdWebSelector *self = EVD_WEB_SELECTOR (obj);

  evd_web_selector_set_default_service (self, NULL);

  G_OBJECT_CLASS (evd_web_selector_parent_class)->dispose (obj);
}

static void
evd_web_selector_finalize (GObject *obj)
{
  //  EvdWebSelector *self = EVD_WEB_SELECTOR (obj);

  G_OBJECT_CLASS (evd_web_selector_parent_class)->finalize (obj);
}

static EvdService *
evd_web_selector_find_match (EvdWebSelector *self,
                             const gchar    *domain,
                             const gchar    *path)
{
  GList *node;

  node = self->priv->candidates;
  while (node != NULL)
    {
      EvdWebSelectorCandidate *candidate;

      candidate = (EvdWebSelectorCandidate *) node->data;

      if ( (candidate->domain_pattern == NULL ||
            g_regex_match (candidate->domain_pattern, domain, 0, NULL)) &&
           (candidate->path_pattern == NULL ||
            g_regex_match (candidate->path_pattern, path, 0, NULL)) )
        return candidate->service;

      node = node->next;
    }

  return NULL;
}

static void
evd_web_selector_conn_on_headers_read (GObject      *obj,
                                       GAsyncResult *res,
                                       gpointer      user_data)
{
  EvdWebSelector *self = EVD_WEB_SELECTOR (user_data);
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
      const gchar *domain;
      EvdService *service;

      domain = soup_message_headers_get_one (headers, "host");

      if ( (service = evd_web_selector_find_match (self, domain, path)) == NULL)
        service = self->priv->default_service;

      if (service != NULL)
        {
          soup_message_headers_replace (headers, "connection", "close");
          soup_message_headers_remove (headers, "keep-alive");

          if (evd_http_connection_unread_request_headers (conn,
                                                          ver,
                                                          method,
                                                          path,
                                                          headers,
                                                          NULL,
                                                          &error))
            {
              evd_io_stream_group_add (EVD_IO_STREAM_GROUP (service), G_IO_STREAM (conn));
            }
        }
      else
        {
          /* @TODO: No service found.
             Respond politely using HTTP, or just drop connection? */
          g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
        }


      soup_message_headers_free (headers);
      g_free (method);
      g_free (path);
    }

  if (error != NULL)
    {
      /* @TODO: handle error */
      g_debug ("error reading request headers: %s", error->message);

      g_error_free (error);
    }

  g_object_unref (conn);
}

static void
evd_web_selector_read_headers (EvdWebSelector     *self,
                               EvdHttpConnection  *conn)
{
  g_object_ref (conn);

  evd_http_connection_read_request_headers_async (conn,
                                          NULL,
                                          evd_web_selector_conn_on_headers_read,
                                          self);
}

static void
evd_web_selector_connection_accepted (EvdService *service, EvdConnection *conn)
{
  EvdWebSelector *self = EVD_WEB_SELECTOR (service);

  g_object_ref (conn);

  evd_web_selector_read_headers (self, EVD_HTTP_CONNECTION (conn));
}

static gboolean
evd_web_selector_remove (EvdIoStreamGroup *io_stream_group,
                         GIOStream        *io_stream)
{
  EvdConnection *conn = EVD_CONNECTION (io_stream);

  if (! EVD_IO_STREAM_GROUP_CLASS (evd_web_selector_parent_class)->
      remove (io_stream_group, io_stream))
    {
      return FALSE;
    }

  /* @TODO */

  g_object_unref (conn);

  return TRUE;
}

/* public methods */

EvdWebSelector *
evd_web_selector_new (void)
{
  EvdWebSelector *self;

  self = g_object_new (EVD_TYPE_WEB_SELECTOR, NULL);

  return self;
}

/**
 * evd_web_selector_add_service:
 * @domain_pattern: (allow-none):
 * @path_patter: (allow-none):
 *
 **/
gboolean
evd_web_selector_add_service (EvdWebSelector  *self,
                              const gchar     *domain_pattern,
                              const gchar     *path_pattern,
                              EvdService      *service,
                              GError         **error)
{
  EvdWebSelectorCandidate *candidate;
  GRegex *domain_regex = NULL;
  GRegex *path_regex = NULL;

  g_return_val_if_fail (EVD_IS_WEB_SELECTOR (self), FALSE);
  g_return_val_if_fail (EVD_IS_SERVICE (service), FALSE);

  if (domain_pattern != NULL &&
      (domain_regex = g_regex_new (domain_pattern,
                                   G_REGEX_CASELESS,
                                   0,
                                   error)) == NULL)
    return FALSE;

  if (path_pattern != NULL &&
      (path_regex = g_regex_new (path_pattern,
                                 G_REGEX_CASELESS,
                                 0,
                                 error)) == NULL)
    return FALSE;

  candidate = g_new0 (EvdWebSelectorCandidate, 1);
  candidate->service = service;
  candidate->domain_pattern = domain_regex;
  candidate->path_pattern = path_regex;

  self->priv->candidates = g_list_append (self->priv->candidates, candidate);

  return TRUE;
}

/**
 * evd_web_selector_set_default_service:
 * @service: (allow-none):
 *
 **/
void
evd_web_selector_set_default_service (EvdWebSelector *self,
                                      EvdService     *service)
{
  g_return_if_fail (EVD_IS_WEB_SELECTOR (self));
  g_return_if_fail (EVD_IS_SERVICE (service));

  if (self->priv->default_service != NULL)
    {
      g_object_unref (self->priv->default_service);
      self->priv->default_service = NULL;
    }

  self->priv->default_service = service;

  if (self->priv->default_service != NULL)
    g_object_ref (self->priv->default_service);
}
