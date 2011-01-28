/*
 * evd-tls-credentials.c
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

#include "evd-error.h"
#include "evd-tls-common.h"
#include "evd-tls-credentials.h"

G_DEFINE_TYPE (EvdTlsCredentials, evd_tls_credentials, G_TYPE_OBJECT)

#define EVD_TLS_CREDENTIALS_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                              EVD_TYPE_TLS_CREDENTIALS, \
                                              EvdTlsCredentialsPrivate))

/* private data */
struct _EvdTlsCredentialsPrivate
{
  gnutls_certificate_credentials_t cred;

  guint              dh_bits;
  gnutls_dh_params_t dh_params;

  gchar *cert_file;
  gchar *key_file;
  gchar *trust_file;

  gboolean ready;
  gboolean preparing;

  EvdTlsMode mode;

  EvdTlsCredentialsCertCb cert_cb;
  gpointer cert_cb_user_data;
  gnutls_retr_st *cert_cb_certs;
  gint cert_cb_result;
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
  PROP_DH_BITS,
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
  g_object_class_install_property (obj_class, PROP_DH_BITS,
                                   g_param_spec_uint ("dh-bits",
                                                      "DH parameters's depth",
                                                      "Bit depth of the Diffie-Hellman key exchange parameters to use during handshake",
                                                      0,
                                                      4096,
                                                      0,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

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

  priv->cred = NULL;

  priv->dh_bits = 0;
  priv->dh_params = NULL;

  priv->cert_file = NULL;
  priv->key_file = NULL;

  priv->ready = FALSE;
  priv->preparing = FALSE;

  priv->cert_cb = NULL;
  priv->cert_cb_user_data = NULL;
  priv->cert_cb_certs = NULL;
  priv->cert_cb_result = 0;
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

  if (self->priv->dh_params != NULL)
    gnutls_dh_params_deinit (self->priv->dh_params);

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
    case PROP_DH_BITS:
      if (g_value_get_uint (value) != self->priv->dh_bits)
        {
          self->priv->dh_bits = g_value_get_uint (value);

          if (self->priv->dh_params != NULL)
            {
              gnutls_dh_params_deinit (self->priv->dh_params);
              self->priv->dh_params = NULL;
            }

          self->priv->ready = FALSE;
        }
      break;

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
    case PROP_DH_BITS:
      g_value_set_uint (value, self->priv->dh_bits);
      break;

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

static gint
evd_tls_credentials_server_cert_cb (gnutls_session_t  session,
                                    gnutls_retr_st   *st)
{
  EvdTlsCredentials *self;
  EvdTlsSession *tls_session;

  tls_session = gnutls_transport_get_ptr (session);
  g_assert (EVD_IS_TLS_SESSION (tls_session));

  self = evd_tls_session_get_credentials (tls_session);

  g_assert (self->priv->cert_cb != NULL);

  self->priv->cert_cb_certs = st;
  self->priv->cert_cb_certs->ncerts = 0;
  self->priv->cert_cb_result = 0;

  if (! self->priv->cert_cb (self,
                             tls_session,
                             NULL,
                             NULL,
                             self->priv->cert_cb_user_data))
    {
      self->priv->cert_cb_result = -1;
    }

  self->priv->cert_cb_certs = NULL;

  return self->priv->cert_cb_result;
}

/* @TODO
static gint
evd_tls_credentials_client_cert_cb (gnutls_session_t             session,
                                    const gnutls_datum_t        *req_ca_rdn,
                                    gint                         nreqs,
                                    const gnutls_pk_algorithm_t *sign_algos,
                                    int                          sign_algos_length,
                                    gnutls_retr_st              *st)
{
}
*/

static gboolean
evd_tls_credentials_prepare_finish (EvdTlsCredentials  *self,
                                    GError            **error)
{
  if (self->priv->preparing)
   return TRUE;

  if (self->priv->cred == NULL)
    gnutls_certificate_allocate_credentials (&self->priv->cred);

  if (self->priv->cert_cb != NULL)
    {
      gnutls_certificate_server_set_retrieve_function (self->priv->cred,
                                            evd_tls_credentials_server_cert_cb);

      /* @TODO: client side cert retrieval disabled by now */
      /*
      gnutls_certificate_client_set_retrieve_function (self->priv->cred,
                                            evd_tls_credentials_client_cert_cb);
      */
    }

  if (self->priv->mode == EVD_TLS_MODE_SERVER &&
      self->priv->dh_bits != 0)
    {
      gnutls_certificate_set_dh_params (self->priv->cred,
                                        self->priv->dh_params);
    }

  self->priv->ready = TRUE;
  self->priv->preparing = FALSE;

  g_signal_emit (self,
                 evd_tls_credentials_signals[SIGNAL_READY],
                 0,
                 NULL);

  return TRUE;
}

static void
evd_tls_credentials_dh_params_ready (GObject      *source_obj,
                                     GAsyncResult *res,
                                     gpointer      user_data)
{
  EvdTlsCredentials *self = EVD_TLS_CREDENTIALS (user_data);
  GError *error = NULL;

  if ( (self->priv->dh_params =
        evd_tls_generate_dh_params_finish (res,
                                           &error)) != NULL)
    {
      evd_tls_credentials_prepare_finish (self, &error);
    }
  else
    {
      /* @TODO: handle async error */
      g_debug ("Error generating DH params: %s", error->message);
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

  self->priv->mode = mode;

  if (self->priv->mode == EVD_TLS_MODE_SERVER &&
      self->priv->dh_bits != 0 && self->priv->dh_params == NULL)
    {
      evd_tls_generate_dh_params (self->priv->dh_bits,
                                  0,
                                  evd_tls_credentials_dh_params_ready,
                                  NULL, /* @TODO: functions ignores cancellable by now */
                                  (gpointer) self);

      return TRUE;
    }
  else
    {
      return evd_tls_credentials_prepare_finish (self, error);
    }
}

gpointer
evd_tls_credentials_get_credentials (EvdTlsCredentials *self)
{
  g_return_val_if_fail (EVD_IS_TLS_CREDENTIALS (self), NULL);

  return self->priv->cred;
}

/**
 * evd_tls_credentials_set_cert_callback:
 * @self:
 * @callback: (allow-none):
 * @user_data: (allow-none):
 **/
void
evd_tls_credentials_set_cert_callback (EvdTlsCredentials       *self,
                                       EvdTlsCredentialsCertCb  callback,
                                       gpointer                 user_data)
{
  g_return_if_fail (EVD_IS_TLS_CREDENTIALS (self));

  self->priv->cert_cb = callback;
  self->priv->cert_cb_user_data = user_data;

  if (self->priv->cred != NULL)
    {
      gnutls_certificate_server_set_retrieve_function (self->priv->cred,
                                            evd_tls_credentials_server_cert_cb);

      /* @TODO: client cert retrieval disabled by now */
      /*
      gnutls_certificate_client_set_retrieve_function (self->priv->cred,
                                            evd_tls_credentials_client_cert_cb);
      */
    }
}
