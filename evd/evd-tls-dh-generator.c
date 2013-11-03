/*
 * evd-tls-dh-generator.c
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
#if (! GLIB_CHECK_VERSION(2, 31, 0))
  GMutex     *cache_mutex;
#else
  GMutex      cache_mutex;
#endif
};

typedef struct _EvdTlsDhParamsSource EvdTlsDhParamsSource;

struct _EvdTlsDhParamsSource
{
  guint               dh_bits;
  gnutls_dh_params_t  dh_params;
  GQueue             *queue;
#if (! GLIB_CHECK_VERSION(2, 31, 0))
  GMutex             *mutex;
#else
  GMutex              mutex;
#endif
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

#if (! GLIB_CHECK_VERSION(2, 31, 0))
  priv->cache_mutex = g_mutex_new ();
#else
  g_mutex_init (&priv->cache_mutex);
#endif
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

#if (! GLIB_CHECK_VERSION(2, 31, 0))
  g_mutex_free (self->priv->cache_mutex);
#else
  g_mutex_clear (&self->priv->cache_mutex);
#endif

  G_OBJECT_CLASS (evd_tls_dh_generator_parent_class)->finalize (obj);
}

static void
evd_tls_dh_generator_free_source (EvdTlsDhParamsSource *source)
{
  GSimpleAsyncResult *item;

#if (! GLIB_CHECK_VERSION(2, 31, 0))
  g_mutex_lock (source->mutex);
#else
  g_mutex_lock (&source->mutex);
#endif

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

#if (! GLIB_CHECK_VERSION(2, 31, 0))
  g_mutex_unlock (source->mutex);

  g_mutex_free (source->mutex);
#else
  g_mutex_unlock (&source->mutex);

  g_mutex_clear (&source->mutex);
#endif

  g_slice_free (EvdTlsDhParamsSource, source);
}

static void
evd_tls_dh_generator_generate_func (GSimpleAsyncResult *res,
                                    GObject            *object,
                                    GCancellable       *cancellable)
{
  EvdTlsDhParamsSource *source;
  gnutls_dh_params_t dh_params;
  gint err_code;
  GSimpleAsyncResult *item;
  GError *error = NULL;

  source = (EvdTlsDhParamsSource *) g_simple_async_result_get_source_tag (res);

  /* @TODO: handle cancellation */

  err_code = gnutls_dh_params_init (&dh_params);
  if (err_code == GNUTLS_E_SUCCESS)
    err_code = gnutls_dh_params_generate2 (dh_params, source->dh_bits);

#if (! GLIB_CHECK_VERSION(2, 31, 0))
  g_mutex_lock (source->mutex);
#else
  g_mutex_lock (&source->mutex);
#endif

  if (! evd_error_propagate_gnutls (err_code, &error))
    source->dh_params = dh_params;

  while ( (item =
           G_SIMPLE_ASYNC_RESULT (g_queue_pop_head (source->queue)))
          != NULL)
    {
      if (error != NULL)
        {
          g_simple_async_result_set_from_error (item, error);
        }
      else
        {
          g_simple_async_result_set_op_res_gpointer (item,
                                                   (gpointer) source->dh_params,
                                                   NULL);
        }

      if (item != res)
        g_simple_async_result_complete_in_idle (item);
      g_object_unref (item);
    }

  g_queue_free (source->queue);
  source->queue = NULL;

  if (error != NULL)
    {
      g_error_free (error);

      g_hash_table_remove (source->parent->priv->cache, &source->dh_bits);

      evd_tls_dh_generator_free_source (source);
    }

#if (! GLIB_CHECK_VERSION(2, 31, 0))
  g_mutex_unlock (source->mutex);
#else
  g_mutex_unlock (&source->mutex);
#endif
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

#if (! GLIB_CHECK_VERSION(2, 31, 0))
  g_mutex_lock (self->priv->cache_mutex);
#else
  g_mutex_lock (&self->priv->cache_mutex);
#endif

  source = g_hash_table_lookup (self->priv->cache,
                                &bit_length);

#if (! GLIB_CHECK_VERSION(2, 31, 0))
  g_mutex_unlock (self->priv->cache_mutex);
#else
  g_mutex_unlock (&self->priv->cache_mutex);
#endif

  if (source != NULL)
    {
      gboolean params_ready;
      gboolean done = FALSE;

#if (! GLIB_CHECK_VERSION(2, 31, 0))
      g_mutex_lock (source->mutex);
#else
      g_mutex_lock (&source->mutex);
#endif

      params_ready = (source->dh_params != NULL);

      result = g_simple_async_result_new (NULL,
                                          callback,
                                          user_data,
                                          NULL);

      if (params_ready)
        {
          if (! regenerate)
            {
              g_simple_async_result_set_op_res_gpointer (result,
                                                   (gpointer) source->dh_params,
                                                   NULL);
              g_simple_async_result_complete_in_idle (result);
              g_object_unref (result);

              done = TRUE;
            }
          else
            {
              g_object_unref (result);

#if (! GLIB_CHECK_VERSION(2, 31, 0))
              g_mutex_lock (self->priv->cache_mutex);
#else
              g_mutex_lock (&self->priv->cache_mutex);
#endif

              g_hash_table_remove (self->priv->cache, &source->dh_bits);

#if (! GLIB_CHECK_VERSION(2, 31, 0))
              g_mutex_unlock (self->priv->cache_mutex);
#else
              g_mutex_unlock (&self->priv->cache_mutex);
#endif

#if (! GLIB_CHECK_VERSION(2, 31, 0))
              g_mutex_unlock (source->mutex);
#else
              g_mutex_unlock (&source->mutex);
#endif

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
        {
#if (! GLIB_CHECK_VERSION(2, 31, 0))
          g_mutex_unlock (source->mutex);
#else
          g_mutex_unlock (&source->mutex);
#endif
        }

      if (done)
        return;
    }

  source = g_slice_new (EvdTlsDhParamsSource);
  source->dh_bits = bit_length;
  source->dh_params = NULL;
  source->queue = g_queue_new ();

#if (! GLIB_CHECK_VERSION(2, 31, 0))
  source->mutex = g_mutex_new ();
#else
  g_mutex_init (&source->mutex);
#endif

  source->parent = self;

#if (! GLIB_CHECK_VERSION(2, 31, 0))
  g_mutex_lock (self->priv->cache_mutex);
#else
  g_mutex_lock (&self->priv->cache_mutex);
#endif

  g_hash_table_insert (self->priv->cache,
                       &source->dh_bits,
                       source);

#if (! GLIB_CHECK_VERSION(2, 31, 0))
  g_mutex_unlock (self->priv->cache_mutex);
#else
  g_mutex_unlock (&self->priv->cache_mutex);
#endif

  result = g_simple_async_result_new (NULL,
                                      callback,
                                      user_data,
                                      (gpointer) source);

  /* append the result to the source queue, only to allow
     destroying it in case of premature freeing of the generator */
#if (! GLIB_CHECK_VERSION(2, 31, 0))
  g_mutex_lock (source->mutex);
#else
  g_mutex_lock (&source->mutex);
#endif

  g_queue_push_tail (source->queue, result);

#if (! GLIB_CHECK_VERSION(2, 31, 0))
  g_mutex_unlock (source->mutex);
#else
  g_mutex_unlock (&source->mutex);
#endif

  g_simple_async_result_run_in_thread (result,
                                       evd_tls_dh_generator_generate_func,
                                       G_PRIORITY_DEFAULT,
                                       cancellable);
}

/**
 * evd_tls_dh_generator_generate_finish:
 *
 * Returns: (transfer none):
 **/
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
