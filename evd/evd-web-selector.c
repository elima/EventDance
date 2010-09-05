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

#include "evd-web-selector.h"

G_DEFINE_TYPE (EvdWebSelector, evd_web_selector, EVD_TYPE_WEB_SERVICE)

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
  gchar *domain_pattern;
  gchar *path_pattern;
  GRegex *domain_regex;
  GRegex *path_regex;
  EvdService *service;
} EvdWebSelectorCandidate;

static void     evd_web_selector_class_init          (EvdWebSelectorClass *class);
static void     evd_web_selector_init                (EvdWebSelector *self);

static void     evd_web_selector_dispose             (GObject *obj);

static void     evd_web_selector_headers_read        (EvdWebService      *self,
                                                      EvdHttpConnection  *conn,
                                                      SoupHTTPVersion     ver,
                                                      gchar              *method,
                                                      gchar              *path,
                                                      SoupMessageHeaders *headers);

static void     evd_web_selector_free_candidate      (gpointer data,
                                                      gpointer user_data);

static void
evd_web_selector_class_init (EvdWebSelectorClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdWebServiceClass *web_service_class = EVD_WEB_SERVICE_CLASS (class);

  obj_class->dispose = evd_web_selector_dispose;

  web_service_class->headers_read = evd_web_selector_headers_read;

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
}

static void
evd_web_selector_dispose (GObject *obj)
{
  EvdWebSelector *self = EVD_WEB_SELECTOR (obj);

  evd_web_selector_set_default_service (self, NULL);

  g_list_foreach (self->priv->candidates,
                  evd_web_selector_free_candidate,
                  self);
  g_list_free (self->priv->candidates);
  self->priv->candidates = NULL;

  G_OBJECT_CLASS (evd_web_selector_parent_class)->dispose (obj);
}

static void
evd_web_selector_free_candidate (gpointer data,
                                 gpointer user_data)
{
  EvdWebSelectorCandidate *candidate = (EvdWebSelectorCandidate *) candidate;

  if (candidate->domain_pattern != NULL)
    {
      g_free (candidate->domain_pattern);
      g_regex_unref (candidate->domain_regex);
    }

  if (candidate->path_pattern != NULL)
    {
      g_free (candidate->path_pattern);
      g_regex_unref (candidate->path_regex);
    }

  g_object_unref (candidate->service);
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
            g_regex_match (candidate->domain_regex, domain, 0, NULL)) &&
           (candidate->path_pattern == NULL ||
            g_regex_match (candidate->path_regex, path, 0, NULL)) )
        return candidate->service;

      node = node->next;
    }

  return NULL;
}

static void
evd_web_selector_headers_read (EvdWebService      *web_service,
                               EvdHttpConnection  *conn,
                               SoupHTTPVersion     ver,
                               gchar              *method,
                               gchar              *path,
                               SoupMessageHeaders *headers)
{
  EvdWebSelector *self = EVD_WEB_SELECTOR (web_service);
  const gchar *domain;
  EvdService *service;
  GError *error = NULL;

  domain = soup_message_headers_get_one (headers, "host");

  if ( (service = evd_web_selector_find_match (self, domain, path)) == NULL)
    service = self->priv->default_service;

  if (service != NULL)
    {
      /* keep-alive is not supported currently */
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
      /* no service found, respond with a 403 Forbidden message
         and close the connection */
      evd_http_connection_respond (conn,
                                   ver,
                                   403,
                                   "Forbidden",
                                   NULL,
                                   NULL,
                                   0,
                                   TRUE,
                                   NULL,
                                   NULL);
    }

  soup_message_headers_free (headers);
  g_free (method);
  g_free (path);
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
    {
      if (domain_regex != NULL)
        g_regex_unref (domain_regex);

      return FALSE;
    }

  candidate = g_new0 (EvdWebSelectorCandidate, 1);
  g_object_ref (service);
  candidate->service = service;
  candidate->domain_pattern = g_strdup (domain_pattern);
  candidate->path_pattern = g_strdup (path_pattern);
  candidate->domain_regex = domain_regex;
  candidate->path_regex = path_regex;

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
