/*
 * evd-http-request.c
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

#include <libsoup/soup-method.h>

#include "evd-http-request.h"

#define EVD_HTTP_REQUEST_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                           EVD_TYPE_HTTP_REQUEST, \
                                           EvdHttpRequestPrivate))

G_DEFINE_TYPE (EvdHttpRequest, evd_http_request, EVD_TYPE_HTTP_MESSAGE)

/* private data */
struct _EvdHttpRequestPrivate
{
  gchar *method;
  gchar *path;
  SoupURI *uri;
};

/* properties */
enum
{
  PROP_0,
  PROP_METHOD,
  PROP_PATH,
  PROP_URI
};

static void     evd_http_request_class_init           (EvdHttpRequestClass *class);
static void     evd_http_request_init                 (EvdHttpRequest *self);

static void     evd_http_request_finalize             (GObject *obj);
static void     evd_http_request_dispose              (GObject *obj);

static void     evd_http_request_set_property         (GObject      *obj,
                                                       guint         prop_id,
                                                       const GValue *value,
                                                       GParamSpec   *pspec);
static void     evd_http_request_get_property         (GObject    *obj,
                                                       guint       prop_id,
                                                       GValue     *value,
                                                       GParamSpec *pspec);

static void
evd_http_request_class_init (EvdHttpRequestClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_http_request_dispose;
  obj_class->finalize = evd_http_request_finalize;
  obj_class->get_property = evd_http_request_get_property;
  obj_class->set_property = evd_http_request_set_property;

  g_object_class_install_property (obj_class, PROP_METHOD,
                                   g_param_spec_string ("method",
                                                        "Method",
                                                        "The HTTP method of the request (GET, POST, HEAD, etc)",
                                                        SOUP_METHOD_GET,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_PATH,
                                   g_param_spec_string ("path",
                                                        "URI path",
                                                        "The path portion of the requested URL",
                                                        "",
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_URI,
                                   g_param_spec_boxed ("uri",
                                                       "Request URI",
                                                       "The URI of the requested resource",
                                                       SOUP_TYPE_URI,
                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                       G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdHttpRequestPrivate));
}

static void
evd_http_request_init (EvdHttpRequest *self)
{
  EvdHttpRequestPrivate *priv;

  priv = EVD_HTTP_REQUEST_GET_PRIVATE (self);
  self->priv = priv;
}

static void
evd_http_request_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_http_request_parent_class)->dispose (obj);
}

static void
evd_http_request_finalize (GObject *obj)
{
  EvdHttpRequest *self = EVD_HTTP_REQUEST (obj);

  g_free (self->priv->method);
  g_free (self->priv->path);

  if (self->priv->uri != NULL)
    soup_uri_free (self->priv->uri);

  G_OBJECT_CLASS (evd_http_request_parent_class)->finalize (obj);
}

static void
evd_http_request_set_property (GObject      *obj,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  EvdHttpRequest *self;

  self = EVD_HTTP_REQUEST (obj);

  switch (prop_id)
    {
    case PROP_METHOD:
      self->priv->method = g_value_dup_string (value);
      break;

    case PROP_PATH:
      self->priv->path = g_value_dup_string (value);
      break;

    case PROP_URI:
      self->priv->uri = g_value_dup_boxed (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_http_request_get_property (GObject    *obj,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  EvdHttpRequest *self;

  self = EVD_HTTP_REQUEST (obj);

  switch (prop_id)
    {
    case PROP_METHOD:
      g_value_set_string (value, self->priv->method);
      break;

    case PROP_PATH:
      g_value_set_string (value, self->priv->path);
      break;

    case PROP_URI:
      g_value_set_boxed (value, self->priv->uri);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

/* public methods */

EvdHttpRequest *
evd_http_request_new (void)
{
  EvdHttpRequest *self;

  self = g_object_new (EVD_TYPE_HTTP_REQUEST, NULL);

  return self;
}

const gchar *
evd_http_request_get_method (EvdHttpRequest *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (self), NULL);

  return self->priv->method;
}

const gchar *
evd_http_request_get_path (EvdHttpRequest *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (self), NULL);

  return self->priv->path;
}

/**
 * evd_http_request_get_uri:
 *
 * Returns: (transfer none):
 **/
SoupURI *
evd_http_request_get_uri (EvdHttpRequest *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_REQUEST (self), NULL);

  return self->priv->uri;
}
