/*
 * evd-buffered-input-stream.c
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

#include <string.h>

#include "evd-error.h"
#include "evd-utils.h"
#include "evd-buffered-input-stream.h"

G_DEFINE_TYPE (EvdBufferedInputStream,
               evd_buffered_input_stream,
               G_TYPE_BUFFERED_INPUT_STREAM)

#define EVD_BUFFERED_INPUT_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                                    EVD_TYPE_BUFFERED_INPUT_STREAM, \
                                                    EvdBufferedInputStreamPrivate))

/* private data */
struct _EvdBufferedInputStreamPrivate
{
  GString *buffer;

  GSimpleAsyncResult *async_result;
  void *async_buffer;
  gsize requested_size;
  gssize actual_size;

  guint read_src_id;

  gboolean frozen;
};

static void     evd_buffered_input_stream_class_init         (EvdBufferedInputStreamClass *class);
static void     evd_buffered_input_stream_init               (EvdBufferedInputStream *self);
static void     evd_buffered_input_stream_finalize           (GObject *obj);

static gssize   evd_buffered_input_stream_read               (GInputStream  *stream,
                                                              void          *buffer,
                                                              gsize          size,
                                                              GCancellable  *cancellable,
                                                              GError       **error);
static void     evd_buffered_input_stream_read_async         (GInputStream        *stream,
                                                              void                *buffer,
                                                              gsize                count,
                                                              int                  io_priority,
                                                              GCancellable        *cancellable,
                                                              GAsyncReadyCallback  callback,
                                                              gpointer             user_data);
static gssize   evd_buffered_input_stream_read_finish        (GInputStream  *self,
                                                              GAsyncResult  *result,
                                                              GError       **error);

static gboolean evd_buffered_input_stream_close              (GInputStream  *stream,
                                                              GCancellable  *cancellable,
                                                              GError       **error);

static void
evd_buffered_input_stream_class_init (EvdBufferedInputStreamClass *class)
{
  GObjectClass *obj_class;
  GInputStreamClass *input_stream_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_buffered_input_stream_finalize;

  input_stream_class = G_INPUT_STREAM_CLASS (class);
  input_stream_class->read_fn = evd_buffered_input_stream_read;
  input_stream_class->read_async = evd_buffered_input_stream_read_async;
  input_stream_class->read_finish = evd_buffered_input_stream_read_finish;
  input_stream_class->close_fn = evd_buffered_input_stream_close;

  g_type_class_add_private (obj_class, sizeof (EvdBufferedInputStreamPrivate));
}

static void
evd_buffered_input_stream_init (EvdBufferedInputStream *self)
{
  EvdBufferedInputStreamPrivate *priv;

  priv = EVD_BUFFERED_INPUT_STREAM_GET_PRIVATE (self);
  self->priv = priv;

  priv->buffer = g_string_new ("");

  priv->async_result = NULL;
  priv->async_buffer = NULL;
  priv->requested_size = 0;
  priv->actual_size = 0;

  priv->read_src_id = 0;

  priv->frozen = FALSE;
}

static void
evd_buffered_input_stream_finalize (GObject *obj)
{
  EvdBufferedInputStream *self = EVD_BUFFERED_INPUT_STREAM (obj);

  if (self->priv->read_src_id != 0)
    g_source_remove (self->priv->read_src_id);

  g_string_free (self->priv->buffer, TRUE);

  if (self->priv->async_result != NULL)
    g_object_unref (self->priv->async_result);

  G_OBJECT_CLASS (evd_buffered_input_stream_parent_class)->finalize (obj);
}

static gssize
evd_buffered_input_stream_read (GInputStream  *stream,
                                void          *buffer,
                                gsize          size,
                                GCancellable  *cancellable,
                                GError       **error)
{
  EvdBufferedInputStream *self = EVD_BUFFERED_INPUT_STREAM (stream);
  gchar *buf;
  gssize read_from_buf = 0;
  gssize read_from_stream = 0;

  if (self->priv->frozen)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_WOULD_BLOCK,
                           "Resource temporarily unavailable");
      return -1;
    }

  /* read from buffer first */
  if (self->priv->buffer->len > 0)
    {
      read_from_buf = MIN (self->priv->buffer->len, size);
      g_memmove (buffer, self->priv->buffer->str, read_from_buf);
      size -= read_from_buf;

      buf = buffer + read_from_buf;
    }
  else
    {
      buf = buffer;
    }

  /* if not enough, read from base stream */
  if (size > 0)
    {
      GInputStream *base_stream;
      GError *_error = NULL;

      base_stream =
        g_filter_input_stream_get_base_stream (G_FILTER_INPUT_STREAM (self));

      read_from_stream = g_input_stream_read (base_stream,
                                              buf,
                                              size,
                                              cancellable,
                                              &_error);
      if (read_from_stream < 0)
        {
          if (read_from_buf > 0)
            {
              g_clear_error (&_error);
              read_from_stream = 0;
            }
          else
            {
              g_propagate_error (error, _error);
            }
        }
    }

  if (read_from_stream >= 0 && read_from_buf > 0)
    g_string_erase (self->priv->buffer, 0, read_from_buf);

  return read_from_stream + read_from_buf;
}

static gboolean
do_read (gpointer user_data)
{
  EvdBufferedInputStream *self = EVD_BUFFERED_INPUT_STREAM (user_data);
  gssize size;
  GError *error = NULL;

  self->priv->read_src_id = 0;

  if (self->priv->async_result == NULL)
    return FALSE;

  size =
    G_INPUT_STREAM_GET_CLASS (self)->read_fn (G_INPUT_STREAM (self),
                                              self->priv->async_buffer,
                                              self->priv->requested_size,
                                              NULL,
                                              &error);

  if (size < 0 && g_error_matches (error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK))
    {
      g_error_free (error);
      return FALSE;
    }

  if (size != 0)
    {
      GSimpleAsyncResult *res;

      res = self->priv->async_result;
      self->priv->async_result = NULL;

      if (size < 0)
        {
          g_simple_async_result_set_from_error (res, error);
          g_error_free (error);
        }
      else
        {
          self->priv->actual_size += size;
        }

      g_input_stream_clear_pending (G_INPUT_STREAM (self));

      g_simple_async_result_complete (res);
      g_object_unref (res);
    }

  return FALSE;
}

static void
evd_buffered_input_stream_read_async (GInputStream        *stream,
                                      void                *buffer,
                                      gsize                size,
                                      int                  io_priority,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
  EvdBufferedInputStream *self = EVD_BUFFERED_INPUT_STREAM (stream);

  self->priv->async_result =
    g_simple_async_result_new (G_OBJECT (stream),
                               callback,
                               user_data,
                               evd_buffered_input_stream_read_async);

  self->priv->async_buffer = buffer;
  self->priv->requested_size = size;
  self->priv->actual_size = 0;

  if (! self->priv->frozen)
    self->priv->read_src_id =
      evd_timeout_add (g_main_context_get_thread_default (),
                       0,
                       io_priority,
                       do_read,
                       self);
}

static gssize
evd_buffered_input_stream_read_finish (GInputStream  *stream,
                                       GAsyncResult  *result,
                                       GError       **error)
{
  EvdBufferedInputStream *self = EVD_BUFFERED_INPUT_STREAM (stream);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
    return self->priv->actual_size;
  else
    return -1;
}

static gboolean
evd_buffered_input_stream_close (GInputStream  *stream,
                                 GCancellable  *cancellable,
                                 GError       **error)
{
  EvdBufferedInputStream *self = EVD_BUFFERED_INPUT_STREAM (stream);

  if (self->priv->read_src_id != 0)
    {
      g_source_remove (self->priv->read_src_id);
      self->priv->read_src_id = 0;
    }

  if (self->priv->async_result != NULL)
    {
      GSimpleAsyncResult *res;

      res = self->priv->async_result;
      self->priv->async_result = NULL;

      g_simple_async_result_set_error (res,
                                       G_IO_ERROR,
                                       G_IO_ERROR_CLOSED,
                                       "Buffered input stream closed during async operation");

      g_simple_async_result_complete (res);
      g_object_unref (res);
    }

  return TRUE;
}

/* public methods */

EvdBufferedInputStream *
evd_buffered_input_stream_new (GInputStream *base_stream)
{
  EvdBufferedInputStream *self;

  g_return_val_if_fail (G_IS_INPUT_STREAM (base_stream), NULL);

  self = g_object_new (EVD_TYPE_BUFFERED_INPUT_STREAM,
                       "base-stream", base_stream,
                       NULL);

  return self;
}

/**
 * evd_buffered_input_stream_unread:
 * @self: The #EvdConnection to unread data to.
 * @buffer: (transfer none): Buffer holding the data to be unread. Can contain nulls.
 * @size: Number of bytes to unread.
 * @cancellable: A #GCancellable object, or NULL.
 * @error: (out) (transfer full): A pointer to a #GError to return, or NULL.
 *
 * Stores @size bytes from @buffer in the local read buffer of the socket. Next calls
 * to read will first get data from the local buffer, before performing the actual read
 * operation. This is useful when one needs to do some action with a data just read, but doesn't
 * want to remove the data from the input stream of the socket.
 *
 * Normally, it would be used to write back data that was previously read, to made it available
 * in further calls to read. But in practice any data can be unread.
 *
 * This feature was implemented basically to provide type-of-stream detection on a socket
 * (e.g. a service selector).
 *
 * Returns: The actual number of bytes unread.
 **/
gssize
evd_buffered_input_stream_unread (EvdBufferedInputStream  *self,
                                  const void              *buffer,
                                  gsize                    size,
                                  GCancellable            *cancellable,
                                  GError                 **error)
{
  g_return_val_if_fail (EVD_IS_BUFFERED_INPUT_STREAM (self), -1);

  if (size == 0)
    return 0;

  g_return_val_if_fail (buffer != NULL, 0);

  if (self->priv->buffer->len + size >
      g_buffered_input_stream_get_buffer_size (G_BUFFERED_INPUT_STREAM (self)))
    {
      if (error != NULL)
        *error = g_error_new (EVD_ERROR,
                              EVD_ERROR_BUFFER_FULL,
                              "Buffer is full");

      return -1;
    }
  else
    {
      g_string_prepend_len (self->priv->buffer, buffer, size);

      if (! self->priv->frozen)
        evd_buffered_input_stream_thaw (self, G_PRIORITY_DEFAULT);

      return size;
    }
}

/**
 * evd_buffered_input_stream_read_str_sync:
 * @size: (inout):
 **/
gchar *
evd_buffered_input_stream_read_str_sync (EvdBufferedInputStream *self,
                                         gssize                 *size,
                                         GError                **error)
{
  void *buf;
  gssize actual_size;
  gchar *data = NULL;

  g_return_val_if_fail (EVD_IS_BUFFERED_INPUT_STREAM (self), NULL);
  g_return_val_if_fail (size != NULL, NULL);

  if (*size == 0)
    return NULL;

  buf = g_slice_alloc ((*size) + 1);

  if ( (actual_size = g_input_stream_read (G_INPUT_STREAM (self),
                                           buf,
                                           *size,
                                           NULL,
                                           error)) >= 0)
    {
      if (actual_size > 0)
        {
          data = g_new (gchar, actual_size + 1);
          g_memmove (data, buf, actual_size);
          data[actual_size] = '\0';
        }

      g_slice_free1 ((*size) + 1, buf);
      *size = actual_size;
    }
  else
    {
      (*size) = 0;
    }

  return data;
}

/**
 * evd_buffered_input_stream_read_str:
 * @callback: (scope async): the #GAsyncReadyCallback
 **/
void
evd_buffered_input_stream_read_str (EvdBufferedInputStream *self,
                                    gsize                   size,
                                    int                     io_priority,
                                    GCancellable           *cancellable,
                                    GAsyncReadyCallback     callback,
                                    gpointer                user_data)
{
  g_return_if_fail (EVD_IS_BUFFERED_INPUT_STREAM (self));

  self->priv->async_buffer = g_new0 (gchar, size + 1);

  g_input_stream_read_async (G_INPUT_STREAM (self),
                             self->priv->async_buffer,
                             size,
                             io_priority,
                             cancellable,
                             callback,
                             user_data);
}

/**
 * evd_buffered_input_stream_read_str_finish:
 * @size: (out):
 *
 * Returns:
 **/
gchar *
evd_buffered_input_stream_read_str_finish (EvdBufferedInputStream  *self,
                                           GAsyncResult            *result,
                                           gssize                  *size,
                                           GError                 **error)
{
  gchar *buf = NULL;
  gssize _size;

  g_return_val_if_fail (EVD_IS_BUFFERED_INPUT_STREAM (self), NULL);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

  if ( (_size = g_input_stream_read_finish (G_INPUT_STREAM (self),
                                            result,
                                            error)) < 0)
    {
      g_free (self->priv->async_buffer);
    }
  else
    {
      buf = self->priv->async_buffer;
    }

  if (size != NULL)
    *size = _size;

  self->priv->async_buffer = NULL;

  return buf;
}

void
evd_buffered_input_stream_freeze (EvdBufferedInputStream *self)
{
  g_return_if_fail (EVD_IS_BUFFERED_INPUT_STREAM (self));

  self->priv->frozen = TRUE;
}

void
evd_buffered_input_stream_thaw (EvdBufferedInputStream *self,
                                gint                    priority)
{
  g_return_if_fail (EVD_IS_BUFFERED_INPUT_STREAM (self));

  self->priv->frozen = FALSE;

  if (self->priv->async_result != NULL && self->priv->read_src_id == 0)
    self->priv->read_src_id =
      evd_timeout_add (g_main_context_get_thread_default (),
                       0,
                       priority,
                       do_read,
                       self);
}
