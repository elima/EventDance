/*
 * evd-socket-stream.c
 *
 * EventDance - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009/2010, Igalia S.L.
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
 *
 */

#include <math.h>
#include <string.h>

#include "evd-socket-stream.h"

G_DEFINE_ABSTRACT_TYPE (EvdSocketStream, evd_socket_stream, EVD_TYPE_STREAM)

#define EVD_SOCKET_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                            EVD_TYPE_SOCKET_STREAM, \
                                            EvdSocketStreamPrivate))

/* private data */
struct _EvdSocketStreamPrivate
{
  gboolean       tls_autostart;
  EvdTlsSession *tls_session;
};


/* properties */
enum
{
  PROP_0,
  PROP_TLS_AUTOSTART,
  PROP_TLS_SESSION
};

static void     evd_socket_stream_class_init         (EvdSocketStreamClass *class);
static void     evd_socket_stream_init               (EvdSocketStream *self);

static void     evd_socket_stream_dispose            (GObject *obj);

static void     evd_socket_stream_set_property       (GObject      *obj,
                                                      guint         prop_id,
                                                      const GValue *value,
                                                      GParamSpec   *pspec);
static void     evd_socket_stream_get_property       (GObject    *obj,
                                                      guint       prop_id,
                                                      GValue     *value,
                                                      GParamSpec *pspec);

static void     evd_socket_stream_copy_properties    (EvdStream *self,
                                                      EvdStream *target);

static void
evd_socket_stream_class_init (EvdSocketStreamClass *class)
{
  GObjectClass *obj_class;
  EvdStreamClass *stream_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_socket_stream_dispose;
  obj_class->get_property = evd_socket_stream_get_property;
  obj_class->set_property = evd_socket_stream_set_property;

  stream_class = EVD_STREAM_CLASS (class);
  stream_class->copy_properties = evd_socket_stream_copy_properties;

  g_object_class_install_property (obj_class, PROP_TLS_AUTOSTART,
                                   g_param_spec_boolean ("tls-autostart",
                                                         "Enable/disable automatic TLS upgrade",
                                                         "Whether SSL/TLS should be started automatically upon connected",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TLS_SESSION,
                                   g_param_spec_object ("tls",
                                                        "The SSL/TLS session",
                                                        "The underlaying SSL/TLS session object",
                                                        EVD_TYPE_TLS_SESSION,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdSocketStreamPrivate));
}

static void
evd_socket_stream_init (EvdSocketStream *self)
{
  EvdSocketStreamPrivate *priv;

  priv = EVD_SOCKET_STREAM_GET_PRIVATE (self);
  self->priv = priv;

  priv->tls_autostart = FALSE;
  priv->tls_session = NULL;
}

static void
evd_socket_stream_dispose (GObject *obj)
{
  EvdSocketStream *self = EVD_SOCKET_STREAM (obj);

  if (self->priv->tls_session != NULL)
    {
      g_object_unref (self->priv->tls_session);
      self->priv->tls_session = NULL;
    }

  G_OBJECT_CLASS (evd_socket_stream_parent_class)->dispose (obj);
}

static void
evd_socket_stream_set_property (GObject      *obj,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  EvdSocketStream *self;

  self = EVD_SOCKET_STREAM (obj);

  switch (prop_id)
    {
    case PROP_TLS_AUTOSTART:
      evd_socket_stream_set_tls_autostart (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_socket_stream_get_property (GObject    *obj,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  EvdSocketStream *self;

  self = EVD_SOCKET_STREAM (obj);

  switch (prop_id)
    {
    case PROP_TLS_AUTOSTART:
      g_value_set_boolean (value, evd_socket_stream_get_tls_autostart (self));
      break;

    case PROP_TLS_SESSION:
      g_value_set_object (value, evd_socket_stream_get_tls_session (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_socket_stream_copy_properties (EvdStream *self, EvdStream *target)
{
  evd_socket_stream_set_tls_autostart (EVD_SOCKET_STREAM (target),
                                 EVD_SOCKET_STREAM (self)->priv->tls_autostart);

  EVD_STREAM_CLASS (evd_socket_stream_parent_class)->
    copy_properties (self, target);
}

/* public methods */

EvdSocketStream *
evd_socket_stream_new (void)
{
  EvdSocketStream *self;

  self = g_object_new (EVD_TYPE_SOCKET_STREAM, NULL);

  return self;
}

void
evd_socket_stream_set_tls_autostart (EvdSocketStream *self, gboolean autostart)
{
  g_return_if_fail (EVD_IS_STREAM (self));

  self->priv->tls_autostart = autostart;
}

gboolean
evd_socket_stream_get_tls_autostart (EvdSocketStream *self)
{
  g_return_val_if_fail (EVD_IS_STREAM (self), FALSE);

  return self->priv->tls_autostart;
}

EvdTlsSession *
evd_socket_stream_get_tls_session (EvdSocketStream *self)
{
  g_return_val_if_fail (EVD_IS_STREAM (self), NULL);

  if (self->priv->tls_session == NULL)
    {
      self->priv->tls_session = evd_tls_session_new ();
      g_object_ref_sink (self->priv->tls_session);
    }

  return self->priv->tls_session;
}
