/*
 * evd-pki-pubkey.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2011-2013, Igalia S.L.
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

#include <gnutls/gnutls.h>

#include "evd-pki-pubkey.h"

#include "evd-error.h"

/* private data */
struct _EvdPkiPubkeyPrivate
{
  gnutls_pubkey_t key;

  EvdPkiKeyType type;
};

G_DEFINE_TYPE_WITH_PRIVATE (EvdPkiPubkey,
                            evd_pki_pubkey,
                            G_TYPE_OBJECT)

typedef struct
{
  gnutls_datum_t data;
  gnutls_datum_t signature;
} VerifyData;

/* properties */
enum
{
  PROP_0,
  PROP_TYPE
};

static void     evd_pki_pubkey_class_init         (EvdPkiPubkeyClass *class);
static void     evd_pki_pubkey_init               (EvdPkiPubkey *self);

static void     evd_pki_pubkey_finalize           (GObject *obj);

static void     evd_pki_pubkey_get_property       (GObject    *obj,
                                                   guint       prop_id,
                                                   GValue     *value,
                                                   GParamSpec *pspec);

static void
evd_pki_pubkey_class_init (EvdPkiPubkeyClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_pki_pubkey_finalize;
  obj_class->get_property = evd_pki_pubkey_get_property;

  /* install properties */
  g_object_class_install_property (obj_class, PROP_TYPE,
                                   g_param_spec_uint ("type",
                                                      "Key type",
                                                      "The type of private key (RSA, DSA, etc)",
                                                      EVD_PKI_KEY_TYPE_UNKNOWN,
                                                      EVD_PKI_KEY_TYPE_DSA,
                                                      EVD_PKI_KEY_TYPE_UNKNOWN,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_STATIC_STRINGS));
}

static void
evd_pki_pubkey_init (EvdPkiPubkey *self)
{
  EvdPkiPubkeyPrivate *priv;

  priv = evd_pki_pubkey_get_instance_private (self);
  self->priv = priv;

  priv->key = NULL;

  self->priv->type = EVD_PKI_KEY_TYPE_UNKNOWN;
}

static void
evd_pki_pubkey_finalize (GObject *obj)
{
  EvdPkiPubkey *self = EVD_PKI_PUBKEY (obj);

  if (self->priv->key != NULL)
    gnutls_pubkey_deinit (self->priv->key);

  G_OBJECT_CLASS (evd_pki_pubkey_parent_class)->finalize (obj);
}

static void
evd_pki_pubkey_get_property (GObject    *obj,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  EvdPkiPubkey *self;

  self = EVD_PKI_PUBKEY (obj);

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
encrypt_in_thread (GTask        *task,
                   gpointer      source_object,
                   gpointer      task_data,
                   GCancellable *cancellable)
{
  EvdPkiPubkey *self = EVD_PKI_PUBKEY (source_object);
  gnutls_datum_t *clear_data;
  gnutls_datum_t *enc_data;
  gint err_code;
  GError *error = NULL;

  clear_data = task_data;
  enc_data = g_new (gnutls_datum_t, 1);

  /* encrypt */
  err_code = gnutls_pubkey_encrypt_data (self->priv->key,
                                         0,
                                         clear_data,
                                         enc_data);
  if (evd_error_propagate_gnutls (err_code, &error))
    {
      g_task_return_error (task, error);
      g_free (enc_data);
    }
  else
    {
      g_task_return_pointer (task, enc_data, g_free);
    }
}

static void
verify_in_thread (GTask        *task,
                  gpointer      source_object,
                  gpointer      task_data,
                  GCancellable *cancellable)
{
  EvdPkiPubkey *self = EVD_PKI_PUBKEY (source_object);
  VerifyData *verify_data;
  gint err_code;
  GError *error = NULL;

  gnutls_sign_algorithm_t sign_algo;

  verify_data = task_data;

  /* verify */
  switch (self->priv->type)
    {
    case GNUTLS_PK_RSA: sign_algo = GNUTLS_SIGN_RSA_SHA256; break;
    case GNUTLS_PK_DSA: sign_algo = GNUTLS_SIGN_DSA_SHA256; break;
    case GNUTLS_PK_EC:  sign_algo = GNUTLS_SIGN_ECDSA_SHA256; break;
    default: sign_algo = GNUTLS_SIGN_UNKNOWN;
    }

  err_code = gnutls_pubkey_verify_data2 (self->priv->key,
                                         sign_algo,
                                         0,
                                         &verify_data->data,
                                         &verify_data->signature);

  if (err_code < 0 && evd_error_propagate_gnutls (err_code, &error))
    {
      g_task_return_error (task, error);
      return;
    }

  g_task_return_boolean (task, TRUE);
}

/* public methods */

EvdPkiPubkey *
evd_pki_pubkey_new (void)
{
  return g_object_new (EVD_TYPE_PKI_PUBKEY, NULL);
}

EvdPkiKeyType
evd_pki_pubkey_get_key_type (EvdPkiPubkey *self)
{
  g_return_val_if_fail (EVD_IS_PKI_PUBKEY (self), -1);

  return self->priv->type;
}

/**
 * evd_pki_pubkey_import_native:
 * @pubkey: (type guintptr):
 *
 **/
gboolean
evd_pki_pubkey_import_native (EvdPkiPubkey     *self,
                              gnutls_pubkey_t   pubkey,
                              GError          **error)
{
  EvdPkiKeyType type;
  gint err_code;
  guint bits;

  g_return_val_if_fail (EVD_IS_PKI_PUBKEY (self), FALSE);
  g_return_val_if_fail (pubkey != NULL, FALSE);

  /* @TODO: check if there are operations pending and return error if so */

  type = gnutls_pubkey_get_pk_algorithm (pubkey, &bits);
  if (type < 0 && evd_error_propagate_gnutls (err_code, error))
    return FALSE;

  if (self->priv->key != NULL)
    gnutls_pubkey_deinit (self->priv->key);

  self->priv->key = pubkey;
  self->priv->type = type;

  return TRUE;
}

void
evd_pki_pubkey_encrypt (EvdPkiPubkey        *self,
                        const gchar         *data,
                        gsize                size,
                        GCancellable        *cancellable,
                        GAsyncReadyCallback  callback,
                        gpointer             user_data)
{
  GTask *task;
  gnutls_datum_t *clear_data;

  g_return_if_fail (EVD_IS_PKI_PUBKEY (self));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, evd_pki_pubkey_encrypt);

  if (self->priv->key == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_INITIALIZED,
                               "Public key not initialized");
      g_object_unref (task);
      return;
    }

  clear_data = g_new (gnutls_datum_t, 1);
  clear_data->data = (guchar *) data;
  clear_data->size = size;

  g_task_set_task_data (task, clear_data, g_free);

  /* @TODO: use a thread pool to avoid overhead */
  g_task_run_in_thread (task, encrypt_in_thread);
  g_object_unref (task);
}

gchar *
evd_pki_pubkey_encrypt_finish (EvdPkiPubkey  *self,
                               GAsyncResult  *result,
                               gsize         *size,
                               GError       **error)
{
  GTask *task = G_TASK (result);
  gnutls_datum_t *data;
  gchar *ret;

  g_return_val_if_fail (EVD_IS_PKI_PUBKEY (self), NULL);
  g_return_val_if_fail (g_task_is_valid (result, self), NULL);
  g_return_val_if_fail (g_task_get_source_tag (task) == evd_pki_pubkey_encrypt,
                        NULL);

  data = g_task_propagate_pointer (task, error);
  if (data == NULL)
    return NULL;

  if (size != NULL)
    *size = data->size;

  ret = (gchar *) data->data;
  g_free (data);

  return ret;
}

/**
 * evd_pki_pubkey_verify_data:
 *
 * Since: 0.2.0
 **/
void
evd_pki_pubkey_verify_data (EvdPkiPubkey        *self,
                            const gchar         *data,
                            gsize                data_size,
                            const gchar         *signature,
                            gsize                signature_size,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  GTask *task;
  VerifyData *verify_data;

  g_return_if_fail (EVD_IS_PKI_PUBKEY (self));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, evd_pki_pubkey_verify_data);

  if (self->priv->key == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_INITIALIZED,
                               "Public key not initialized");
      g_object_unref (task);
      return;
    }

  verify_data = g_new (VerifyData, 1);

  verify_data->data.data = (guchar *) data;
  verify_data->data.size = data_size;

  verify_data->signature.data = (guchar *) signature;
  verify_data->signature.size = signature_size;

  g_task_set_task_data (task, verify_data, g_free);

  /* @TODO: use a thread pool to avoid overhead */
  g_task_run_in_thread (task, verify_in_thread);
  g_object_unref (task);
}

/**
 * evd_pki_pubkey_verify_data_finish:
 *
 * Since: 0.2.0
 **/
gboolean
evd_pki_pubkey_verify_data_finish (EvdPkiPubkey  *self,
                                   GAsyncResult  *result,
                                   GError       **error)
{
  GTask *task = G_TASK (result);

  g_return_val_if_fail (EVD_IS_PKI_PUBKEY (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (task) == evd_pki_pubkey_verify_data,
                        FALSE);

  return g_task_propagate_boolean (task, error);
}
