/*
 * evd-output-stream.h
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
 * version 3, or (at your option) any later version as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License at http://www.gnu.org/licenses/lgpl-3.0.txt
 * for more details.
 */

#ifndef __EVD_OUTPUT_STREAM_H__
#define __EVD_OUTPUT_STREAM_H__

#if !defined (__EVD_H_INSIDE__) && !defined (EVD_COMPILATION)
#error "Only <evd.h> can be included directly."
#endif

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _EvdOutputStream          EvdOutputStream;
typedef struct _EvdOutputStreamInterface EvdOutputStreamInterface;

struct _EvdOutputStreamInterface
{
  GTypeInterface parent_iface;

  /* virtual methods */
  gssize  (* write_fn)         (EvdOutputStream  *self,
                                const void       *buffer,
                                gsize             size,
                                GError          **error);
  void    (* close_fn)         (EvdOutputStream  *self);
  void    (* flush_fn)         (EvdOutputStream *self,
                                GAsyncResult    *result,
                                GCancellable    *cancellable);
  gsize   (* get_max_writable) (EvdOutputStream *self);

  /* signals */
  void (* signal_close)        (EvdOutputStream *self,
                                gpointer         user_data);

  /* members */
  gboolean is_closed;
  gboolean has_pending;

  /* padding for future expansion */
  void (* _padding_0_) (void);
  void (* _padding_1_) (void);
  void (* _padding_2_) (void);
  void (* _padding_3_) (void);
};

#define EVD_TYPE_OUTPUT_STREAM                 (evd_output_stream_get_type ())
#define EVD_OUTPUT_STREAM(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_OUTPUT_STREAM, EvdOutputStream))
#define EVD_IS_OUTPUT_STREAM(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_OUTPUT_STREAM))
#define EVD_OUTPUT_STREAM_GET_INTERFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE ((inst), EVD_TYPE_OUTPUT_STREAM, EvdOutputStreamInterface))

GType           evd_output_stream_get_type         (void);

gssize          evd_output_stream_write            (EvdOutputStream  *self,
                                                    const void       *buffer,
                                                    gsize             size,
                                                    GError          **error);
void            evd_output_stream_close            (EvdOutputStream *self);
void            evd_output_stream_flush            (EvdOutputStream     *self,
                                                    GCancellable        *cancellable,
                                                    GAsyncReadyCallback  callback,
                                                    gpointer             user_data);
gboolean        evd_output_stream_flush_finish     (EvdOutputStream  *self,
                                                    GAsyncResult     *result,
                                                    GError          **error);
gsize           evd_output_stream_get_max_writable (EvdOutputStream *self);

G_END_DECLS

#endif /* __EVD_OUTPUT_STREAM_H__ */
