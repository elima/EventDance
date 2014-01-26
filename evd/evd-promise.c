/*
 * evd-promise.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2014, Igalia S.L.
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
 */

/**
 * SECTION:evd-promise
 * @short_description: Promises asynchronous pattern
 * @stability: Unstable
 *
 * The deferred/promise asynchronous pattern is similar to, and compatible with,
 * GIO's #GAsyncResult model. However, it allows for new functionality that is
 * currently not easy to implement with GIO's model.
 *
 * Specifically, promises allow for several callbacks to observe the completion
 * of an asynchronous operation. It also simplifies the code for cases when
 * an application action depends on the completion of several asynchronous
 * operations.
 *
 * It works as follows:
 *
 * First, the object that performs the asynchronous operation creates a deferred
 * object with evd_deferred_new(). Every deferred object has an #EvdPromise
 * object associated that can be retrieved with evd_deferred_get_promise().
 *
 * An #EvdPromise object represents the future completion of the deferred
 * operation, and is immediately returned to the application. The promise
 * cannot resolve the operation by itself, only the deferred object can.
 * The application can then register one or more callbacks (or none) using
 * evd_promise_then(), to get notified when the operation completes. Callbacks
 * can be attached even after the operation completed, in which case they are
 * called immediately on the next even loop cycle. The result value held by a
 * resolved promise remains immutable until the object is destroyed.
 *
 * The #EvdDeferred object is kept private during the implementation of the
 * asynchronous operation (e.g, as with #GSimpleAsyncResult).
 *
 * When the operation completes, a set of convenient methods are provided
 * by #EvdDeferred to set the result of the operation:
 * evd_deferred_set_result_pointer(), evd_deferred_set_result_size(),
 * evd_deferred_set_result_boolean() and evd_deferred_take_result_error(). These
 * methods work similarly to the g_simple_async_result_set_op_res_*() and
 * g_simple_async_result_*_error() family.
 *
 * After the result has been set, evd_deferred_complete() or
 * evd_deferred_complete_in_idle() must be called to notify the application
 * that the promise has been resolved and the operation completed.
 *
 * If callbacks were attached to the promise, these will be called in order upon
 * completion of the operation. To retrieve the result, a set of methods are
 * provided by #EvdPromise: evd_promise_get_result_pointer(),
 * evd_promise_get_result_size(), evd_promise_get_result_boolean() and
 * evd_promise_propagate_error(). Calling these methods before the operation
 * completes returns an undefined value.
 *
 * If a cancellable object was provided by the application when launching
 * the asynchronous operation, then it can be cancelled using
 * evd_promise_cancel(). Alternatively, it can be retrieved from the promise
 * with evd_promise_get_cancellable() (e.g, to give it to another asynchronous
 * operation).
 **/

/**
 * EvdPromiseClass:
 * @parent_class: The parent class
 *
 * The class for #EvdPromise objects.
 **/

/**
 * EvdDeferred:
 *
 * An opaque structure that represents a deferred object.
 **/

#include "evd-promise.h"

#define WARN_IF_NOT_COMPLETED(promise) if (!promise->priv->completed)  \
                                         g_warning ("Getting the result from an unresolved promise")

#define EVD_PROMISE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                      EVD_TYPE_PROMISE, \
                                      EvdPromisePrivate))

typedef void (* ResolvePointer) (EvdPromise     *self,
                                 gpointer        data,
                                 GDestroyNotify  data_free_func);
typedef void (* ResolveSize)    (EvdPromise     *self,
                                 gssize          size);
typedef void (* ResolveBoolean) (EvdPromise     *self,
                                 gboolean        bool);
typedef void (* Reject)         (EvdPromise     *self,
                                 GError         *error);

typedef struct
{
  ResolvePointer resolve_pointer;
  ResolveSize resolve_size;
  ResolveBoolean resolve_boolean;
  Reject reject;
} ResolveFuncs;

typedef struct
{
  EvdPromise *promise;
  GAsyncReadyCallback callback;
  gpointer user_data;
} PromiseClosure;

struct _EvdPromisePrivate
{
  gboolean completed;

  GObject *src_obj;
  gpointer tag;
  GCancellable *cancellable;
  gpointer user_data;

  gpointer res_pointer;
  GDestroyNotify res_pointer_free_func;
  gssize res_size;
  gboolean res_boolean;
  GError *res_error;

  ResolveFuncs *resolve_funcs;

  GList *listeners;
};

struct _EvdDeferred
{
  gint ref_count;

  gboolean completed;
  EvdPromise *promise;
  ResolveFuncs *resolve_funcs;
};

static void      evd_promise_class_init           (EvdPromiseClass *class);
static void      evd_promise_init                 (EvdPromise *self);

static void      evd_promise_finalize             (GObject *obj);
static void      evd_promise_dispose              (GObject *obj);

static void      async_result_iface_init          (GAsyncResultIface *iface);
static gpointer  async_result_get_user_data       (GAsyncResult *res);
static GObject * async_result_get_source_object   (GAsyncResult *res);
static gboolean  async_result_is_tagged           (GAsyncResult *res,
                                                   gpointer      source_tag);

static void      resolve_pointer_real             (EvdPromise     *self,
                                                   gpointer        data,
                                                   GDestroyNotify  data_free_func);
static void      resolve_size_real                (EvdPromise *self,
                                                   gssize      size);
static void      resolve_boolean_real             (EvdPromise *self,
                                                   gboolean    bool);
static void      reject_real                      (EvdPromise  *self,
                                                   GError      *error);

static void      free_promise_closure             (PromiseClosure *closure);

G_DEFINE_TYPE_WITH_CODE (EvdPromise, evd_promise, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_RESULT,
                                                async_result_iface_init));

G_DEFINE_BOXED_TYPE (EvdDeferred,
                     evd_deferred,
                     evd_deferred_ref,
                     evd_deferred_unref)

static void
evd_promise_class_init (EvdPromiseClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_promise_dispose;
  obj_class->finalize = evd_promise_finalize;

  g_type_class_add_private (obj_class, sizeof (EvdPromisePrivate));
}

static void
async_result_iface_init (GAsyncResultIface *iface)
{
  iface->get_user_data = async_result_get_user_data;
  iface->get_source_object = async_result_get_source_object;
  iface->is_tagged = async_result_is_tagged;
}

static void
evd_promise_init (EvdPromise *self)
{
  EvdPromisePrivate *priv;

  priv = EVD_PROMISE_GET_PRIVATE (self);
  self->priv = priv;

  priv->completed = FALSE;

  priv->src_obj = NULL;
  priv->tag = NULL;
  priv->cancellable = NULL;

  priv->res_pointer = NULL;
  priv->res_pointer_free_func = NULL;
  priv->res_size = 0;
  priv->res_boolean = FALSE;
  priv->res_error = NULL;

  priv->resolve_funcs = g_new0 (ResolveFuncs, 1);
  priv->resolve_funcs->resolve_pointer = resolve_pointer_real;
  priv->resolve_funcs->resolve_size = resolve_size_real;
  priv->resolve_funcs->resolve_boolean = resolve_boolean_real;
  priv->resolve_funcs->reject = reject_real;

  priv->listeners = NULL;
}

static void
evd_promise_dispose (GObject *obj)
{
  EvdPromise *self = EVD_PROMISE (obj);

  if (self->priv->res_pointer != NULL &&
      self->priv->res_pointer_free_func != NULL)
    {
      self->priv->res_pointer_free_func (self->priv->res_pointer);
      self->priv->res_pointer = NULL;
    }

  if (self->priv->listeners != NULL)
    {
      g_list_free_full (self->priv->listeners,
                        (GDestroyNotify) free_promise_closure);
      self->priv->listeners = NULL;
    }

  if (self->priv->src_obj != NULL)
    {
      g_object_unref (self->priv->src_obj);
      self->priv->src_obj = NULL;
    }

  G_OBJECT_CLASS (evd_promise_parent_class)->dispose (obj);
}

static void
evd_promise_finalize (GObject *obj)
{
  EvdPromise *self = EVD_PROMISE (obj);

  g_free (self->priv->resolve_funcs);

  if (self->priv->res_error != NULL)
    g_error_free (self->priv->res_error);

  G_OBJECT_CLASS (evd_promise_parent_class)->finalize (obj);
}

static gpointer
async_result_get_user_data (GAsyncResult *res)
{
  EvdPromise *self = EVD_PROMISE (res);

  return self->priv->user_data;
}

static GObject *
async_result_get_source_object (GAsyncResult *res)
{
  EvdPromise *self = EVD_PROMISE (res);

  if (self->priv->src_obj != NULL)
    return g_object_ref (self->priv->src_obj);
  else
    return NULL;
}

static gboolean
async_result_is_tagged (GAsyncResult *res, gpointer source_tag)
{
  EvdPromise *self = EVD_PROMISE (res);

  return self->priv->tag == source_tag;
}

static ResolveFuncs *
steal_resolve_funcs (EvdPromise *self)
{
  ResolveFuncs *result;

  result = self->priv->resolve_funcs;
  self->priv->resolve_funcs = NULL;

  return result;
}

static void
free_promise_closure (PromiseClosure *closure)
{
  g_object_unref (closure->promise);
  g_free (closure);
}

static void
evd_promise_notify_completion (EvdPromise *self)
{
  GList *node;

  self->priv->completed = TRUE;

  node = self->priv->listeners;
  while (node != NULL)
    {
      PromiseClosure *closure;

      closure = node->data;

      /* this is to make g_async_result_get_user_data() work */
      self->priv->user_data = closure->user_data;

      closure->callback (closure->promise->priv->src_obj,
                         G_ASYNC_RESULT (self),
                         closure->user_data);

      free_promise_closure (closure);

      node = g_list_next (node);
    }

  g_list_free (self->priv->listeners);
  self->priv->listeners = NULL;
}

static void
resolve_pointer_real (EvdPromise     *self,
                      gpointer        data,
                      GDestroyNotify  data_free_func)
{
  g_return_if_fail (! self->priv->completed);

  self->priv->res_pointer = data;
  self->priv->res_pointer_free_func = data_free_func;
}

static void
resolve_size_real (EvdPromise *self, gssize size)
{
  g_return_if_fail (! self->priv->completed);

  self->priv->res_size = size;
}

static void
resolve_boolean_real (EvdPromise *self, gboolean bool)
{
  g_return_if_fail (! self->priv->completed);

  self->priv->res_boolean = bool;
}

static void
reject_real (EvdPromise *self, GError *error)
{
  g_return_if_fail (! self->priv->completed);

  self->priv->res_error = error;
}

static EvdPromise *
evd_promise_new (GObject      *source_object,
                 GCancellable *cancellable,
                 gpointer      tag)
{
  EvdPromise *self;

  self = g_object_new (EVD_TYPE_PROMISE, NULL);

  if (source_object != NULL)
    self->priv->src_obj = g_object_ref (source_object);

  if (cancellable != NULL)
    self->priv->cancellable = cancellable;

  self->priv->tag = tag;

  return self;
}

static gboolean
call_listener_in_idle (gpointer user_data)
{
  PromiseClosure *closure = user_data;

  closure->callback (closure->promise->priv->src_obj,
                     G_ASYNC_RESULT (closure->promise),
                     closure->user_data);

  free_promise_closure (closure);

  return FALSE;
}

static gboolean
deferred_complete_in_idle_cb (gpointer user_data)
{
  EvdDeferred *self = user_data;

  evd_promise_notify_completion (self->promise);

  return FALSE;
}

static void
deferred_free (EvdDeferred *self)
{
  g_object_unref (self->promise);
  g_free (self);
}

/* public methods */

/**
 * evd_promise_then:
 * @self: An #EvdPromise object
 * @callback: (scope async): Function to call when the promise is resolved
 * @user_data: (allow-none): Application data to pass in @callback
 *
 * Adds a new listener function to the asynchronous operation represented by the
 * promise. If the operation has not yet completed, @callback will be called
 * together with all the other listeners as soon as it completes, in the
 * same order as the listeners were added. If the operation already completed,
 * @callback will be called immediately on the next turn of the event loop.
 **/
void
evd_promise_then (EvdPromise          *self,
                  GAsyncReadyCallback  callback,
                  gpointer             user_data)
{
  PromiseClosure *closure;

  g_return_if_fail (EVD_IS_PROMISE (self));
  g_return_if_fail (callback != NULL);

  closure = g_new0 (PromiseClosure, 1);
  closure->promise = g_object_ref (self);
  closure->callback = callback;
  closure->user_data = user_data;

  /* this is to make g_async_result_get_user_data() work */
  self->priv->user_data = user_data;

  if (self->priv->completed)
    g_idle_add (call_listener_in_idle, closure);
  else
    self->priv->listeners = g_list_append (self->priv->listeners, closure);
}

/**
 * evd_promise_get_result_pointer:
 * @self: An #EvdPromise object
 *
 * Retrieves the result of the asynchronous operation represented by the
 * promise if it is held as a gpointer, otherwise returns %NULL. It is an
 * error to call this method before the promise has been resolved.
 *
 * Returns: (transfer none): The result of the operation as a gpointer, or %NULL
 **/
gpointer
evd_promise_get_result_pointer (EvdPromise *self)
{
  g_return_val_if_fail (EVD_IS_PROMISE (self), NULL);

  WARN_IF_NOT_COMPLETED(self);

  return self->priv->res_pointer;
}

/**
 * evd_promise_get_result_size:
 * @self: An #EvdPromise object
 *
 * Retrieves the result of the asynchronous operation represented by the
 * promise if it is held as gssize, otherwise returns zero. It is an
 * error to call this method before the promise has been resolved.
 *
 * Returns: (transfer none): The result of the operation as a gssize, or zero
 **/
gssize
evd_promise_get_result_size (EvdPromise *self)
{
  g_return_val_if_fail (EVD_IS_PROMISE (self), NULL);

  WARN_IF_NOT_COMPLETED(self);

  return self->priv->res_size;
}

/**
 * evd_promise_get_result_boolean:
 * @self: An #EvdPromise object
 *
 * Retrieves the result of the asynchronous operation represented by the
 * promise if it is held as gboolean, otherwise returns %FALSE. It is an
 * error to call this method before the promise has been resolved.
 *
 * Returns: (transfer none): The result of the operation as a gboolean,
 *   or %FALSE
 **/
gboolean
evd_promise_get_result_boolean (EvdPromise *self)
{
  g_return_val_if_fail (EVD_IS_PROMISE (self), NULL);

  WARN_IF_NOT_COMPLETED(self);

  return self->priv->res_boolean;
}

/**
 * evd_promise_propagate_error:
 * @self: An #EvdPromise object
 * @error: (allow-none): A pointer to a #GError to retrieve the error, or %NULL
 *
 * Tells whether an asynchronous operation failed, in which case %TRUE is
 * returned and the resulting error copied into @error. Otherwise returns %FALSE.
 *
 * It is an error to call this method before the promise has been resolved.
 *
 * Returns: %TRUE if an error was propagated, %FALSE otherwise
 **/
gboolean
evd_promise_propagate_error (EvdPromise *self, GError **error)
{
  g_return_val_if_fail (EVD_IS_PROMISE (self), NULL);

  WARN_IF_NOT_COMPLETED(self);

  if (self->priv->res_error == NULL)
    return FALSE;

  g_propagate_error (error, g_error_copy (self->priv->res_error));

  return TRUE;
}

/**
 * evd_promise_cancel:
 * @self: An #EvdPromise object
 *
 * Cancels the asynchronous operation represented by the promise by calling
 * g_cancellable_cancel() on the #GCancellable associated with the promise,
 * if it is not %NULL.
 **/
void
evd_promise_cancel (EvdPromise *self)
{
  g_return_if_fail (EVD_IS_PROMISE (self));

  if (self->priv->cancellable == NULL)
    return;

  g_cancellable_cancel (self->priv->cancellable);
}

/**
 * evd_promise_get_cancellable:
 * @self: An #EvdPromise object
 *
 * Obtains the #GCancellable object associated with the promise, which can
 * be %NULL. Normally, the cancellable is passed to the function that triggered
 * the asynchronous operation represented by the promise.
 *
 * Returns: (transfer none): The #GCancellable, or %NULL
 **/
GCancellable *
evd_promise_get_cancellable (EvdPromise *self)
{
  g_return_val_if_fail (EVD_IS_PROMISE (self), NULL);

  return self->priv->cancellable;
}

/**
 * evd_deferred_new:
 * @source_object: (allow-none): The #GObject performing the async operation,
 *   or %NULL
 * @cancellable: (allow-none): A #GCancellable object, or %NULL
 * @tag: (allow-none): An arbitrary pointer identifying the async operation,
 *   or %NULL
 *
 * Creates a new deferred object to track the execution of an
 * asynchronous operation. It works like #GSimpleAsyncResult, but with some
 * important differences.
 *
 * #EvdDeferred does not represent itself the result of the asynchronous
 * operation. Instead, it delegates on #EvdPromise, which is a
 * #GAsyncResult, all the functionality except the ability to set the result
 * and complete the operation. This way, the #EvdPromise can be made public
 * to the application, while only the #EvdDeferred object is kept private in
 * the implementation as with #GSimpleAsyncResult.
 *
 * An #EvdDeferred and its associated #EvdPromise are bound together so that
 * only a deferred object can resolve or reject its associated
 * promise. The promise of a deferred object can be obtained with
 * evd_deferred_get_promise().
 *
 * Returns: (transfer full): A new #EvdDeferred, to be freed with
 *   evd_deferred_unref()
 **/
EvdDeferred *
evd_deferred_new (GObject      *source_object,
                  GCancellable *cancellable,
                  gpointer      tag)
{
  EvdDeferred *self;

  g_return_val_if_fail (G_IS_OBJECT (source_object) || source_object == NULL,
                        NULL);
  g_return_val_if_fail (G_IS_CANCELLABLE (cancellable) || cancellable == NULL,
                        NULL);

  self = g_new0 (EvdDeferred, 1);

  self->ref_count = 1;
  self->completed = FALSE;
  self->promise = evd_promise_new (source_object,
                                   cancellable,
                                   tag);
  self->resolve_funcs = steal_resolve_funcs (self->promise);

  return self;
}

/**
 * evd_deferred_ref:
 * @self: An #EvdDeferred object
 *
 * Increases the reference count of the deferred object.
 *
 * Returns: (transfer full): The same #EvdDeferred object
 **/
EvdDeferred *
evd_deferred_ref (EvdDeferred *self)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (self->ref_count > 0, NULL);

  g_atomic_int_add (&self->ref_count, 1);

  return self;
}

/**
 * evd_deferred_unref:
 * @self: An #EvdDeferred object
 *
 * Decreases the reference count of the deferred. If it reaches
 * zero, the object is destroyed and all its memory released.
 **/
void
evd_deferred_unref (EvdDeferred *self)
{
  gint old_ref;

  g_return_if_fail (self != NULL);
  g_return_if_fail (self->ref_count > 0);

  old_ref = g_atomic_int_get (&self->ref_count);
  if (old_ref > 1)
    g_atomic_int_compare_and_exchange (&self->ref_count, old_ref, old_ref - 1);
  else
    deferred_free (self);
}

/**
 * evd_deferred_get_promise:
 * @self: An #EvdDeferred object
 *
 * Retrieves the promise object associated with the deferred.
 *
 * Returns: (transfer none): The #EvdPromise, owned by the deferred object
 **/
EvdPromise *
evd_deferred_get_promise (EvdDeferred *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  return self->promise;
}

/**
 * evd_deferred_set_result_pointer:
 * @self: An #EvdDeferred object
 * @data: (allow-none): The result of the async operation as a gpointer
 * @data_free_func: (allow-none): #GDestroyNotify callback to free @data,
 *   or %NULL
 *
 * Sets the result of the asynchronous operation as an arbitrary pointer of
 * data. @data_free_func, if provided, will be called when @data is no longer
 * used.
 *
 * This method does not completes the operation. evd_deferred_complete() or
 * evd_deferred_complete_in_idle() should be called after for that purpose.
 **/
void
evd_deferred_set_result_pointer (EvdDeferred    *self,
                                 gpointer        data,
                                 GDestroyNotify  data_free_func)
{
  g_return_if_fail (self != NULL);

  self->resolve_funcs->resolve_pointer (self->promise, data, data_free_func);
}

/**
 * evd_deferred_set_result_size:
 * @self: An #EvdDeferred object
 * @size: The result of the async operation as a gssize
 *
 * Sets the result of the asynchronous operation as a long signed integer,
 * useful for size results.
 *
 * This method does not completes the operation. evd_deferred_complete() or
 * evd_deferred_complete_in_idle() should be called after for that purpose.
 **/
void
evd_deferred_set_result_size (EvdDeferred *self, gssize size)
{
  g_return_if_fail (self != NULL);

  self->resolve_funcs->resolve_size (self->promise, size);
}

/**
 * evd_deferred_set_result_boolean:
 * @self: An #EvdDeferred object
 * @bool: (allow-none): The result of the async operation as a gboolean
 *
 * Sets the result of the asynchronous operation as a boolean value.
 *
 * This method does not completes the operation. evd_deferred_complete() or
 * evd_deferred_complete_in_idle() should be called after for that purpose.
 **/
void
evd_deferred_set_result_boolean (EvdDeferred *self, gboolean bool)
{
  g_return_if_fail (self != NULL);

  self->resolve_funcs->resolve_boolean (self->promise, bool);
}

/**
 * evd_deferred_take_result_error:
 * @self: An #EvdDeferred object
 * @error: The result of the async operation as a #GError
 *
 * Sets the result of the asynchronous operation as an error, indicating that
 * the operation failed. The deferred object takes ownership of @error.
 *
 * This method does not completes the operation. evd_deferred_complete() or
 * evd_deferred_complete_in_idle() should be called after for that purpose.
 **/
void
evd_deferred_take_result_error (EvdDeferred *self, GError *error)
{
  g_return_if_fail (self != NULL);

  self->resolve_funcs->reject (self->promise, error);
}

/**
 * evd_deferred_complete:
 * @self: An #EvdDeferred object
 *
 * Completes the asynchronous operation represented by the deferred object,
 * immediately calling all the listener callbacks added to the associated
 * #EvdPromise object.
 *
 * This method must not be used if the operation is completed on the same
 * event loop cycle. For those cases, evd_deferred_complete_in_idle()
 * is provided.
 **/
void
evd_deferred_complete (EvdDeferred *self)
{
  g_return_if_fail (self != NULL);

  if (self->completed)
    return;

  self->completed = TRUE;

  evd_promise_notify_completion (self->promise);
}

/**
 * evd_deferred_complete_in_idle:
 * @self: An #EvdDeferred object
 *
 * Works as evd_deferred_complete(), but defers the actual completion
 * to the next event loop cycle.
 **/
void
evd_deferred_complete_in_idle (EvdDeferred *self)
{
  g_return_if_fail (self != NULL);

  if (self->completed)
    return;

  self->completed = TRUE;

  g_idle_add (deferred_complete_in_idle_cb, self);
}
