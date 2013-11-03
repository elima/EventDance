/*
 * evd-buffered-output-stream.h
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

#ifndef __EVD_BUFFERED_OUTPUT_STREAM_H__
#define __EVD_BUFFERED_OUTPUT_STREAM_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _EvdBufferedOutputStream EvdBufferedOutputStream;
typedef struct _EvdBufferedOutputStreamClass EvdBufferedOutputStreamClass;
typedef struct _EvdBufferedOutputStreamPrivate EvdBufferedOutputStreamPrivate;

struct _EvdBufferedOutputStream
{
  GFilterOutputStream parent;

  EvdBufferedOutputStreamPrivate *priv;
};

struct _EvdBufferedOutputStreamClass
{
  GFilterOutputStreamClass parent_class;

  /* padding for future expansion */
  void (* _padding_0_) (void);
  void (* _padding_1_) (void);
  void (* _padding_2_) (void);
  void (* _padding_3_) (void);
  void (* _padding_4_) (void);
  void (* _padding_5_) (void);
  void (* _padding_6_) (void);
  void (* _padding_7_) (void);
};

#define EVD_TYPE_BUFFERED_OUTPUT_STREAM           (evd_buffered_output_stream_get_type ())
#define EVD_BUFFERED_OUTPUT_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_BUFFERED_OUTPUT_STREAM, EvdBufferedOutputStream))
#define EVD_BUFFERED_OUTPUT_STREAM_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_BUFFERED_OUTPUT_STREAM, EvdBufferedOutputStreamClass))
#define EVD_IS_BUFFERED_OUTPUT_STREAM(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_BUFFERED_OUTPUT_STREAM))
#define EVD_IS_BUFFERED_OUTPUT_STREAM_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_BUFFERED_OUTPUT_STREAM))
#define EVD_BUFFERED_OUTPUT_STREAM_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_BUFFERED_OUTPUT_STREAM, EvdBufferedOutputStreamClass))


GType                   evd_buffered_output_stream_get_type          (void) G_GNUC_CONST;

EvdBufferedOutputStream *evd_buffered_output_stream_new              (GOutputStream *base_stream);

gssize                  evd_buffered_output_stream_write_str_sync    (EvdBufferedOutputStream  *self,
                                                                      const gchar              *buffer,
                                                                      GError                  **error);

void                    evd_buffered_output_stream_write_str         (EvdBufferedOutputStream *self,
                                                                      const gchar             *buffer,
                                                                      int                      io_priority,
                                                                      GCancellable            *cancellable,
                                                                      GAsyncReadyCallback      callback,
                                                                      gpointer                 user_data);
gssize                  evd_buffered_output_stream_write_str_finish  (EvdBufferedOutputStream  *self,
                                                                      GAsyncResult             *result,
                                                                      GError                  **error);

void                    evd_buffered_output_stream_set_auto_flush    (EvdBufferedOutputStream *self,
                                                                      gboolean                 auto_flush);
gboolean                evd_buffered_output_stream_get_auto_flush    (EvdBufferedOutputStream *self);

void                    evd_buffered_output_stream_notify_write      (EvdBufferedOutputStream *self);

G_END_DECLS

#endif /* __EVD_BUFFERED_OUTPUT_STREAM_H__ */
