/*
 * evd-pki-privkey.c
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

#include "evd-pki-privkey.h"

#include "evd-error.h"

/* private data */
struct _EvdPkiPrivkeyPrivate
{
  gnutls_privkey_t key;

  EvdPkiKeyType type;
};

G_DEFINE_TYPE_WITH_PRIVATE (EvdPkiPrivkey,
                            evd_pki_privkey,
                            G_TYPE_OBJECT)

typedef struct
{
  EvdPkiKeyType key_type;
  guint bits;
} GenKeyData;

/* properties */
enum
{
  PROP_0,
  PROP_TYPE
};

static void     evd_pki_privkey_class_init         (EvdPkiPrivkeyClass *class);
static void     evd_pki_privkey_init               (EvdPkiPrivkey *self);

static void     evd_pki_privkey_finalize           (GObject *obj);

static void     evd_pki_privkey_get_property       (GObject    *obj,
                                                    guint       prop_id,
                                                    GValue     *value,
                                                    GParamSpec *pspec);

static void
evd_pki_privkey_class_init (EvdPkiPrivkeyClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_pki_privkey_finalize;
  obj_class->get_property = evd_pki_privkey_get_property;

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
evd_pki_privkey_init (EvdPkiPrivkey *self)
{
  EvdPkiPrivkeyPrivate *priv;

  priv = evd_pki_privkey_get_instance_private (self);
  self->priv = priv;

  priv->key = NULL;

  self->priv->type = EVD_PKI_KEY_TYPE_UNKNOWN;
}

static void
evd_pki_privkey_finalize (GObject *obj)
{
  EvdPkiPrivkey *self = EVD_PKI_PRIVKEY (obj);

  if (self->priv->key != NULL)
    gnutls_privkey_deinit (self->priv->key);

  G_OBJECT_CLASS (evd_pki_privkey_parent_class)->finalize (obj);
}

static void
evd_pki_privkey_get_property (GObject    *obj,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  EvdPkiPrivkey *self;

  self = EVD_PKI_PRIVKEY (obj);

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
decrypt_in_thread (GTask       *task,
                   gpointer     source_object,
                   gpointer     task_data,
                   GCancellable *cancellable)
{
  EvdPkiPrivkey *self = EVD_PKI_PRIVKEY (source_object);
  gint err_code;
  GError *error = NULL;
  gnutls_datum_t *data;
  gnutls_datum_t *msg;

  data = task_data;
  msg = g_new (gnutls_datum_t, 1);

  err_code = gnutls_privkey_decrypt_data (self->priv->key,
                                          0,
                                          data,
                                          msg);
  if (evd_error_propagate_gnutls (err_code, &error))
    {
      g_task_return_error (task, error);
      g_free (msg);
    }
  else
    {
      g_task_return_pointer (task, msg, g_free);
    }
}

static void
sign_in_thread (GTask        *task,
                gpointer      source_object,
                gpointer      task_data,
                GCancellable *cancellable)
{
  EvdPkiPrivkey *self = EVD_PKI_PRIVKEY (source_object);
  gint err_code;
  GError *error = NULL;
  gnutls_datum_t *data;
  gnutls_datum_t *signed_data;

  data = task_data;
  signed_data = g_new (gnutls_datum_t, 1);

  err_code = gnutls_privkey_sign_data (self->priv->key,
                                       GNUTLS_DIG_SHA256,
                                       0,
                                       data,
                                       signed_data);
  if (evd_error_propagate_gnutls (err_code, &error))
    {
      g_task_return_error (task, error);
      g_free (signed_data);
    }
  else
    {
      g_task_return_pointer (task, signed_data, g_free);
    }
}

static void
generate_in_thread (GTask        *task,
                    gpointer      source_object,
                    gpointer      task_data,
                    GCancellable *cancellable)
{
  EvdPkiPrivkey *self = EVD_PKI_PRIVKEY (source_object);
  GenKeyData *data;
  gnutls_x509_privkey_t x509_privkey;
  gnutls_privkey_t privkey;
  gint err_code;
  GError *error = NULL;

  data = task_data;

  /* generate X.509 private key */
  gnutls_x509_privkey_init (&x509_privkey);
  err_code = gnutls_x509_privkey_generate (x509_privkey,
                                           data->key_type,
                                           data->bits,
                                           0);
  if (evd_error_propagate_gnutls (err_code, &error))
    {
      g_task_return_error (task, error);
      gnutls_x509_privkey_deinit (x509_privkey);
      return;
    }

  /* import to abstract private key struct */
  gnutls_privkey_init (&privkey);
  err_code = gnutls_privkey_import_x509 (privkey,
                                         x509_privkey,
                                         GNUTLS_PRIVKEY_IMPORT_COPY);
  if (evd_error_propagate_gnutls (err_code, &error))
    {
      gnutls_privkey_deinit (privkey);
      g_task_return_error (task, error);
      gnutls_x509_privkey_deinit (x509_privkey);
      return;
    }

  /* set the abstract key as the new internal key */
  if (self->priv->key != NULL)
    gnutls_privkey_deinit (self->priv->key);

  self->priv->key = privkey;

  gnutls_x509_privkey_deinit (x509_privkey);
  g_task_return_boolean (task, TRUE);
}

/* public methods */

EvdPkiPrivkey *
evd_pki_privkey_new (void)
{
  return g_object_new (EVD_TYPE_PKI_PRIVKEY, NULL);
}

EvdPkiKeyType
evd_pki_privkey_get_key_type (EvdPkiPrivkey *self)
{
  g_return_val_if_fail (EVD_IS_PKI_PRIVKEY (self), -1);

  return self->priv->type;
}

/**
 * evd_pki_privkey_import_native:
 * @privkey: (type guintptr):
 *
 **/
gboolean
evd_pki_privkey_import_native (EvdPkiPrivkey     *self,
                               gnutls_privkey_t   privkey,
                               GError           **error)
{
  gint err_code;
  guint bits;
  EvdPkiKeyType type;

  g_return_val_if_fail (EVD_IS_PKI_PRIVKEY (self), FALSE);
  g_return_val_if_fail (privkey != NULL, FALSE);

  /* @TODO: check if there are operations pending and return error if so */

  type = gnutls_privkey_get_pk_algorithm (privkey, &bits);
  if (type < 0 && evd_error_propagate_gnutls (err_code, error))
    return FALSE;

  if (self->priv->key != NULL)
    gnutls_privkey_deinit (self->priv->key);

  self->priv->key = privkey;
  self->priv->type = type;

  return TRUE;
}

void
evd_pki_privkey_decrypt (EvdPkiPrivkey       *self,
                         const gchar         *data,
                         gsize                size,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
  GTask *task;
  gnutls_datum_t *dec_data;

  g_return_if_fail (EVD_IS_PKI_PRIVKEY (self));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, evd_pki_privkey_decrypt);

  if (self->priv->key == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_INITIALIZED,
                               "Private key not initialized");
      g_object_unref (task);
      return;
    }

  dec_data = g_new (gnutls_datum_t, 1);
  dec_data->data = (guchar *) data;
  dec_data->size = size;

  g_task_set_task_data (task, dec_data, g_free);

  /* @TODO: use a thread pool to avoid overhead */
  g_task_run_in_thread (task, decrypt_in_thread);
  g_object_unref (task);
}

gchar *
evd_pki_privkey_decrypt_finish (EvdPkiPrivkey  *self,
                                GAsyncResult   *result,
                                gsize          *size,
                                GError        **error)
{
  GTask *task = G_TASK (result);
  gnutls_datum_t *msg;
  gchar *ret;

  g_return_val_if_fail (EVD_IS_PKI_PRIVKEY (self), NULL);
  g_return_val_if_fail (g_task_is_valid (result, self), NULL);
  g_return_val_if_fail (g_task_get_source_tag (task) == evd_pki_privkey_decrypt,
                        NULL);

  msg = g_task_propagate_pointer (task, error);
  if (msg == NULL)
    return NULL;

  if (size != NULL)
    *size = msg->size;

  ret = (gchar *) msg->data;
  g_free (msg);

  return ret;
}

/**
 * evd_pki_privkey_sign_data:
 *
 * Since: 0.2.0
 **/
void
evd_pki_privkey_sign_data (EvdPkiPrivkey       *self,
                           const gchar         *data,
                           gsize                data_size,
                           GCancellable        *cancellable,
                           GAsyncReadyCallback  callback,
                           gpointer             user_data)
{
  GTask *task;
  gnutls_datum_t *sign_data;

  g_return_if_fail (EVD_IS_PKI_PRIVKEY (self));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, evd_pki_privkey_sign_data);

  if (self->priv->key == NULL)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_NOT_INITIALIZED,
                               "Private key not initialized");
      g_object_unref (task);
      return;
    }

  sign_data = g_new (gnutls_datum_t, 1);
  sign_data->data = (guchar *) data;
  sign_data->size = data_size;

  g_task_set_task_data (task, sign_data, g_free);

  /* @TODO: use a thread pool to avoid overhead */
  g_task_run_in_thread (task, sign_in_thread);
  g_object_unref (task);
}

/**
 * evd_pki_privkey_sign_data_finish:
 *
 * Since: 0.2.0
 **/
gchar *
evd_pki_privkey_sign_data_finish (EvdPkiPrivkey  *self,
                                  GAsyncResult   *result,
                                  gsize          *size,
                                  GError        **error)
{
  GTask *task = G_TASK (result);
  gnutls_datum_t *data;
  gchar *ret;

  g_return_val_if_fail (EVD_IS_PKI_PRIVKEY (self), NULL);
  g_return_val_if_fail (g_task_is_valid (result, self), NULL);
  g_return_val_if_fail (g_task_get_source_tag (task) == evd_pki_privkey_sign_data,
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
 * evd_pki_privkey_generate:
 *
 * Since: 0.2.0
 **/
void
evd_pki_privkey_generate (EvdPkiPrivkey        *self,
                          EvdPkiKeyType         key_type,
                          guint                 bits,
                          GCancellable         *cancellable,
                          GAsyncReadyCallback   callback,
                          gpointer              user_data)
{
  GTask *task;
  GenKeyData *data;

  g_return_if_fail (EVD_IS_PKI_PRIVKEY (self));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, evd_pki_privkey_generate);

  data = g_new (GenKeyData, 1);
  data->key_type = key_type;
  data->bits = bits;

  g_task_set_task_data (task, data, g_free);

  g_task_run_in_thread (task, generate_in_thread);
  g_object_unref (task);
}

/**
 * evd_pki_privkey_generate_finish:
 *
 * Since: 0.2.0
 **/
gboolean
evd_pki_privkey_generate_finish (EvdPkiPrivkey  *self,
                                 GAsyncResult   *result,
                                 GError        **error)
{
  GTask *task = G_TASK (result);

  g_return_val_if_fail (EVD_IS_PKI_PRIVKEY (self), FALSE);
  g_return_val_if_fail (g_task_is_valid (result, self), FALSE);
  g_return_val_if_fail (g_task_get_source_tag (task) == evd_pki_privkey_generate,
                        FALSE);

  return g_task_propagate_boolean (task, error);
}

/**
 * evd_pki_privkey_get_public_key:
 *
 * Returns: (transfer full):
 *
 * Since: 0.2.0
 **/
EvdPkiPubkey *
evd_pki_privkey_get_public_key (EvdPkiPrivkey *self, GError **error)
{
  gnutls_pubkey_t pubkey;
  gint err_code;
  EvdPkiPubkey *result = NULL;

  g_return_val_if_fail (EVD_IS_PKI_PRIVKEY (self), NULL);

  if (self->priv->key == NULL)
    {
      g_set_error (error,
                   G_IO_ERROR,
                   G_IO_ERROR_NOT_INITIALIZED,
                   "Private key not initialized");
      return NULL;
    }

  gnutls_pubkey_init (&pubkey);

  err_code = gnutls_pubkey_import_privkey (pubkey,
                                           self->priv->key,
                                           GNUTLS_KEY_ENCIPHER_ONLY,
                                           0);
  if (evd_error_propagate_gnutls (err_code, error))
    {
      gnutls_pubkey_deinit (pubkey);
      return NULL;
    }

  result = evd_pki_pubkey_new ();
  if (! evd_pki_pubkey_import_native (result, pubkey, error))
    {
      gnutls_pubkey_deinit (pubkey);
      g_object_unref (result);
      return NULL;
    }

  return result;
}
