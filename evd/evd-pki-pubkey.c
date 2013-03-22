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

G_DEFINE_TYPE (EvdPkiPubkey, evd_pki_pubkey, G_TYPE_OBJECT)

#define EVD_PKI_PUBKEY_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                         EVD_TYPE_PKI_PUBKEY, \
                                         EvdPkiPubkeyPrivate))

/* private data */
struct _EvdPkiPubkeyPrivate
{
  gnutls_pubkey_t key;

  EvdPkiKeyType type;
};

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

  g_type_class_add_private (obj_class, sizeof (EvdPkiPubkeyPrivate));
}

static void
evd_pki_pubkey_init (EvdPkiPubkey *self)
{
  EvdPkiPubkeyPrivate *priv;

  priv = EVD_PKI_PUBKEY_GET_PRIVATE (self);
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
encrypt_in_thread (GSimpleAsyncResult *res,
                   GObject            *object,
                   GCancellable       *cancellable)
{
  EvdPkiPubkey *self = EVD_PKI_PUBKEY (object);;
  gnutls_datum_t *clear_data;
  gnutls_datum_t *enc_data;
  gint err_code;
  GError *error = NULL;

  clear_data = g_simple_async_result_get_op_res_gpointer (res);
  enc_data = g_new (gnutls_datum_t, 1);

  /* encrypt */
  err_code = gnutls_pubkey_encrypt_data (self->priv->key,
                                         0,
                                         clear_data,
                                         enc_data);
  if (evd_error_propagate_gnutls (err_code, &error))
    {
      g_simple_async_result_take_error (res, error);
      g_free (enc_data);
    }
  else
    {
      g_simple_async_result_set_op_res_gpointer (res, enc_data, g_free);
    }

  g_object_unref (res);
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
 * @pubkey_st: (type guintptr):
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
  GSimpleAsyncResult *res;
  gnutls_datum_t *enc_data;

  g_return_if_fail (EVD_IS_PKI_PUBKEY (self));

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_pki_pubkey_encrypt);

  if (self->priv->key == NULL)
    {
      g_simple_async_result_set_error (res,
                                       G_IO_ERROR,
                                       G_IO_ERROR_NOT_INITIALIZED,
                                       "Public key not initialized");
      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);
      return;
    }

  enc_data = g_new (gnutls_datum_t, 1);
  enc_data->data = (guchar *) data;
  enc_data->size = size;

  g_simple_async_result_set_op_res_gpointer (res, enc_data, g_free);

  /* @TODO: use a thread pool to avoid overhead */
  g_simple_async_result_run_in_thread (res,
                                       encrypt_in_thread,
                                       G_PRIORITY_DEFAULT,
                                       cancellable);
}

gchar *
evd_pki_pubkey_encrypt_finish (EvdPkiPubkey  *self,
                               GAsyncResult  *result,
                               gsize         *size,
                               GError       **error)
{
  GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (result);

  g_return_val_if_fail (EVD_IS_PKI_PUBKEY (self), NULL);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                        G_OBJECT (self),
                                                        evd_pki_pubkey_encrypt),
                        NULL);

  if (! g_simple_async_result_propagate_error (res, error))
    {
      gnutls_datum_t *data;

      data = g_simple_async_result_get_op_res_gpointer (res);

      if (size != NULL)
        *size = data->size;

      return (gchar *) data->data;
    }
  else
    return NULL;
}
