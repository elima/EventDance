/*
 * evd-tls-session.c
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
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include "evd-tls-common.h"
#include "evd-tls-session.h"

#define DOMAIN_QUARK_STRING "org.eventdance.lib.tls-session"

G_DEFINE_TYPE (EvdTlsSession, evd_tls_session, G_TYPE_OBJECT)

#define EVD_TLS_SESSION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                          EVD_TYPE_TLS_SESSION, \
                                          EvdTlsSessionPrivate))

/* private data */
struct _EvdTlsSessionPrivate
{
  gnutls_session_t session;
  EvdTlsCredentials *cred;

  EvdTlsMode mode;
};


/* properties */
enum
{
  PROP_0,
  PROP_CREDENTIALS,
  PROP_MODE
};

static void     evd_tls_session_class_init         (EvdTlsSessionClass *class);
static void     evd_tls_session_init               (EvdTlsSession *self);

static void     evd_tls_session_finalize           (GObject *obj);
static void     evd_tls_session_dispose            (GObject *obj);

static void     evd_tls_session_set_property       (GObject      *obj,
                                                    guint         prop_id,
                                                    const GValue *value,
                                                    GParamSpec   *pspec);
static void     evd_tls_session_get_property       (GObject    *obj,
                                                    guint       prop_id,
                                                    GValue     *value,
                                                    GParamSpec *pspec);

static void
evd_tls_session_class_init (EvdTlsSessionClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_tls_session_dispose;
  obj_class->finalize = evd_tls_session_finalize;
  obj_class->get_property = evd_tls_session_get_property;
  obj_class->set_property = evd_tls_session_set_property;

  /* install properties */
  g_object_class_install_property (obj_class, PROP_CREDENTIALS,
                                   g_param_spec_object ("credentials",
                                                        "The SSL/TLS session's credentials",
                                                        "The certificate credentials object to use by this SSL/TLS session",
                                                        EVD_TYPE_TLS_CREDENTIALS,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_MODE,
                                   g_param_spec_uint ("mode",
                                                      "SSL/TLS session mode",
                                                      "The SSL/TLS session's mode of operation: client or server",
                                                      EVD_TLS_MODE_SERVER,
                                                      EVD_TLS_MODE_CLIENT,
                                                      EVD_TLS_MODE_SERVER,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdTlsSessionPrivate));
}

static void
evd_tls_session_init (EvdTlsSession *self)
{
  EvdTlsSessionPrivate *priv;

  evd_tls_global_init ();

  priv = EVD_TLS_SESSION_GET_PRIVATE (self);
  self->priv = priv;

  /* initialize private members */
  priv->session = NULL;
  priv->cred = NULL;

  priv->mode = EVD_TLS_MODE_SERVER;
}

static void
evd_tls_session_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_tls_session_parent_class)->dispose (obj);
}

static void
evd_tls_session_finalize (GObject *obj)
{
  EvdTlsSession *self = EVD_TLS_SESSION (obj);

  if (self->priv->session != NULL)
    gnutls_deinit (self->priv->session);

  if (self->priv->cred != NULL)
    g_object_unref (self->priv->cred);

  G_OBJECT_CLASS (evd_tls_session_parent_class)->finalize (obj);

  evd_tls_global_deinit ();
}

static void
evd_tls_session_set_property (GObject      *obj,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  EvdTlsSession *self;

  self = EVD_TLS_SESSION (obj);

  switch (prop_id)
    {
    case PROP_CREDENTIALS:
      evd_tls_session_set_credentials (self, g_value_get_object (value));
      break;

    case PROP_MODE:
      self->priv->mode = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_tls_session_get_property (GObject    *obj,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  EvdTlsSession *self;

  self = EVD_TLS_SESSION (obj);

  switch (prop_id)
    {
    case PROP_CREDENTIALS:
      g_value_set_object (value, evd_tls_session_get_credentials (self));
      break;

    case PROP_MODE:
      g_value_set_uint (value, self->priv->mode);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

/* public methods */

EvdTlsSession *
evd_tls_session_new (void)
{
  EvdTlsSession *self;

  self = g_object_new (EVD_TYPE_TLS_SESSION, NULL);

  return self;
}

void
evd_tls_session_set_credentials (EvdTlsSession     *self,
                                 EvdTlsCredentials *credentials)
{
  g_return_if_fail (EVD_IS_TLS_SESSION (self));

  if (self->priv->cred != NULL)
    g_object_unref (self->priv->cred);

  self->priv->cred = credentials;

  if (self->priv->cred != NULL)
    g_object_ref (self->priv->cred);
}

EvdTlsCredentials *
evd_tls_session_get_credentials (EvdTlsSession *self)
{
  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), NULL);

  if (self->priv->cred == NULL)
    {
      self->priv->cred = evd_tls_credentials_new ();
      g_object_ref_sink (self->priv->cred);
    }

  return self->priv->cred;
}
