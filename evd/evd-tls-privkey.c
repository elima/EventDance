/*
 * evd-tls-privkey.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009-2015, Igalia S.L.
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
#include "evd-tls-privkey.h"

#include "evd-error.h"
#include "evd-tls-common.h"

/* private data */
struct _EvdTlsPrivkeyPrivate
{
  gnutls_x509_privkey_t x509_privkey;

  EvdTlsCertificateType type;

  gboolean native_stolen;
};

G_DEFINE_TYPE_WITH_PRIVATE (EvdTlsPrivkey,
                            evd_tls_privkey,
                            G_TYPE_OBJECT)


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
                                                      "The type of privkey",
                                                      EVD_TLS_CERTIFICATE_TYPE_UNKNOWN,
                                                      EVD_TLS_CERTIFICATE_TYPE_X509,
                                                      EVD_TLS_CERTIFICATE_TYPE_UNKNOWN,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_STATIC_STRINGS));
}

static void
evd_tls_privkey_init (EvdTlsPrivkey *self)
{
  EvdTlsPrivkeyPrivate *priv;

  priv = evd_tls_privkey_get_instance_private (self);
  self->priv = priv;

  priv->x509_privkey = NULL;

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

  self->priv->native_stolen = FALSE;

  self->priv->type = EVD_TLS_CERTIFICATE_TYPE_UNKNOWN;
}

static EvdTlsCertificateType
evd_tls_privkey_detect_type (const gchar *raw_data)
{
  if (g_strstr_len (raw_data, 26, "BEGIN RSA PRIVATE KEY") != NULL)
    return EVD_TLS_CERTIFICATE_TYPE_X509;
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
evd_tls_privkey_import_from_file_thread (GTask        *task,
                                         gpointer      source_object,
                                         gpointer      task_data,
                                         GCancellable *cancellable)
{
  EvdTlsPrivkey *self = EVD_TLS_PRIVKEY (source_object);
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

  if (! evd_tls_privkey_import (self, content, size, &error))
    {
      g_free (content);
      g_task_return_error (task, error);
      return;
    }

  g_free (content);
  g_task_return_boolean (task, TRUE);
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
  GTask *task;

  g_return_if_fail (EVD_IS_TLS_PRIVKEY (self));
  g_return_if_fail (filename != NULL);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, evd_tls_privkey_import_from_file);

  g_task_set_task_data (task, g_strdup (filename), g_free);

  g_task_run_in_thread (task,
                        evd_tls_privkey_import_from_file_thread);
  g_object_unref (task);
}

gboolean
evd_tls_privkey_import_from_file_finish (EvdTlsPrivkey  *self,
                                         GAsyncResult   *result,
                                         GError        **error)
{
  GTask *task = G_TASK (result);

  g_return_val_if_fail (EVD_IS_TLS_PRIVKEY (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (task) == evd_tls_privkey_import_from_file,
                        FALSE);

  return g_task_propagate_boolean (task, error);
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
  else
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_INVALID_DATA,
                           "Unsupported private key type");
      gnutls_privkey_deinit (privkey);
      return NULL;
    }

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
