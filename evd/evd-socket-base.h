/*
 * evd-socket-base.h
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

#ifndef __EVD_SOCKET_BASE_H__
#define __EVD_SOCKET_BASE_H__

#include <glib-object.h>

#include "evd-tls-session.h"

G_BEGIN_DECLS

typedef struct _EvdSocketBase EvdSocketBase;
typedef struct _EvdSocketBaseClass EvdSocketBaseClass;
typedef struct _EvdSocketBasePrivate EvdSocketBasePrivate;

struct _EvdSocketBase
{
  GObject parent;

  /* private structure */
  EvdSocketBasePrivate *priv;
};

struct _EvdSocketBaseClass
{
  GObjectClass parent_class;

  GClosureMarshal read_handler_marshal;
  GClosureMarshal write_handler_marshal;

  /* virtual methods */
  void (* copy_properties)       (EvdSocketBase *self, EvdSocketBase *target);
  void (* read_closure_changed)  (EvdSocketBase *self);
  void (* write_closure_changed) (EvdSocketBase *self);
};

#define EVD_TYPE_SOCKET_BASE           (evd_socket_base_get_type ())
#define EVD_SOCKET_BASE(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_SOCKET_BASE, EvdSocketBase))
#define EVD_SOCKET_BASE_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_SOCKET_BASE, EvdSocketBaseClass))
#define EVD_IS_SOCKET_BASE(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_SOCKET_BASE))
#define EVD_IS_SOCKET_BASE_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_SOCKET_BASE))
#define EVD_SOCKET_BASE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_SOCKET_BASE, EvdSocketBaseClass))


GType          evd_socket_base_get_type                (void) G_GNUC_CONST;

/**
 * evd_socket_base_set_read_handler:
 * @self: The #EvdSocketBase.
 * @callback: (allow-none): The #GCallback to call upon read condition.
 * @user_data: Pointer to arbitrary data to pass in @callback.
 *
 * Specifies a pointer to the function to be invoked when data is waiting
 * to be read from the stream.
 */
void           evd_socket_base_set_read_handler        (EvdSocketBase *self,
                                                        GCallback  callback,
                                                        gpointer   user_data);

/**
 * evd_socket_base_set_on_read:
 * @self: The #EvdSocketBase.
 * @closure: (in) (allow-none): The #GClosure to be invoked.
 *
 * Specifies the closure to be invoked when data is waiting to be read from the
 * stream.
 */
void           evd_socket_base_set_on_read             (EvdSocketBase *self,
                                                        GClosure  *closure);

/**
 * evd_socket_base_get_on_read:
 * @self: The #EvdSocketBase.
 *
 * Return value: (transfer none): A #GClosure representing the current read handler,
 * or NULL.
 */
GClosure      *evd_socket_base_get_on_read             (EvdSocketBase *self);

/**
 * evd_socket_base_set_write_handler:
 * @self: The #EvdSocketBase.
 * @callback: (allow-none): The #GCallback to call upon write condition.
 * @user_data: Pointer to arbitrary data to pass in @callback.
 *
 * Specifies a pointer to the function to be invoked when it becomes safe to
 * write data to the stream.
 */
void           evd_socket_base_set_write_handler       (EvdSocketBase *self,
                                                        GCallback      callback,
                                                        gpointer       user_data);

/**
 * evd_socket_base_set_on_write:
 * @self: The #EvdSocketBase.
 * @closure: (in) (allow-none): The #GClosure to be invoked.
 *
 * Specifies the closure to be invoked when it becomes safe to write data to the
 * stream.
 */
void           evd_socket_base_set_on_write            (EvdSocketBase *self,
                                                        GClosure      *closure);

/**
 * evd_socket_base_get_on_write:
 * @self: The #EvdSocketBase.
 *
 * Return value: (transfer none): A #GClosure representing the current write handler,
 * or NULL.
 */
GClosure      *evd_socket_base_get_on_write            (EvdSocketBase *self);

gsize          evd_socket_base_request_write           (EvdSocketBase *self,
                                                        gsize          size,
                                                        guint         *wait);
gsize          evd_socket_base_request_read            (EvdSocketBase *self,
                                                        gsize          size,
                                                        guint         *wait);

gulong         evd_socket_base_get_total_read          (EvdSocketBase *self);
gulong         evd_socket_base_get_total_written       (EvdSocketBase *self);

gfloat         evd_socket_base_get_actual_bandwidth_in  (EvdSocketBase *self);
gfloat         evd_socket_base_get_actual_bandwidth_out (EvdSocketBase *self);

G_END_DECLS

#endif /* __EVD_SOCKET_BASE_H__ */
