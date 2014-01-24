/*
 * evd-promise.h
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

#ifndef __EVD_PROMISE_H__
#define __EVD_PROMISE_H__

#if !defined (__EVD_H_INSIDE__) && !defined (EVD_COMPILATION)
#error "Only <evd.h> can be included directly."
#endif

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _EvdPromise EvdPromise;
typedef struct _EvdPromiseClass EvdPromiseClass;
typedef struct _EvdPromisePrivate EvdPromisePrivate;
typedef struct _EvdDeferred EvdDeferred;

struct _EvdPromise
{
  GOutputStream parent;

  EvdPromisePrivate *priv;
};

struct _EvdPromiseClass
{
  GOutputStreamClass parent_class;
};

#define EVD_TYPE_PROMISE           (evd_promise_get_type ())
#define EVD_PROMISE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_PROMISE, EvdPromise))
#define EVD_PROMISE_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_PROMISE, EvdPromiseClass))
#define EVD_IS_PROMISE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_PROMISE))
#define EVD_IS_PROMISE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_PROMISE))
#define EVD_PROMISE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_PROMISE, EvdPromiseClass))

GType            evd_promise_get_type             (void) G_GNUC_CONST;

void             evd_promise_then                 (EvdPromise          *self,
                                                   GAsyncReadyCallback  callback,
                                                   gpointer             user_data);

gpointer         evd_promise_get_result_pointer   (EvdPromise *self);
gssize           evd_promise_get_result_size      (EvdPromise *self);
gboolean         evd_promise_get_result_boolean   (EvdPromise *self);
gboolean         evd_promise_propagate_error      (EvdPromise  *self,
                                                   GError     **error);

GCancellable *   evd_promise_get_cancellable      (EvdPromise *self);
void             evd_promise_cancel               (EvdPromise *self);


#define EVD_TYPE_DEFERRED (evd_deferred_get_type ())

GType            evd_deferred_get_type            (void);

EvdDeferred *    evd_deferred_new                 (GObject      *source_object,
                                                   GCancellable *cancellable,
                                                   gpointer      tag);
EvdDeferred *    evd_deferred_ref                 (EvdDeferred *self);
void             evd_deferred_unref               (EvdDeferred *self);

EvdPromise *     evd_deferred_get_promise         (EvdDeferred *self);

void             evd_deferred_set_result_pointer  (EvdDeferred    *self,
                                                   gpointer        data,
                                                   GDestroyNotify  data_free_func);
void             evd_deferred_set_result_size     (EvdDeferred *self,
                                                   gssize       size);
void             evd_deferred_set_result_boolean  (EvdDeferred *self,
                                                   gboolean     bool);
void             evd_deferred_take_result_error   (EvdDeferred *self,
                                                   GError      *error);

void             evd_deferred_complete            (EvdDeferred *self);
void             evd_deferred_complete_in_idle    (EvdDeferred *self);

G_END_DECLS

#endif /* __EVD_PROMISE_H__ */
