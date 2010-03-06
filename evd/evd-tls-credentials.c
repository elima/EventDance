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
  PROP_CERT_FILE,
  PROP_KEY_FILE
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
  g_object_class_install_property (obj_class, PROP_CERT_FILE,
                                   g_param_spec_string ("cert-file",
                                                        "Certificate file",
                                                        "Filename of the X.509 or OpenPGP certificate file to use with this credentials",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_KEY_FILE,
                                   g_param_spec_string ("key-file",
                                                        "Private key file",
                                                        "Filename of the X.509 or OpenPGP private key file to use with this credentials",
                                                        NULL,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

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

  if (self->priv->cert_file != NULL)
    g_free (self->priv->cert_file);

  if (self->priv->key_file != NULL)
    g_free (self->priv->key_file);

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
    case PROP_CERT_FILE:
      evd_tls_credentials_set_cert_file (self, g_value_get_string (value));
      break;

    case PROP_KEY_FILE:
      evd_tls_credentials_set_key_file (self, g_value_get_string (value));
      break;

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
    case PROP_CERT_FILE:
      g_value_set_string (value, self->priv->cert_file);
      break;

    case PROP_KEY_FILE:
      g_value_set_string (value, self->priv->key_file);
      break;

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

void
evd_tls_credentials_set_cert_file (EvdTlsCredentials *self,
                                   const gchar       *cert_file)
{
  if (self->priv->cert_file != NULL)
    g_free (self->priv->cert_file);

  self->priv->cert_file = g_strdup (cert_file);
}

void
evd_tls_credentials_set_key_file (EvdTlsCredentials *self,
                                  const gchar       *key_file)
{
  if (self->priv->key_file != NULL)
    g_free (self->priv->key_file);

  self->priv->key_file = g_strdup (key_file);
}
