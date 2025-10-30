/*
 * evd-tls-certificate.c
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
#include <gnutls/abstract.h>

#include "evd-error.h"
#include "evd-tls-common.h"
#include "evd-tls-certificate.h"

/* private data */
struct _EvdTlsCertificatePrivate
{
  gnutls_x509_crt_t    x509_cert;

  EvdTlsCertificateType type;

  gboolean native_stolen;
};

G_DEFINE_TYPE_WITH_PRIVATE (EvdTlsCertificate,
                            evd_tls_certificate,
                            G_TYPE_OBJECT)


/* properties */
enum
{
  PROP_0,
  PROP_TYPE
};

static void     evd_tls_certificate_class_init         (EvdTlsCertificateClass *class);
static void     evd_tls_certificate_init               (EvdTlsCertificate *self);

static void     evd_tls_certificate_finalize           (GObject *obj);
static void     evd_tls_certificate_dispose            (GObject *obj);

static void     evd_tls_certificate_get_property       (GObject    *obj,
                                                        guint       prop_id,
                                                        GValue     *value,
                                                        GParamSpec *pspec);

static void     evd_tls_certificate_cleanup            (EvdTlsCertificate *self);

static void
evd_tls_certificate_class_init (EvdTlsCertificateClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_tls_certificate_dispose;
  obj_class->finalize = evd_tls_certificate_finalize;
  obj_class->get_property = evd_tls_certificate_get_property;

  /* install properties */
  g_object_class_install_property (obj_class, PROP_TYPE,
                                   g_param_spec_uint ("type",
                                                      "Certificate type",
                                                      "The type of certificate",
                                                      EVD_TLS_CERTIFICATE_TYPE_UNKNOWN,
                                                      EVD_TLS_CERTIFICATE_TYPE_X509,
                                                      EVD_TLS_CERTIFICATE_TYPE_UNKNOWN,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_STATIC_STRINGS));
}

static void
evd_tls_certificate_init (EvdTlsCertificate *self)
{
  EvdTlsCertificatePrivate *priv;

  priv = evd_tls_certificate_get_instance_private (self);
  self->priv = priv;

  priv->x509_cert = NULL;

  self->priv->type = EVD_TLS_CERTIFICATE_TYPE_UNKNOWN;

  priv->native_stolen = FALSE;
}

static void
evd_tls_certificate_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_tls_certificate_parent_class)->dispose (obj);
}

static void
evd_tls_certificate_finalize (GObject *obj)
{
  EvdTlsCertificate *self = EVD_TLS_CERTIFICATE (obj);

  evd_tls_certificate_cleanup (self);

  G_OBJECT_CLASS (evd_tls_certificate_parent_class)->finalize (obj);
}

static void
evd_tls_certificate_get_property (GObject    *obj,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EvdTlsCertificate *self;

  self = EVD_TLS_CERTIFICATE (obj);

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
evd_tls_certificate_cleanup (EvdTlsCertificate *self)
{
  if (self->priv->x509_cert != NULL)
    {
      if (! self->priv->native_stolen)
        gnutls_x509_crt_deinit (self->priv->x509_cert);
      self->priv->x509_cert = NULL;
    }

  self->priv->native_stolen = FALSE;

  self->priv->type = EVD_TLS_CERTIFICATE_TYPE_UNKNOWN;
}

static EvdTlsCertificateType
evd_tls_certificate_detect_type (const gchar *raw_data)
{
  if (g_strstr_len (raw_data, 22, "BEGIN CERTIFICATE") != NULL)
    return EVD_TLS_CERTIFICATE_TYPE_X509;
  else
    return EVD_TLS_CERTIFICATE_TYPE_UNKNOWN;
}

static gboolean
evd_tls_certificate_import_x509 (EvdTlsCertificate      *self,
                                 const gchar            *raw_data,
                                 gsize                   size,
                                 gnutls_x509_crt_fmt_t   format,
                                 GError                **error)
{
  gint err_code;
  gnutls_x509_crt_t cert;

  err_code = gnutls_x509_crt_init (&cert);

  if (err_code == GNUTLS_E_SUCCESS)
    {
      gnutls_datum_t datum = { NULL, 0 };

      datum.data = (void *) raw_data;
      datum.size = size;

      err_code = gnutls_x509_crt_import (cert, &datum, format);
    }

  if (! evd_error_propagate_gnutls (err_code, error))
    {
      evd_tls_certificate_cleanup (self);

      self->priv->x509_cert = cert;
      self->priv->type = EVD_TLS_CERTIFICATE_TYPE_X509;

      return TRUE;
    }

  return FALSE;
}

static void
evd_tls_certificate_import_from_file_thread (GTask        *task,
                                             gpointer      source_object,
                                             gpointer      task_data,
                                             GCancellable *cancellable)
{
  EvdTlsCertificate *self = EVD_TLS_CERTIFICATE (source_object);
  gchar *filename;
  gchar *content = NULL;
  gsize size;
  GError *error = NULL;

  filename = task_data;

  if (! g_file_get_contents (filename, &content, &size, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  if (! evd_tls_certificate_import (self, content, size, &error))
    {
      g_free (content);
      g_task_return_error (task, error);
      return;
    }

  g_free (content);
  g_task_return_boolean (task, TRUE);
}

/* public methods */

EvdTlsCertificate *
evd_tls_certificate_new (void)
{
  EvdTlsCertificate *self;

  self = g_object_new (EVD_TYPE_TLS_CERTIFICATE, NULL);

  return self;
}

gboolean
evd_tls_certificate_import (EvdTlsCertificate  *self,
                            const gchar        *raw_data,
                            gsize               size,
                            GError            **error)
{
  EvdTlsCertificateType type;

  g_return_val_if_fail (EVD_IS_TLS_CERTIFICATE (self), FALSE);
  g_return_val_if_fail (raw_data != NULL, FALSE);

  type = evd_tls_certificate_detect_type (raw_data);
  switch (type)
    {
    case EVD_TLS_CERTIFICATE_TYPE_X509:
      {
        if (evd_tls_certificate_import_x509 (self,
                                             raw_data,
                                             size,
                                             GNUTLS_X509_FMT_PEM,
                                             error))
          {
            return TRUE;
          }

        break;
      }

    default:
      {
        /* probe DER format */
        if (evd_tls_certificate_import_x509 (self,
                                             raw_data,
                                             size,
                                             GNUTLS_X509_FMT_DER,
                                             NULL))
          {
            return TRUE;
          }
        else
          {
            if (error != NULL)
              *error = g_error_new (G_IO_ERROR,
                                    G_IO_ERROR_INVALID_DATA,
                                    "Unable to detect certificate type when trying to import");
          }

        break;
      }
    };

  return FALSE;
}

void
evd_tls_certificate_import_from_file (EvdTlsCertificate   *self,
                                      const gchar         *filename,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
  GTask *task;

  g_return_if_fail (EVD_IS_TLS_CERTIFICATE (self));
  g_return_if_fail (filename != NULL);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, evd_tls_certificate_import_from_file);

  g_task_set_task_data (task, g_strdup (filename), g_free);

  g_task_run_in_thread (task,
                        evd_tls_certificate_import_from_file_thread);
  g_object_unref (task);
}

gboolean
evd_tls_certificate_import_from_file_finish (EvdTlsCertificate  *self,
                                             GAsyncResult       *result,
                                             GError            **error)
{
  GTask *task = G_TASK (result);

  g_return_val_if_fail (EVD_IS_TLS_CERTIFICATE (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (task) == evd_tls_certificate_import_from_file,
                        FALSE);

  return g_task_propagate_boolean (task, error);
}

/**
 * evd_tls_certificate_get_native:
 *
 * Returns: (transfer none):
 **/
gpointer
evd_tls_certificate_get_native (EvdTlsCertificate *self)
{
  g_return_val_if_fail (EVD_IS_TLS_CERTIFICATE (self), NULL);

  if (self->priv->type == EVD_TLS_CERTIFICATE_TYPE_X509)
    return self->priv->x509_cert;
  else
    return NULL;
}

/**
 * evd_tls_certificate_steal_native:
 *
 * Returns: (transfer full):
 **/
gpointer
evd_tls_certificate_steal_native (EvdTlsCertificate *self)
{
  gpointer native;

  g_return_val_if_fail (EVD_IS_TLS_CERTIFICATE (self), NULL);

  native = evd_tls_certificate_get_native (self);

  if (native != NULL)
    self->priv->native_stolen = TRUE;

  return native;
}

gchar *
evd_tls_certificate_get_dn (EvdTlsCertificate *self, GError **error)
{
  gchar *dn = NULL;
  gint ret;

  g_return_val_if_fail (EVD_IS_TLS_CERTIFICATE (self), NULL);

  switch (self->priv->type)
    {
    case EVD_TLS_CERTIFICATE_TYPE_X509:
      {
        gchar *buf = NULL;
        gsize size = 0;

        ret = gnutls_x509_crt_get_dn (self->priv->x509_cert, NULL, &size);
        if (ret == GNUTLS_E_SHORT_MEMORY_BUFFER)
          {
            buf = g_new (gchar, size);
            ret = gnutls_x509_crt_get_dn (self->priv->x509_cert, buf, &size);
            if (ret == GNUTLS_E_SUCCESS)
              dn = buf;
          }
        else
          {
            evd_error_propagate_gnutls (ret, error);
          }

        break;
      }

    default:
      if (error != NULL)
        *error = g_error_new (G_IO_ERROR,
                              G_IO_ERROR_NOT_INITIALIZED,
                              "Certificate not initialized when requesting 'dn'");

      break;
    };

  return dn;
}

time_t
evd_tls_certificate_get_expiration_time (EvdTlsCertificate  *self,
                                         GError            **error)
{
  time_t time = -1;

  g_return_val_if_fail (EVD_IS_TLS_CERTIFICATE (self), -1);

  switch (self->priv->type)
    {
    case EVD_TLS_CERTIFICATE_TYPE_X509:
      {
        time = gnutls_x509_crt_get_expiration_time (self->priv->x509_cert);
        if (time == -1 && error != NULL)
          *error = g_error_new (G_IO_ERROR,
                                G_IO_ERROR_INVALID_DATA,
                                "Failed to obtain expiration time from X.509 certificate");

        break;
      }

    default:
      if (error != NULL)
        *error = g_error_new (G_IO_ERROR,
                              G_IO_ERROR_NOT_INITIALIZED,
                              "Certificate not initialized when requesting expiration time");

      break;
    };

  return time;
}

time_t
evd_tls_certificate_get_activation_time (EvdTlsCertificate  *self,
                                         GError            **error)
{
  time_t time = -1;

  g_return_val_if_fail (EVD_IS_TLS_CERTIFICATE (self), -1);

  switch (self->priv->type)
    {
    case EVD_TLS_CERTIFICATE_TYPE_X509:
      {
        time = gnutls_x509_crt_get_activation_time (self->priv->x509_cert);
        if (time == -1 && error != NULL)
          *error = g_error_new (G_IO_ERROR,
                                G_IO_ERROR_INVALID_DATA,
                                "Failed to obtain activation time from X.509 certificate");

        break;
      }

    default:
      if (error != NULL)
        *error = g_error_new (G_IO_ERROR,
                              G_IO_ERROR_NOT_INITIALIZED,
                              "Certificate not initialized when requesting activation time");

      break;
    };

  return time;
}

gint
evd_tls_certificate_verify_validity (EvdTlsCertificate  *self,
                                     GError            **error)
{
  gint result = EVD_TLS_VERIFY_STATE_OK;
  time_t time_from;
  time_t time_to;
  time_t now;

  g_return_val_if_fail (EVD_IS_TLS_CERTIFICATE (self), -1);

  if ( (time_from = evd_tls_certificate_get_expiration_time (self, error)) == -1 ||
       (time_to = evd_tls_certificate_get_activation_time (self, error)) == -1 )
    return -1;

  now = time (NULL);

  if (time_from < now)
    result |= EVD_TLS_VERIFY_STATE_EXPIRED;

  if (time_to > now)
    result |= EVD_TLS_VERIFY_STATE_NOT_ACTIVE;

  return result;
}

/**
 * evd_tls_certificate_get_pki_key:
 *
 * Returns: (transfer full):
 **/
EvdPkiPubkey *
evd_tls_certificate_get_pki_key (EvdTlsCertificate *self, GError **error)
{
  EvdPkiPubkey *key = NULL;
  gnutls_pubkey_t pubkey = NULL;
  gint err_code;

  g_return_val_if_fail (EVD_IS_TLS_CERTIFICATE (self), NULL);

  if (self->priv->type == EVD_TLS_CERTIFICATE_TYPE_UNKNOWN)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Failed to get key from not initialized private key");
      return NULL;
    }

  err_code = gnutls_pubkey_init (&pubkey);
  if (evd_error_propagate_gnutls (err_code, error))
    return NULL;

  if (self->priv->type == EVD_TLS_CERTIFICATE_TYPE_X509)
    err_code = gnutls_pubkey_import_x509 (pubkey, self->priv->x509_cert, 0);
  else
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Unsupported certificate type");
      gnutls_pubkey_deinit (pubkey);
      return NULL;
    }

  if (evd_error_propagate_gnutls (err_code, error))
    {
      gnutls_pubkey_deinit (pubkey);
      return NULL;
    }
  else
    {
      key = evd_pki_pubkey_new ();
      if (! evd_pki_pubkey_import_native (key, pubkey, error))
        {
          gnutls_pubkey_deinit (pubkey);
          g_object_unref (key);
          key = NULL;
        }
    }

  return key;
}
