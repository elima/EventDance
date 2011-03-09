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

#include <gnutls/openpgp.h>

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

  gboolean ready;
  gboolean preparing;

  EvdTlsCredentialsCertCb cert_cb;
  gpointer cert_cb_user_data;
  gnutls_retr_st *cert_cb_certs;
  gint cert_cb_result;
};

struct CertData
{
  EvdTlsCertificate *cert;
  EvdTlsPrivkey *key;
  GError *error;
  gint ready;
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
  PROP_DH_BITS
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

  priv->dh_params = NULL;

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

  if (self->priv->dh_bits != 0)
    gnutls_certificate_set_dh_params (self->priv->cred, self->priv->dh_params);

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

static void
evd_tls_certificate_real_add_cert_from_file (GSimpleAsyncResult *res,
                                             struct CertData    *data)
{
  EvdTlsCredentials *self;

  self = EVD_TLS_CREDENTIALS (g_async_result_get_source_object (G_ASYNC_RESULT (res)));

  if (data->error == NULL)
    evd_tls_credentials_add_certificate (self,
                                         data->cert,
                                         data->key,
                                         &data->error);

  if (data->error != NULL)
    {
      g_simple_async_result_set_from_error (res, data->error);
      g_error_free (data->error);
    }

  if (data->cert != NULL)
    g_object_unref (data->cert);

  if (data->key != NULL)
    g_object_unref (data->key);

  g_slice_free (struct CertData, data);

  g_simple_async_result_complete (res);
  g_object_unref (res);
}

static void
evd_tls_certificate_on_cert_imported (GObject      *obj,
                                      GAsyncResult *res,
                                      gpointer      user_data)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
  GError *error = NULL;
  struct CertData *data;

  data = g_simple_async_result_get_op_res_gpointer (result);

  data->ready++;

  if (data->error == NULL)
    {
      if (! evd_tls_certificate_import_from_file_finish (EVD_TLS_CERTIFICATE (obj),
                                                         res,
                                                         &error))
        {
          g_simple_async_result_set_from_error (result, error);
          g_object_unref (obj);
          data->error = error;
        }
      else
        {
          data->cert = EVD_TLS_CERTIFICATE (obj);
        }
    }

  if (data->ready == 2)
    evd_tls_certificate_real_add_cert_from_file (result, data);
}

static void
evd_tls_certificate_on_privkey_imported (GObject      *obj,
                                         GAsyncResult *res,
                                         gpointer      user_data)
{
  GSimpleAsyncResult *result = G_SIMPLE_ASYNC_RESULT (user_data);
  GError *error = NULL;
  struct CertData *data;

  data = g_simple_async_result_get_op_res_gpointer (result);

  data->ready++;

  if (data->error == NULL)
    {
      if (! evd_tls_privkey_import_from_file_finish (EVD_TLS_PRIVKEY (obj),
                                                     res,
                                                     &error))
        {
          g_simple_async_result_set_from_error (result, error);
          g_object_unref (obj);
          data->error = error;
        }
      else
        {
          data->key = EVD_TLS_PRIVKEY (obj);
        }
    }

  if (data->ready == 2)
    evd_tls_certificate_real_add_cert_from_file (result, data);
}

/* public methods */

EvdTlsCredentials *
evd_tls_credentials_new (void)
{
  EvdTlsCredentials *self;

  self = g_object_new (EVD_TYPE_TLS_CREDENTIALS, NULL);

  return self;
}

gboolean
evd_tls_credentials_ready (EvdTlsCredentials *self)
{
  g_return_val_if_fail (EVD_IS_TLS_CREDENTIALS (self), FALSE);

  return self->priv->ready;
}

gboolean
evd_tls_credentials_prepare (EvdTlsCredentials  *self,
                             GError            **error)
{
  g_return_val_if_fail (EVD_IS_TLS_CREDENTIALS (self), FALSE);

  if (self->priv->preparing)
   return TRUE;

  if (self->priv->dh_bits != 0 && self->priv->dh_params == NULL)
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

  if (self->priv->cred == NULL)
    gnutls_certificate_allocate_credentials (&self->priv->cred);

  gnutls_certificate_server_set_retrieve_function (self->priv->cred,
                                            evd_tls_credentials_server_cert_cb);

  /* @TODO: client cert retrieval disabled by now */
  /*
    gnutls_certificate_client_set_retrieve_function (self->priv->cred,
    evd_tls_credentials_client_cert_cb);
  */
}

gboolean
evd_tls_credentials_add_certificate (EvdTlsCredentials  *self,
                                     EvdTlsCertificate  *cert,
                                     EvdTlsPrivkey      *privkey,
                                     GError            **error)
{
  gpointer _cert;
  gpointer _privkey;
  EvdTlsCertificateType cert_type;
  EvdTlsCertificateType key_type;

  g_return_val_if_fail (EVD_IS_TLS_CREDENTIALS (self), FALSE);
  g_return_val_if_fail (EVD_IS_TLS_CERTIFICATE (cert), FALSE);
  g_return_val_if_fail (EVD_IS_TLS_PRIVKEY (privkey), FALSE);

  g_object_get (cert, "type", &cert_type, NULL);
  g_object_get (privkey, "type", &key_type, NULL);
  if (cert_type == EVD_TLS_CERTIFICATE_TYPE_UNKNOWN ||
      key_type == EVD_TLS_CERTIFICATE_TYPE_UNKNOWN)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "Invalid certificate or key type");
      return FALSE;
    }

  if (cert_type != key_type)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "Certificate and private key do not match type");
      return FALSE;
    }

  _cert = evd_tls_certificate_get_certificate (cert);
  _privkey = evd_tls_privkey_get_privkey (privkey);

  if (_cert == NULL || _privkey == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_INVALID_ARGUMENT,
                   "Certificate or private key not initialized");
      return FALSE;
    }

  if (self->priv->cert_cb_certs != NULL)
    {
      self->priv->cert_cb_certs->ncerts = 1;
      self->priv->cert_cb_certs->deinit_all = 0;

      if (cert_type == EVD_TLS_CERTIFICATE_TYPE_X509)
        {
          self->priv->cert_cb_certs->type = GNUTLS_CRT_X509;
          self->priv->cert_cb_certs->cert.x509 = (gnutls_x509_crt_t *) &_cert;
          self->priv->cert_cb_certs->key.x509 = (gnutls_x509_privkey_t) _privkey;
        }
      else
        {
          self->priv->cert_cb_certs->type = GNUTLS_CRT_OPENPGP;
          self->priv->cert_cb_certs->cert.pgp = (gnutls_openpgp_crt_t) _cert;
          self->priv->cert_cb_certs->key.pgp = (gnutls_openpgp_privkey_t) _privkey;
        }
    }
  else
    {
      gint err_code;

      if (self->priv->cred == NULL)
        gnutls_certificate_allocate_credentials (&self->priv->cred);

      if (cert_type == EVD_TLS_CERTIFICATE_TYPE_X509)
        {
          err_code = gnutls_certificate_set_x509_key (self->priv->cred,
                                                      (gnutls_x509_crt_t *) &_cert,
                                                      1,
                                                      (gnutls_x509_privkey_t) _privkey);
        }
      else
        {
          err_code = gnutls_certificate_set_openpgp_key (self->priv->cred,
                                                         (gnutls_openpgp_crt_t) _cert,
                                                         (gnutls_openpgp_privkey_t) _privkey);
        }

      if (err_code != GNUTLS_E_SUCCESS)
        {
          evd_tls_build_error (err_code, error);
          return FALSE;
        }
    }

  return TRUE;
}

void
evd_tls_credentials_add_certificate_from_file (EvdTlsCredentials   *self,
                                               const gchar         *cert_file,
                                               const gchar         *key_file,
                                               GCancellable        *cancellable,
                                               GAsyncReadyCallback  callback,
                                               gpointer             user_data)
{
  GSimpleAsyncResult *res;
  struct CertData *data;
  EvdTlsCertificate *cert;
  EvdTlsPrivkey *privkey;

  g_return_if_fail (EVD_IS_TLS_CREDENTIALS (self));
  g_return_if_fail (cert_file != NULL);
  g_return_if_fail (key_file != NULL);

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_tls_credentials_add_certificate_from_file);

  data = g_slice_new0 (struct CertData);

  g_simple_async_result_set_op_res_gpointer (res, data, NULL);

  cert = evd_tls_certificate_new ();
  evd_tls_certificate_import_from_file (cert,
                                        cert_file,
                                        cancellable,
                                        evd_tls_certificate_on_cert_imported,
                                        res);

  privkey = evd_tls_privkey_new ();
  evd_tls_privkey_import_from_file (privkey,
                                    key_file,
                                    cancellable,
                                    evd_tls_certificate_on_privkey_imported,
                                    res);
}

gboolean
evd_tls_credentials_add_certificate_from_file_finish (EvdTlsCredentials  *self,
                                                      GAsyncResult       *result,
                                                      GError            **error)
{
  g_return_val_if_fail (EVD_IS_TLS_CREDENTIALS (self), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                 G_OBJECT (self),
                                 evd_tls_credentials_add_certificate_from_file),
                        FALSE);

  return
    ! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                             error);
}
