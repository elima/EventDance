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
  GQuark err_domain;

  gnutls_certificate_credentials_t cred;
  gnutls_anon_client_credentials_t anon_client_cred;
  gnutls_anon_server_credentials_t anon_server_cred;
  gboolean anonymous;

  gchar *cert_file;
  gchar *key_file;
  gchar *trust_file;

  gboolean ready;
  gboolean preparing;

  EvdTlsMode mode;
};

/* signals */
enum
{
  SIGNAL_READY,
  SIGNAL_LAST
};

static guint evd_tls_credentials_signals[SIGNAL_LAST] = { 0 };

/* properties */
enum
{
  PROP_0,
  PROP_CERT_FILE,
  PROP_KEY_FILE,
  PROP_TRUST_FILE
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

  evd_tls_credentials_signals[SIGNAL_READY] =
    g_signal_new ("ready",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdTlsCredentialsClass, ready),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

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

  g_object_class_install_property (obj_class, PROP_TRUST_FILE,
                                   g_param_spec_string ("trust-file",
                                                        "Trust file",
                                                        "Filename of the X.509 or OpenPGP trust chain to use with this credentials",
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

  priv = EVD_TLS_CREDENTIALS_GET_PRIVATE (self);
  self->priv = priv;

  priv->err_domain = g_quark_from_static_string (DOMAIN_QUARK_STRING);

  priv->cred = NULL;

  priv->cert_file = NULL;
  priv->key_file = NULL;

  priv->ready = FALSE;
  priv->preparing = FALSE;
  priv->anonymous = TRUE;
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

  if (self->priv->trust_file != NULL)
    g_free (self->priv->trust_file);

  G_OBJECT_CLASS (evd_tls_credentials_parent_class)->finalize (obj);
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

    case PROP_TRUST_FILE:
      evd_tls_credentials_set_trust_file (self, g_value_get_string (value));
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

    case PROP_TRUST_FILE:
      g_value_set_string (value, self->priv->trust_file);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static gboolean
evd_tls_credentials_prepare_finish (EvdTlsCredentials  *self,
                                    GError            **error)
{
  gint err_code;

  if (self->priv->preparing)
   return TRUE;

  if (self->priv->cert_file == NULL && self->priv->key_file == NULL)
    {
      /* no certificates specified, assume anonynous credentials */
      if (self->priv->mode == EVD_TLS_MODE_CLIENT)
        err_code =
          gnutls_anon_allocate_client_credentials (&self->priv->anon_client_cred);
      else
        err_code =
          gnutls_anon_allocate_server_credentials (&self->priv->anon_server_cred);

      if (err_code != GNUTLS_E_SUCCESS)
        {
          evd_tls_build_error (err_code, error, self->priv->err_domain);
          return FALSE;
        }

      self->priv->anonymous = TRUE;
      self->priv->ready = TRUE;
      self->priv->preparing = FALSE;

      g_signal_emit (self,
                     evd_tls_credentials_signals[SIGNAL_READY],
                     0,
                     NULL);
    }
  else if (self->priv->cert_file != NULL && self->priv->key_file != NULL)
    {
      /* TODO: Detect type of certificates: X.509 vs. OpenPGP.
         By now assume just X.509. */

      if (self->priv->cred == NULL)
        gnutls_certificate_allocate_credentials (&self->priv->cred);

      err_code = gnutls_certificate_set_x509_key_file (self->priv->cred,
                                                       self->priv->cert_file,
                                                       self->priv->key_file,
                                                       GNUTLS_X509_FMT_PEM);

      if (err_code == GNUTLS_E_SUCCESS &&
          self->priv->trust_file != NULL)
        {
          err_code =
            gnutls_certificate_set_x509_trust_file (self->priv->cred,
                                                    self->priv->trust_file,
                                                    GNUTLS_X509_FMT_PEM);

          if (err_code >= 0)
            err_code = GNUTLS_E_SUCCESS;
        }

      if (err_code != GNUTLS_E_SUCCESS)
        {
          evd_tls_build_error (err_code, error, self->priv->err_domain);
          return FALSE;
        }

      self->priv->anonymous = FALSE;
      self->priv->ready = TRUE;
      self->priv->preparing = FALSE;

      g_signal_emit (self,
                     evd_tls_credentials_signals[SIGNAL_READY],
                     0,
                     NULL);
    }
  else
    {
      /* handle error, key or cert file not specified */
      if (error != NULL)
        {
          if (self->priv->cert_file == NULL)
            *error = g_error_new (self->priv->err_domain,
                                  EVD_TLS_ERROR_INVALID_CRED_CERT,
                                  "Credentials' certificate not specified");
          else
            *error = g_error_new (self->priv->err_domain,
                                  EVD_TLS_ERROR_INVALID_CRED_KEY,
                                  "Credentials' key not specified");
        }

      return FALSE;
    }

  return TRUE;
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

void
evd_tls_credentials_set_trust_file (EvdTlsCredentials *self,
                                    const gchar       *trust_file)
{
  if (self->priv->trust_file != NULL)
    g_free (self->priv->trust_file);

  self->priv->trust_file = g_strdup (trust_file);
}

gboolean
evd_tls_credentials_ready (EvdTlsCredentials *self)
{
  g_return_val_if_fail (EVD_IS_TLS_CREDENTIALS (self), FALSE);

  return self->priv->ready;
}

gboolean
evd_tls_credentials_prepare (EvdTlsCredentials  *self,
                             EvdTlsMode          mode,
                             GError            **error)
{
  g_return_val_if_fail (EVD_IS_TLS_CREDENTIALS (self), FALSE);

  if (self->priv->preparing)
   return TRUE;

  /* TODO: if DH params are needed, request them now */

  self->priv->mode = mode;

  return evd_tls_credentials_prepare_finish (self, error);
}

gboolean
evd_tls_credentials_get_anonymous (EvdTlsCredentials *self)
{
  g_return_val_if_fail (EVD_IS_TLS_CREDENTIALS (self), FALSE);

  return self->priv->anonymous;
}

gpointer
evd_tls_credentials_get_credentials (EvdTlsCredentials *self)
{
  g_return_val_if_fail (EVD_IS_TLS_CREDENTIALS (self), NULL);

  if (self->priv->cred != NULL)
    return self->priv->cred;
  else if (self->priv->anon_server_cred != NULL)
    return self->priv->anon_server_cred;
  else
    return self->priv->anon_client_cred;
}
