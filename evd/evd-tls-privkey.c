/*
 * evd-tls-privkey.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009-2013, Igalia S.L.
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

#include <gnutls/x509.h>
#include <gnutls/openpgp.h>
#include <gcrypt.h>

#include "evd-tls-privkey.h"

#include "evd-error.h"
#include "evd-tls-common.h"

G_DEFINE_TYPE (EvdTlsPrivkey, evd_tls_privkey, G_TYPE_OBJECT)

#define EVD_TLS_PRIVKEY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                          EVD_TYPE_TLS_PRIVKEY, \
                                          EvdTlsPrivkeyPrivate))

/* private data */
struct _EvdTlsPrivkeyPrivate
{
  gnutls_x509_privkey_t x509_privkey;
  gnutls_openpgp_privkey_t openpgp_privkey;

  EvdTlsCertificateType type;

  gboolean native_stolen;
};


/* properties */
enum
{
  PROP_0,
  PROP_TYPE
};

static void     evd_tls_privkey_class_init         (EvdTlsPrivkeyClass *class);
static void     evd_tls_privkey_init               (EvdTlsPrivkey *self);

static void     evd_tls_privkey_finalize           (GObject *obj);
static void     evd_tls_privkey_dispose            (GObject *obj);

static void     evd_tls_privkey_get_property       (GObject    *obj,
                                                    guint       prop_id,
                                                    GValue     *value,
                                                    GParamSpec *pspec);

static void     evd_tls_privkey_cleanup            (EvdTlsPrivkey *self);

static void
evd_tls_privkey_class_init (EvdTlsPrivkeyClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_tls_privkey_dispose;
  obj_class->finalize = evd_tls_privkey_finalize;
  obj_class->get_property = evd_tls_privkey_get_property;

  /* install properties */
  g_object_class_install_property (obj_class, PROP_TYPE,
                                   g_param_spec_uint ("type",
                                                      "Privkey type",
                                                      "The type of privkey (X.509 or OPENPGP)",
                                                      EVD_TLS_CERTIFICATE_TYPE_UNKNOWN,
                                                      EVD_TLS_CERTIFICATE_TYPE_OPENPGP,
                                                      EVD_TLS_CERTIFICATE_TYPE_UNKNOWN,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_STATIC_STRINGS));

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdTlsPrivkeyPrivate));
}

static void
evd_tls_privkey_init (EvdTlsPrivkey *self)
{
  EvdTlsPrivkeyPrivate *priv;

  priv = EVD_TLS_PRIVKEY_GET_PRIVATE (self);
  self->priv = priv;

  priv->x509_privkey = NULL;
  priv->openpgp_privkey = NULL;

  self->priv->type = EVD_TLS_CERTIFICATE_TYPE_UNKNOWN;

  priv->native_stolen = FALSE;
}

static void
evd_tls_privkey_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_tls_privkey_parent_class)->dispose (obj);
}

static void
evd_tls_privkey_finalize (GObject *obj)
{
  EvdTlsPrivkey *self = EVD_TLS_PRIVKEY (obj);

  evd_tls_privkey_cleanup (self);

  G_OBJECT_CLASS (evd_tls_privkey_parent_class)->finalize (obj);
}

static void
evd_tls_privkey_get_property (GObject    *obj,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EvdTlsPrivkey *self;

  self = EVD_TLS_PRIVKEY (obj);

  switch (prop_id)
    {
    case PROP_TYPE:
      g_value_set_uint (value, self->priv->type);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_tls_privkey_cleanup (EvdTlsPrivkey *self)
{
  if (self->priv->x509_privkey != NULL)
    {
      if (! self->priv->native_stolen)
        gnutls_x509_privkey_deinit (self->priv->x509_privkey);
      self->priv->x509_privkey = NULL;
    }

  if (self->priv->openpgp_privkey != NULL)
    {
      if (! self->priv->native_stolen)
        gnutls_openpgp_privkey_deinit (self->priv->openpgp_privkey);
      self->priv->openpgp_privkey = NULL;
    }

  self->priv->native_stolen = FALSE;

  self->priv->type = EVD_TLS_CERTIFICATE_TYPE_UNKNOWN;
}

static EvdTlsCertificateType
evd_tls_privkey_detect_type (const gchar *raw_data)
{
  if (g_strstr_len (raw_data, 26, "BEGIN RSA PRIVATE KEY") != NULL)
    return EVD_TLS_CERTIFICATE_TYPE_X509;
  else if (g_strstr_len (raw_data, 32, "BEGIN PGP PRIVATE KEY BLOCK") != NULL)
    return EVD_TLS_CERTIFICATE_TYPE_OPENPGP;
  else
    return EVD_TLS_CERTIFICATE_TYPE_UNKNOWN;
}

static gboolean
evd_tls_privkey_import_x509 (EvdTlsPrivkey      *self,
                             const gchar            *raw_data,
                             gsize                   size,
                             gnutls_x509_crt_fmt_t   format,
                             GError                **error)
{
  gint err_code;
  gnutls_x509_privkey_t privkey;

  err_code = gnutls_x509_privkey_init (&privkey);

  if (err_code == GNUTLS_E_SUCCESS)
    {
      gnutls_datum_t datum = { NULL, 0 };

      datum.data = (void *) raw_data;
      datum.size = size;

      err_code = gnutls_x509_privkey_import (privkey, &datum, format);
    }

  if (! evd_error_propagate_gnutls (err_code, error))
    {
      evd_tls_privkey_cleanup (self);

      self->priv->x509_privkey = privkey;
      self->priv->type = EVD_TLS_CERTIFICATE_TYPE_X509;

      return TRUE;
    }

  return FALSE;
}

static void
evd_tls_privkey_import_from_file_thread (GSimpleAsyncResult *res,
                                         GObject            *object,
                                         GCancellable       *cancellable)
{
  EvdTlsPrivkey *self = EVD_TLS_PRIVKEY (object);
  gchar *filename;
  gchar *content;
  gsize size;
  GError *error = NULL;

  filename = g_simple_async_result_get_op_res_gpointer (res);

  if (! g_file_get_contents (filename, &content, &size, &error) ||
      ! evd_tls_privkey_import (self, content, size, &error))
    {
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);
    }

  g_free (content);
  g_free (filename);
  g_object_unref (res);
}

/* public methods */

EvdTlsPrivkey *
evd_tls_privkey_new (void)
{
  EvdTlsPrivkey *self;

  self = g_object_new (EVD_TYPE_TLS_PRIVKEY, NULL);

  return self;
}

gboolean
evd_tls_privkey_import (EvdTlsPrivkey  *self,
                        const gchar    *raw_data,
                        gsize           size,
                        GError        **error)
{
  EvdTlsCertificateType type;

  g_return_val_if_fail (EVD_IS_TLS_PRIVKEY (self), FALSE);
  g_return_val_if_fail (raw_data != NULL, FALSE);

  type = evd_tls_privkey_detect_type (raw_data);
  switch (type)
    {
    case EVD_TLS_CERTIFICATE_TYPE_X509:
      {
        if (evd_tls_privkey_import_x509 (self,
                                             raw_data,
                                             size,
                                             GNUTLS_X509_FMT_PEM,
                                             error))
          {
            return TRUE;
          }

        break;
      }

    case EVD_TLS_CERTIFICATE_TYPE_OPENPGP:
      {
        gint err_code;
        gnutls_openpgp_privkey_t privkey;

        err_code = gnutls_openpgp_privkey_init (&privkey);

        if (err_code == GNUTLS_E_SUCCESS)
          {
            gnutls_datum_t datum = { NULL, 0 };

            datum.data = (void *) raw_data;
            datum.size = size;

            err_code = gnutls_openpgp_privkey_import (privkey,
                                                      &datum,
                                                      GNUTLS_OPENPGP_FMT_BASE64,
                                                      NULL,
                                                      0);
          }

        if (! evd_error_propagate_gnutls (err_code, error))
          {
            evd_tls_privkey_cleanup (self);

            self->priv->openpgp_privkey = privkey;
            self->priv->type = EVD_TLS_CERTIFICATE_TYPE_OPENPGP;

            return TRUE;
          }

        break;
      }

    default:
      {
        /* probe DER format */
        if (evd_tls_privkey_import_x509 (self,
                                         raw_data,
                                         size,
                                         GNUTLS_X509_FMT_DER,
                                         NULL))
          {
            return TRUE;
          }
        else
          {
            g_set_error_literal (error,
                                 G_IO_ERROR,
                                 G_IO_ERROR_INVALID_DATA,
                                 "Unable to detect privkey type when trying to import");
          }

        break;
      }
    };

  return FALSE;
}

void
evd_tls_privkey_import_from_file (EvdTlsPrivkey       *self,
                                  const gchar         *filename,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  GSimpleAsyncResult *res;

  g_return_if_fail (EVD_IS_TLS_PRIVKEY (self));
  g_return_if_fail (filename != NULL);

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_tls_privkey_import_from_file);

  g_simple_async_result_set_op_res_gpointer (res, g_strdup (filename), NULL);

  g_simple_async_result_run_in_thread (res,
                                       evd_tls_privkey_import_from_file_thread,
                                       G_PRIORITY_DEFAULT,
                                       cancellable);
}

gboolean
evd_tls_privkey_import_from_file_finish (EvdTlsPrivkey  *self,
                                         GAsyncResult   *result,
                                         GError        **error)
{
  g_return_val_if_fail (EVD_IS_TLS_PRIVKEY (self), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                           G_OBJECT (self),
                                           evd_tls_privkey_import_from_file),
                        FALSE);

  return ! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                                  error);
}

/**
 * evd_tls_privkey_get_native:
 *
 * Returns: (transfer none):
 **/
gpointer
evd_tls_privkey_get_native (EvdTlsPrivkey *self)
{
  g_return_val_if_fail (EVD_IS_TLS_PRIVKEY (self), NULL);

  if (self->priv->type == EVD_TLS_CERTIFICATE_TYPE_X509)
    return self->priv->x509_privkey;
  else if (self->priv->type == EVD_TLS_CERTIFICATE_TYPE_OPENPGP)
    return self->priv->openpgp_privkey;
  else
    return NULL;
}

/**
 * evd_tls_privkey_steal_native:
 *
 * Returns: (transfer full):
 **/
gpointer
evd_tls_privkey_steal_native (EvdTlsPrivkey *self)
{
  gpointer native;

  g_return_val_if_fail (EVD_IS_TLS_PRIVKEY (self), NULL);

  native = evd_tls_privkey_get_native (self);

  if (native != NULL)
    self->priv->native_stolen = TRUE;

  return native;
}

/**
 * evd_tls_privkey_get_pki_key:
 *
 * Returns: (transfer full):
 **/
EvdPkiPrivkey *
evd_tls_privkey_get_pki_key (EvdTlsPrivkey *self, GError **error)
{
  EvdPkiPrivkey *key = NULL;
  gnutls_privkey_t privkey = NULL;
  gint err_code;

  g_return_val_if_fail (EVD_IS_TLS_PRIVKEY (self), NULL);

  if (self->priv->type == EVD_TLS_CERTIFICATE_TYPE_UNKNOWN)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Failed to get key from not initialized private key");
      return NULL;
    }

  err_code = gnutls_privkey_init (&privkey);
  if (evd_error_propagate_gnutls (err_code, error))
    return NULL;

  if (self->priv->type == EVD_TLS_CERTIFICATE_TYPE_X509)
    err_code = gnutls_privkey_import_x509 (privkey,
                                           self->priv->x509_privkey,
                                           GNUTLS_PRIVKEY_IMPORT_COPY);
  else if (self->priv->type == EVD_TLS_CERTIFICATE_TYPE_OPENPGP)
    err_code = gnutls_privkey_import_openpgp (privkey,
                                              self->priv->openpgp_privkey,
                                              GNUTLS_PRIVKEY_IMPORT_COPY);

  if (evd_error_propagate_gnutls (err_code, error))
    {
      gnutls_privkey_deinit (privkey);
      return NULL;
    }
  else
    {
      key = evd_pki_privkey_new ();
      if (! evd_pki_privkey_import_native (key, privkey, error))
        {
          gnutls_privkey_deinit (privkey);
          g_object_unref (key);
          key = NULL;
        }
    }

  return key;
}
