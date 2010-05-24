/*
 * evd-tls-dh-generator.c
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

#include "evd-error.h"
#include "evd-tls-dh-generator.h"
#include "evd-tls-common.h"

G_DEFINE_TYPE (EvdTlsDhGenerator, evd_tls_dh_generator, G_TYPE_OBJECT)

#define EVD_TLS_DH_GENERATOR_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                               EVD_TYPE_TLS_DH_GENERATOR, \
                                               EvdTlsDhGeneratorPrivate))

struct _EvdTlsDhGeneratorPrivate
{
  GHashTable *cache;
  GMutex     *cache_mutex;
};

typedef struct _EvdTlsDhParamsSource EvdTlsDhParamsSource;

struct _EvdTlsDhParamsSource
{
  guint               dh_bits;
  gnutls_dh_params_t  dh_params;
  GQueue             *queue;
  GMutex             *mutex;
  EvdTlsDhGenerator  *parent;
};

static void     evd_tls_dh_generator_class_init         (EvdTlsDhGeneratorClass *class);
static void     evd_tls_dh_generator_init               (EvdTlsDhGenerator *self);

static void     evd_tls_dh_generator_finalize           (GObject *obj);

static void     evd_tls_dh_generator_free_source        (EvdTlsDhParamsSource *source);


static void
evd_tls_dh_generator_class_init (EvdTlsDhGeneratorClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_tls_dh_generator_finalize;

  g_type_class_add_private (obj_class, sizeof (EvdTlsDhGeneratorPrivate));
}

static void
evd_tls_dh_generator_init (EvdTlsDhGenerator *self)
{
  EvdTlsDhGeneratorPrivate *priv;

  priv = EVD_TLS_DH_GENERATOR_GET_PRIVATE (self);
  self->priv = priv;

  priv->cache = g_hash_table_new (g_int_hash, g_int_equal);
  priv->cache_mutex = g_mutex_new ();
}

static gboolean
evd_tls_dh_generator_free_cache_item (gpointer key,
                                      gpointer value,
                                      gpointer user_data)
{
  evd_tls_dh_generator_free_source ((EvdTlsDhParamsSource *) value);

  return TRUE;
}

static void
evd_tls_dh_generator_finalize (GObject *obj)
{
  EvdTlsDhGenerator *self = EVD_TLS_DH_GENERATOR (obj);

  g_hash_table_foreach_remove (self->priv->cache,
                               evd_tls_dh_generator_free_cache_item,
                               self);
  g_hash_table_destroy (self->priv->cache);

  g_mutex_free (self->priv->cache_mutex);

  G_OBJECT_CLASS (evd_tls_dh_generator_parent_class)->finalize (obj);
}

static void
evd_tls_dh_generator_free_source (EvdTlsDhParamsSource *source)
{
  GSimpleAsyncResult *item;

  g_mutex_lock (source->mutex);

  if (source->queue != NULL)
    {
      while ( (item =
               G_SIMPLE_ASYNC_RESULT (g_queue_pop_head (source->queue)))
              != NULL)
        {
          g_object_unref (item);
        }
      g_queue_free (source->queue);
    }

  if (source->dh_params != NULL)
    gnutls_dh_params_deinit (source->dh_params);

  g_mutex_unlock (source->mutex);

  g_mutex_free (source->mutex);

  g_slice_free (EvdTlsDhParamsSource, source);
}

static void
evd_tls_dh_generator_source_destroy_notify (gpointer data)
{
  evd_tls_dh_generator_free_source ((EvdTlsDhParamsSource *) data);
}

static void
evd_tls_dh_generator_setup_result_from_source (EvdTlsDhParamsSource  *source,
                                               GSimpleAsyncResult    *result)
{
  gnutls_dh_params_t dh_params = NULL;
  gint err_code;

  err_code = gnutls_dh_params_init (&dh_params);
  if (err_code == GNUTLS_E_SUCCESS)
    err_code = gnutls_dh_params_cpy (dh_params, source->dh_params);

  if (err_code != GNUTLS_E_SUCCESS)
    {
      GError *error = NULL;

      evd_tls_build_error (err_code,
                           &error,
                           EVD_TLS_ERROR);

      g_simple_async_result_set_from_error (result, error);
      g_error_free (error);
    }
  else
    {
      g_simple_async_result_set_op_res_gpointer (result,
                                    (gpointer) source->dh_params,
                                    evd_tls_dh_generator_source_destroy_notify);
    }
}

static void
evd_tls_dh_generator_generate_func (GSimpleAsyncResult *res,
                                    GObject            *object,
                                    GCancellable       *cancellable)
{
  EvdTlsDhParamsSource *source;
  gnutls_dh_params_t dh_params;
  gint err_code;

  source = (EvdTlsDhParamsSource *) g_simple_async_result_get_source_tag (res);

  /* @TODO: handle cancellation */

  err_code = gnutls_dh_params_init (&dh_params);
  if (err_code == GNUTLS_E_SUCCESS)
    err_code = gnutls_dh_params_generate2 (dh_params, source->dh_bits);

  if (err_code != GNUTLS_E_SUCCESS)
    {
      GError *error = NULL;

      evd_tls_build_error (err_code,
                           &error,
                           EVD_TLS_ERROR);

      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);

      evd_tls_dh_generator_free_source (source);
    }
  else
    {
      GSimpleAsyncResult *item;

      /* fill the cache with the generated params */
      g_mutex_lock (source->mutex);
      source->dh_params = dh_params;

      evd_tls_dh_generator_setup_result_from_source (source, res);

      /* the first AsyncResult in the queue is the one passed to this
         function and will be completed automatically */
      g_queue_pop_head (source->queue);

      while ( (item =
               G_SIMPLE_ASYNC_RESULT (g_queue_pop_head (source->queue)))
               != NULL)
        {
          evd_tls_dh_generator_setup_result_from_source (source, item);

          g_simple_async_result_complete_in_idle (item);
        }
      g_queue_free (source->queue);
      source->queue = NULL;

      g_mutex_unlock (source->mutex);
    }
}

/* public methods */

EvdTlsDhGenerator *
evd_tls_dh_generator_new (void)
{
  EvdTlsDhGenerator *self;

  self = g_object_new (EVD_TYPE_TLS_DH_GENERATOR, NULL);

  return self;
}

void
evd_tls_dh_generator_generate (EvdTlsDhGenerator   *self,
                               guint                bit_length,
                               gboolean             regenerate,
                               GAsyncReadyCallback  callback,
                               GCancellable        *cancellable,
                               gpointer             user_data)
{
  EvdTlsDhParamsSource *source;
  GSimpleAsyncResult *result;

  g_return_if_fail (bit_length > 0);
  g_return_if_fail (callback != NULL);

  g_mutex_lock (self->priv->cache_mutex);
  source = g_hash_table_lookup (self->priv->cache,
                                &bit_length);
  g_mutex_unlock (self->priv->cache_mutex);

  if (source != NULL)
    {
      gboolean params_ready;
      gboolean done = FALSE;

      g_mutex_lock (source->mutex);
      params_ready = (source->dh_params != NULL);

      result = g_simple_async_result_new (NULL,
                                          callback,
                                          user_data,
                                          NULL);

      if (params_ready)
        {
          if (! regenerate)
            {
              evd_tls_dh_generator_setup_result_from_source (source, result);
              g_simple_async_result_complete_in_idle (result);

              done = TRUE;
            }
          else
            {
              g_object_unref (result);

              g_mutex_lock (self->priv->cache_mutex);
              g_hash_table_remove (self->priv->cache, &source->dh_bits);
              g_mutex_unlock (self->priv->cache_mutex);

              g_mutex_unlock (source->mutex);
              evd_tls_dh_generator_free_source (source);
              source = NULL;
            }
        }
      else
        {
          g_queue_push_tail (source->queue,
                             (gpointer) result);

          done = TRUE;
        }

      if (source != NULL)
        g_mutex_unlock (source->mutex);

      if (done)
        return;
    }

  source = g_slice_new (EvdTlsDhParamsSource);
  source->dh_bits = bit_length;
  source->dh_params = NULL;
  source->queue = g_queue_new ();
  source->mutex = g_mutex_new ();
  source->parent = self;

  g_mutex_lock (self->priv->cache_mutex);
  g_hash_table_insert (self->priv->cache,
                       &source->dh_bits,
                       source);
  g_mutex_unlock (self->priv->cache_mutex);

  result = g_simple_async_result_new (NULL,
                                      callback,
                                      user_data,
                                      (gpointer) source);

  /* append the result to the source queue, only to allow
     destroying it in case of premature freeing of the generator */
  g_mutex_lock (source->mutex);
  g_queue_push_tail (source->queue, result);
  g_mutex_unlock (source->mutex);

  g_simple_async_result_run_in_thread (result,
                                       evd_tls_dh_generator_generate_func,
                                       G_PRIORITY_DEFAULT,
                                       cancellable);
}

gpointer
evd_tls_dh_generator_generate_finish (EvdTlsDhGenerator  *self,
                                      GAsyncResult       *result,
                                      GError            **error)
{
  GSimpleAsyncResult *res;

  g_return_val_if_fail (EVD_IS_TLS_DH_GENERATOR (self), NULL);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

  res = G_SIMPLE_ASYNC_RESULT (result);

  if (g_simple_async_result_get_op_res_gpointer (res) == NULL)
    {
      g_simple_async_result_propagate_error (res, error);

      return NULL;
    }
  else
    {
      return g_simple_async_result_get_op_res_gpointer (res);
    }
}
