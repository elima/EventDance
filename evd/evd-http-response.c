/*
 * evd-http-response.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2013, Igalia S.L.
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

#include "evd-http-response.h"

#define EVD_HTTP_RESPONSE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                            EVD_TYPE_HTTP_RESPONSE, \
                                            EvdHttpResponsePrivate))

G_DEFINE_TYPE (EvdHttpResponse, evd_http_response, EVD_TYPE_HTTP_MESSAGE)

/* private data */
struct _EvdHttpResponsePrivate
{
  guint status_code;
  gchar *reason_phrase;
};

/* properties */
enum
{
  PROP_0,
  PROP_STATUS_CODE,
  PROP_REASON_PHRASE
};

static void     evd_http_response_class_init           (EvdHttpResponseClass *class);
static void     evd_http_response_init                 (EvdHttpResponse *self);

static void     evd_http_response_finalize             (GObject *obj);
static void     evd_http_response_dispose              (GObject *obj);

static void     evd_http_response_set_property         (GObject      *obj,
                                                        guint         prop_id,
                                                        const GValue *value,
                                                        GParamSpec   *pspec);
static void     evd_http_response_get_property         (GObject    *obj,
                                                        guint       prop_id,
                                                        GValue     *value,
                                                        GParamSpec *pspec);

static void
evd_http_response_class_init (EvdHttpResponseClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdHttpMessageClass *http_msg_class = EVD_HTTP_MESSAGE_CLASS (class);

  obj_class->dispose = evd_http_response_dispose;
  obj_class->finalize = evd_http_response_finalize;
  obj_class->get_property = evd_http_response_get_property;
  obj_class->set_property = evd_http_response_set_property;

  http_msg_class->type = SOUP_MESSAGE_HEADERS_RESPONSE;

  g_object_class_install_property (obj_class, PROP_STATUS_CODE,
                                   g_param_spec_uint ("status-code",
                                                      "Status code",
                                                      "The status code of the HTTP response",
                                                      SOUP_STATUS_NONE,
                                                      G_MAXUINT,
                                                      SOUP_STATUS_OK,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_REASON_PHRASE,
                                   g_param_spec_string ("reason-phrase",
                                                        "Reason phrase",
                                                        "The reason phrase of the HTTP response",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdHttpResponsePrivate));
}

static void
evd_http_response_init (EvdHttpResponse *self)
{
  EvdHttpResponsePrivate *priv;

  priv = EVD_HTTP_RESPONSE_GET_PRIVATE (self);
  self->priv = priv;
}

static void
evd_http_response_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_http_response_parent_class)->dispose (obj);
}

static void
evd_http_response_finalize (GObject *obj)
{
  EvdHttpResponse *self = EVD_HTTP_RESPONSE (obj);

  if (self->priv->reason_phrase != NULL)
    g_free (self->priv->reason_phrase);

  G_OBJECT_CLASS (evd_http_response_parent_class)->finalize (obj);
}

static void
evd_http_response_set_property (GObject      *obj,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  EvdHttpResponse *self;

  self = EVD_HTTP_RESPONSE (obj);

  switch (prop_id)
    {
    case PROP_STATUS_CODE:
      self->priv->status_code = g_value_get_uint (value);
      break;

    case PROP_REASON_PHRASE:
      evd_http_response_set_reason_phrase (self, g_value_dup_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_http_response_get_property (GObject    *obj,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  EvdHttpResponse *self;

  self = EVD_HTTP_RESPONSE (obj);

  switch (prop_id)
    {
    case PROP_STATUS_CODE:
      g_value_set_uint (value, self->priv->status_code);
      break;

    case PROP_REASON_PHRASE:
      g_value_set_string (value, evd_http_response_get_reason_phrase (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

/* public methods */

EvdHttpResponse *
evd_http_response_new (void)
{
  EvdHttpResponse *self;

  self = g_object_new (EVD_TYPE_HTTP_RESPONSE, NULL);

  return self;
}

void
evd_http_response_set_reason_phrase (EvdHttpResponse *self,
                                     const gchar     *reason_phrase)
{
  g_return_if_fail (EVD_IS_HTTP_RESPONSE (self));
  g_return_if_fail (reason_phrase != NULL);

  if (self->priv->reason_phrase != NULL)
    g_free (self->priv->reason_phrase);

  self->priv->reason_phrase = g_strdup (reason_phrase);
}

const gchar *
evd_http_response_get_reason_phrase (EvdHttpResponse *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_RESPONSE (self), NULL);

  return self->priv->reason_phrase;
}

void
evd_http_response_set_status_code (EvdHttpResponse *self,
                                   guint            status_code)
{
  g_return_if_fail (EVD_IS_HTTP_RESPONSE (self));

  self->priv->status_code = status_code;
}

guint
evd_http_response_get_status_code (EvdHttpResponse *self)
{
  g_return_val_if_fail (EVD_IS_HTTP_RESPONSE (self), 0);

  return self->priv->status_code;
}
