/*
 * evd-tls-credentials.c
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

#include "evd-tls-common.h"
#include "evd-tls-credentials.h"

#define DOMAIN_QUARK_STRING "org.eventdance.lib.tls-credentials"

G_DEFINE_TYPE (EvdTlsCredentials, evd_tls_credentials, G_TYPE_OBJECT)

#define EVD_TLS_CREDENTIALS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                              EVD_TYPE_TLS_CREDENTIALS, \
                                              EvdTlsCredentialsPrivate))

/* private data */
struct _EvdTlsCredentialsPrivate
{
  gnutls_certificate_credentials_t cred;

  gchar *cert_file;
  gchar *key_file;
};


/* properties */
enum
{
  PROP_0,
};

static void     evd_tls_credentials_class_init         (EvdTlsCredentialsClass *class);
static void     evd_tls_credentials_init               (EvdTlsCredentials *self);

static void     evd_tls_credentials_finalize           (GObject *obj);
static void     evd_tls_credentials_dispose            (GObject *obj);

static void     evd_tls_credentials_set_property       (GObject      *obj,
                                                        guint         prop_id,
                                                        const GValue *value,
                                                        GParamSpec   *pspec);
static void     evd_tls_credentials_get_property       (GObject    *obj,
                                                        guint       prop_id,
                                                        GValue     *value,
                                                        GParamSpec *pspec);

static void
evd_tls_credentials_class_init (EvdTlsCredentialsClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_tls_credentials_dispose;
  obj_class->finalize = evd_tls_credentials_finalize;
  obj_class->get_property = evd_tls_credentials_get_property;
  obj_class->set_property = evd_tls_credentials_set_property;

  /* install properties */

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdTlsCredentialsPrivate));
}

static void
evd_tls_credentials_init (EvdTlsCredentials *self)
{
  EvdTlsCredentialsPrivate *priv;

  evd_tls_global_init ();

  priv = EVD_TLS_CREDENTIALS_GET_PRIVATE (self);
  self->priv = priv;

  /* initialize private members */
  priv->cred = NULL;

  priv->cert_file = NULL;
  priv->key_file = NULL;
}

static void
evd_tls_credentials_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_tls_credentials_parent_class)->dispose (obj);
}

static void
evd_tls_credentials_finalize (GObject *obj)
{
  EvdTlsCredentials *self = EVD_TLS_CREDENTIALS (obj);

  if (self->priv->cred != NULL)
    gnutls_certificate_free_credentials (self->priv->cred);

  G_OBJECT_CLASS (evd_tls_credentials_parent_class)->finalize (obj);

  evd_tls_global_deinit ();
}

static void
evd_tls_credentials_set_property (GObject      *obj,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  EvdTlsCredentials *self;

  self = EVD_TLS_CREDENTIALS (obj);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_tls_credentials_get_property (GObject    *obj,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EvdTlsCredentials *self;

  self = EVD_TLS_CREDENTIALS (obj);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

/* public methods */

EvdTlsCredentials *
evd_tls_credentials_new (void)
{
  EvdTlsCredentials *self;

  self = g_object_new (EVD_TYPE_TLS_CREDENTIALS, NULL);

  return self;
}
