/*
 * evd-http-message.c
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

#include "evd-http-message.h"

#include "evd-http-connection.h"

#define EVD_HTTP_MESSAGE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                           EVD_TYPE_HTTP_MESSAGE, \
                                           EvdHttpMessagePrivate))

G_DEFINE_ABSTRACT_TYPE (EvdHttpMessage, evd_http_message, G_TYPE_OBJECT)

/* private data */
struct _EvdHttpMessagePrivate
{
  SoupHTTPVersion version;
  SoupMessageHeaders *headers;
};

/* properties */
enum
{
  PROP_0,
  PROP_VERSION,
  PROP_HEADERS
};

static void     evd_http_message_class_init           (EvdHttpMessageClass *class);
static void     evd_http_message_init                 (EvdHttpMessage *self);

static void     evd_http_message_finalize             (GObject *obj);
static void     evd_http_message_dispose              (GObject *obj);

static void     evd_http_message_set_property         (GObject      *obj,
                                                       guint         prop_id,
                                                       const GValue *value,
                                                       GParamSpec   *pspec);
static void     evd_http_message_get_property         (GObject    *obj,
                                                       guint       prop_id,
                                                       GValue     *value,
                                                       GParamSpec *pspec);

static void
evd_http_message_class_init (EvdHttpMessageClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_http_message_dispose;
  obj_class->finalize = evd_http_message_finalize;
  obj_class->get_property = evd_http_message_get_property;
  obj_class->set_property = evd_http_message_set_property;

  g_object_class_install_property (obj_class, PROP_VERSION,
                                   g_param_spec_enum ("version",
                                                      "HTTP Version",
                                                      "The HTTP protocol version to use",
                                                      SOUP_TYPE_HTTP_VERSION,
                                                      SOUP_HTTP_1_1,
                                                      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_HEADERS,
                                   g_param_spec_boxed ("headers",
                                                       "HTTP message headers",
                                                       "The HTTP message headers",
                                                       SOUP_TYPE_MESSAGE_HEADERS,
                                                       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
                                                       G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdHttpMessagePrivate));
}

static void
evd_http_message_init (EvdHttpMessage *self)
{
  EvdHttpMessagePrivate *priv;

  priv = EVD_HTTP_MESSAGE_GET_PRIVATE (self);
  self->priv = priv;
}

static void
evd_http_message_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_http_message_parent_class)->dispose (obj);
}

static void
evd_http_message_finalize (GObject *obj)
{
  EvdHttpMessage *self = EVD_HTTP_MESSAGE (obj);

  if (self->priv->headers != NULL)
    soup_message_headers_free (self->priv->headers);

  G_OBJECT_CLASS (evd_http_message_parent_class)->finalize (obj);
}

static void
evd_http_message_set_property (GObject      *obj,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  EvdHttpMessage *self;

  self = EVD_HTTP_MESSAGE (obj);

  switch (prop_id)
    {
    case PROP_VERSION:
      self->priv->version = g_value_get_enum (value);
      break;

    case PROP_HEADERS:
      if (self->priv->headers != NULL)
        soup_message_headers_free (self->priv->headers);
      self->priv->headers = g_value_get_boxed (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_http_message_get_property (GObject    *obj,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  EvdHttpMessage *self;

  self = EVD_HTTP_MESSAGE (obj);

  switch (prop_id)
    {
    case PROP_VERSION:
      g_value_set_enum (value, self->priv->version);
      break;

    case PROP_HEADERS:
      g_value_set_boxed (value, evd_http_message_get_headers (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

/* public methods */

EvdHttpMessage *
evd_http_message_new (SoupHTTPVersion     version,
                      SoupMessageHeaders *headers)
{
  EvdHttpMessage *self;

  self = g_object_new (EVD_TYPE_HTTP_MESSAGE,
                       "version", version,
                       "headers", headers,
                       NULL);

  return self;
}

SoupHTTPVersion
evd_http_message_get_version (EvdHttpMessage *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_MESSAGE (self), 0);

  return self->priv->version;
}

/**
 * evd_http_message_get_headers:
 *
 * Returns: (transfer none):
 **/
SoupMessageHeaders *
evd_http_message_get_headers (EvdHttpMessage *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_MESSAGE (self), NULL);

  if (self->priv->headers == NULL)
    self->priv->headers = soup_message_headers_new (SOUP_MESSAGE_HEADERS_REQUEST);

  return self->priv->headers;
}

/**
 * evd_http_message_headers_to_string:
 * @size: (out):
 *
 * Returns: (transfer full):
 **/
gchar *
evd_http_message_headers_to_string (EvdHttpMessage *self,
                                    gsize          *size)
{
  SoupMessageHeaders *headers;
  SoupMessageHeadersIter iter;
  const gchar *name;
  const gchar *value;

  gchar *st;
  GString *buf;
  gchar *result;

  g_return_val_if_fail (EVD_IS_HTTP_MESSAGE (self), NULL);

  headers = evd_http_message_get_headers (EVD_HTTP_MESSAGE (self));

  buf = g_string_new ("");

  soup_message_headers_iter_init (&iter, headers);
  while (soup_message_headers_iter_next (&iter, &name, &value))
    {
      st = g_strdup_printf ("%s: %s\r\n", name, value);
      g_string_append_len (buf, st, strlen (st));
      g_free (st);
    }

  if (size != NULL)
    *size = buf->len;

  result = buf->str;
  g_string_free (buf, FALSE);

  return result;
}
