/*
 * evd-buffered-input-stream.h
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

#ifndef __EVD_BUFFERED_INPUT_STREAM_H__
#define __EVD_BUFFERED_INPUT_STREAM_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct _EvdBufferedInputStream EvdBufferedInputStream;
typedef struct _EvdBufferedInputStreamClass EvdBufferedInputStreamClass;
typedef struct _EvdBufferedInputStreamPrivate EvdBufferedInputStreamPrivate;

struct _EvdBufferedInputStream
{
  GBufferedInputStream parent;

  EvdBufferedInputStreamPrivate *priv;
};

struct _EvdBufferedInputStreamClass
{
  GBufferedInputStreamClass parent_class;
};

#define EVD_TYPE_BUFFERED_INPUT_STREAM           (evd_buffered_input_stream_get_type ())
#define EVD_BUFFERED_INPUT_STREAM(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), EVD_TYPE_BUFFERED_INPUT_STREAM, EvdBufferedInputStream))
#define EVD_BUFFERED_INPUT_STREAM_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), EVD_TYPE_BUFFERED_INPUT_STREAM, EvdBufferedInputStreamClass))
#define EVD_IS_BUFFERED_INPUT_STREAM(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EVD_TYPE_BUFFERED_INPUT_STREAM))
#define EVD_IS_BUFFERED_INPUT_STREAM_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), EVD_TYPE_BUFFERED_INPUT_STREAM))
#define EVD_BUFFERED_INPUT_STREAM_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), EVD_TYPE_BUFFERED_INPUT_STREAM, EvdBufferedInputStreamClass))


GType                   evd_buffered_input_stream_get_type          (void) G_GNUC_CONST;

EvdBufferedInputStream *evd_buffered_input_stream_new               (GInputStream *base_stream);

/**
 * evd_buffered_input_stream_unread:
 * @buffer: (type utf8):
 **/
gssize                  evd_buffered_input_stream_unread            (EvdBufferedInputStream  *self,
                                                                     const void              *buffer,
                                                                     gsize                    size,
                                                                     GCancellable            *cancellable,
                                                                     GError                 **error);

/**
 * evd_buffered_input_stream_read_str:
 * @size: (inout):
 * @user_data: (allow-none):
 **/
gchar                  *evd_buffered_input_stream_read_str          (EvdBufferedInputStream *self,
                                                                     gssize                 *size,
                                                                     GError                **error);

/**
 * evd_buffered_input_stream_read_str_async:
 * @callback: (scope async): the #GAsyncReadyCallback
 **/
void                    evd_buffered_input_stream_read_str_async    (EvdBufferedInputStream *stream,
                                                                     gsize                   count,
                                                                     int                     io_priority,
                                                                     GCancellable           *cancellable,
                                                                     GAsyncReadyCallback     callback,
                                                                     gpointer                user_data);

/**
 * evd_buffered_input_stream_read_str_finish:
 * @size: (out):
 *
 * Returns:
 **/
gchar                  *evd_buffered_input_stream_read_str_finish   (EvdBufferedInputStream  *self,
                                                                     GAsyncResult            *result,
                                                                     gssize                  *size,
                                                                     GError                 **error);

void                    evd_buffered_input_stream_notify_read       (EvdBufferedInputStream *self,
                                                                     gint                    priority);

G_END_DECLS

#endif /* __EVD_BUFFERED_INPUT_STREAM_H__ */
