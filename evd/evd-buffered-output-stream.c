/*
 * evd-buffered-output-stream.c
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

#include <string.h>

#include "evd-error.h"
#include "evd-utils.h"
#include "evd-buffered-output-stream.h"

G_DEFINE_TYPE (EvdBufferedOutputStream,
               evd_buffered_output_stream,
               G_TYPE_BUFFERED_OUTPUT_STREAM)

#define EVD_BUFFERED_OUTPUT_STREAM_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                                     EVD_TYPE_BUFFERED_OUTPUT_STREAM, \
                                                     EvdBufferedOutputStreamPrivate))

/* private data */
struct _EvdBufferedOutputStreamPrivate
{
  GString *buffer;

  gboolean auto_flush;

  GSimpleAsyncResult *async_result;
  gsize requested_size;
  gssize actual_size;

  guint write_src_id;

  gboolean frozen;
};

/* properties */
enum
{
  PROP_0,
  PROP_AUTO_FLUSH
};

static void     evd_buffered_output_stream_class_init         (EvdBufferedOutputStreamClass *class);
static void     evd_buffered_output_stream_init               (EvdBufferedOutputStream *self);
static void     evd_buffered_output_stream_finalize           (GObject *obj);

static void     evd_buffered_output_stream_set_property       (GObject      *obj,
                                                               guint         prop_id,
                                                               const GValue *value,
                                                               GParamSpec   *pspec);
static void     evd_buffered_output_stream_get_property       (GObject    *obj,
                                                               guint       prop_id,
                                                               GValue     *value,
                                                               GParamSpec *pspec);

static gssize   evd_buffered_output_stream_write              (GOutputStream  *stream,
                                                               const void     *buffer,
                                                               gsize           size,
                                                               GCancellable   *cancellable,
                                                               GError        **error);
static gboolean evd_buffered_output_stream_flush              (GOutputStream  *stream,
                                                               GCancellable   *cancellable,
                                                               GError        **error);

static void     evd_buffered_output_stream_write_async        (GOutputStream       *stream,
                                                               const void          *buffer,
                                                               gsize                size,
                                                               int                  io_priority,
                                                               GCancellable        *cancellable,
                                                               GAsyncReadyCallback  callback,
                                                               gpointer             user_data);
static gssize   evd_buffered_output_stream_write_finish       (GOutputStream  *stream,
                                                               GAsyncResult   *result,
                                                               GError        **error);

static gboolean evd_buffered_output_stream_close              (GOutputStream  *stream,
                                                               GCancellable   *cancellable,
                                                               GError        **error);

static void
evd_buffered_output_stream_class_init (EvdBufferedOutputStreamClass *class)
{
  GObjectClass *obj_class;
  GOutputStreamClass *output_stream_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->finalize = evd_buffered_output_stream_finalize;
  obj_class->get_property = evd_buffered_output_stream_get_property;
  obj_class->set_property = evd_buffered_output_stream_set_property;

  output_stream_class = G_OUTPUT_STREAM_CLASS (class);
  output_stream_class->write_fn = evd_buffered_output_stream_write;
  output_stream_class->flush = evd_buffered_output_stream_flush;
  output_stream_class->write_async = evd_buffered_output_stream_write_async;
  output_stream_class->write_finish = evd_buffered_output_stream_write_finish;
  output_stream_class->close_fn = evd_buffered_output_stream_close;

  g_object_class_install_property (obj_class, PROP_AUTO_FLUSH,
                                   g_param_spec_boolean ("auto-flush",
                                                         "Auto flush",
                                                         "Whether buffered data should be automaticallly flushed",
                                                         TRUE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdBufferedOutputStreamPrivate));
}

static void
evd_buffered_output_stream_init (EvdBufferedOutputStream *self)
{
  EvdBufferedOutputStreamPrivate *priv;

  priv = EVD_BUFFERED_OUTPUT_STREAM_GET_PRIVATE (self);
  self->priv = priv;

  priv->buffer = g_string_new ("");

  priv->auto_flush = TRUE;

  priv->async_result = NULL;
  priv->requested_size = 0;
  priv->actual_size = 0;

  priv->write_src_id = 0;

  priv->frozen = FALSE;
}

static void
evd_buffered_output_stream_finalize (GObject *obj)
{
  EvdBufferedOutputStream *self = EVD_BUFFERED_OUTPUT_STREAM (obj);

  if (self->priv->write_src_id != 0)
    g_source_remove (self->priv->write_src_id);

  g_string_free (self->priv->buffer, TRUE);

  if (self->priv->async_result != NULL)
    g_object_unref (self->priv->async_result);

  G_OBJECT_CLASS (evd_buffered_output_stream_parent_class)->finalize (obj);
}

static void
evd_buffered_output_stream_set_property (GObject      *obj,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  EvdBufferedOutputStream *self;

  self = EVD_BUFFERED_OUTPUT_STREAM (obj);

  switch (prop_id)
    {
    case PROP_AUTO_FLUSH:
      self->priv->auto_flush = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_buffered_output_stream_get_property (GObject    *obj,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  EvdBufferedOutputStream *self;

  self = EVD_BUFFERED_OUTPUT_STREAM (obj);

  switch (prop_id)
    {
    case PROP_AUTO_FLUSH:
      g_value_set_boolean (value, self->priv->auto_flush);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static gsize
evd_buffered_output_stream_fill (EvdBufferedOutputStream  *self,
                                 const gchar              *buf,
                                 gsize                     size)
{
  gsize buf_size;

  buf_size = g_buffered_output_stream_get_buffer_size (G_BUFFERED_OUTPUT_STREAM (self));

  if (self->priv->buffer->len + size > buf_size)
    {
      if (g_buffered_output_stream_get_auto_grow (G_BUFFERED_OUTPUT_STREAM (self)))
        {
          g_buffered_output_stream_set_buffer_size (G_BUFFERED_OUTPUT_STREAM (self),
                                                    self->priv->buffer->len + size);
        }
      else
        {
          size = buf_size - self->priv->buffer->len;
        }
    }

  if (size > 0)
    g_string_append_len (self->priv->buffer, buf, size);

  return size;
}

static gssize
evd_buffered_output_stream_write (GOutputStream  *stream,
                                  const void     *buffer,
                                  gsize           size,
                                  GCancellable   *cancellable,
                                  GError        **error)
{
  EvdBufferedOutputStream *self = EVD_BUFFERED_OUTPUT_STREAM (stream);
  gssize actual_size;
  gsize buffered_size = 0;

  if (self->priv->frozen ||
      self->priv->buffer->len > 0 ||
      ! self->priv->auto_flush)
    {
      actual_size = evd_buffered_output_stream_fill (self,
                                                     buffer,
                                                     size);
    }
  else
    {
      GOutputStream *base_stream;

      base_stream =
        g_filter_output_stream_get_base_stream (G_FILTER_OUTPUT_STREAM (stream));

      actual_size = g_output_stream_write (base_stream,
                                           buffer,
                                           size,
                                           cancellable,
                                           error);
      if (actual_size > 0 && actual_size < size)
        {
          buffered_size = evd_buffered_output_stream_fill (self,
                                   (void *) (((guintptr) buffer) + actual_size),
                                   size - actual_size);
        }
    }

  return actual_size + buffered_size;
}

static gboolean
do_write (gpointer user_data)
{
  EvdBufferedOutputStream *self = EVD_BUFFERED_OUTPUT_STREAM (user_data);
  GError *error = NULL;
  gboolean result;

  self->priv->write_src_id = 0;

  result = evd_buffered_output_stream_flush (G_OUTPUT_STREAM (self),
                                             NULL,
                                             &error);

  if (self->priv->buffer->len == 0 && self->priv->async_result != NULL)
    {
      GSimpleAsyncResult *res;

      g_output_stream_clear_pending (G_OUTPUT_STREAM (self));

      res = self->priv->async_result;
      self->priv->async_result = NULL;

      if (! result)
        {
          g_simple_async_result_set_from_error (res, error);
          g_error_free (error);
        }

      g_simple_async_result_complete (res);
      g_object_unref (res);
    }

  return FALSE;
}

static void
evd_buffered_output_stream_write_async (GOutputStream       *stream,
                                        const void          *buffer,
                                        gsize                size,
                                        int                  io_priority,
                                        GCancellable        *cancellable,
                                        GAsyncReadyCallback  callback,
                                        gpointer             user_data)
{
  EvdBufferedOutputStream *self = EVD_BUFFERED_OUTPUT_STREAM (stream);

  self->priv->async_result =
    g_simple_async_result_new (G_OBJECT (stream),
                               callback,
                               user_data,
                               evd_buffered_output_stream_write_async);

  self->priv->requested_size = size;
  self->priv->actual_size = 0;

  evd_buffered_output_stream_fill (self,
                                   buffer,
                                   size);

  if (! self->priv->frozen)
    self->priv->write_src_id =
      evd_timeout_add (g_main_context_get_thread_default (),
                       0,
                       io_priority,
                       do_write,
                       self);
}

static gssize
evd_buffered_output_stream_write_finish (GOutputStream  *stream,
                                         GAsyncResult   *result,
                                         GError        **error)
{
  EvdBufferedOutputStream *self = EVD_BUFFERED_OUTPUT_STREAM (stream);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
    return self->priv->actual_size;
  else
    return -1;
}

static gboolean
evd_buffered_output_stream_flush (GOutputStream  *stream,
                                  GCancellable   *cancellable,
                                  GError        **error)
{
  EvdBufferedOutputStream *self = EVD_BUFFERED_OUTPUT_STREAM (stream);
  GOutputStream *base_stream;
  gsize size;
  gssize actual_size;

  size = MIN (self->priv->requested_size, self->priv->buffer->len);
  if (size == 0)
    return TRUE;

  base_stream =
    g_filter_output_stream_get_base_stream (G_FILTER_OUTPUT_STREAM (stream));

  actual_size = g_output_stream_write (base_stream,
                                       self->priv->buffer->str,
                                       size,
                                       cancellable,
                                       error);

  if (actual_size > 0)
    {
      g_string_erase (self->priv->buffer, 0, actual_size);
      self->priv->actual_size += actual_size;
      self->priv->requested_size -= actual_size;
    }

  return TRUE;
}

static gboolean
evd_buffered_output_stream_close (GOutputStream  *stream,
                                  GCancellable   *cancellable,
                                  GError        **error)
{
  EvdBufferedOutputStream *self = EVD_BUFFERED_OUTPUT_STREAM (stream);

  if (self->priv->async_result != NULL)
    {
      GSimpleAsyncResult *res;

      res = self->priv->async_result;
      self->priv->async_result = NULL;

      g_simple_async_result_set_error (res,
                                       EVD_ERROR,
                                       EVD_ERROR_NOT_WRITABLE,
                                       "Stream has been closed");

      g_simple_async_result_complete (res);
      g_object_unref (res);
    }

  if (self->priv->write_src_id != 0)
    {
      g_source_remove (self->priv->write_src_id);
      self->priv->write_src_id = 0;
    }

  return TRUE;
}

/* public methods */

EvdBufferedOutputStream *
evd_buffered_output_stream_new (GOutputStream *base_stream)
{
  EvdBufferedOutputStream *self;

  g_return_val_if_fail (G_IS_OUTPUT_STREAM (base_stream), NULL);

  self = g_object_new (EVD_TYPE_BUFFERED_OUTPUT_STREAM,
                       "base-stream", base_stream,
                       NULL);

  return self;
}

gssize
evd_buffered_output_stream_write_str (EvdBufferedOutputStream  *self,
                                      const gchar              *buffer,
                                      GError                  **error)
{
  gsize size;

  g_return_val_if_fail (EVD_IS_BUFFERED_OUTPUT_STREAM (self), 0);

  if (buffer == NULL)
    return 0;

  size = strlen (buffer);

  return g_output_stream_write (G_OUTPUT_STREAM (self),
                                buffer,
                                size,
                                NULL,
                                error);
}

void
evd_buffered_output_stream_write_str_async (EvdBufferedOutputStream *self,
                                            const gchar             *buffer,
                                            int                      io_priority,
                                            GCancellable            *cancellable,
                                            GAsyncReadyCallback      callback,
                                            gpointer                 user_data)
{
  g_return_if_fail (EVD_IS_BUFFERED_OUTPUT_STREAM (self));

  g_output_stream_write_async (G_OUTPUT_STREAM (self),
                               (void *) buffer,
                               strlen (buffer),
                               io_priority,
                               cancellable,
                               callback,
                               user_data);
}

gssize
evd_buffered_output_stream_write_str_finish (EvdBufferedOutputStream  *self,
                                             GAsyncResult             *result,
                                             GError                  **error)
{
  g_return_val_if_fail (EVD_IS_BUFFERED_OUTPUT_STREAM (self), 0);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), 0);

  return g_output_stream_write_finish (G_OUTPUT_STREAM (self),
                                       result,
                                       error);
}

void
evd_buffered_output_stream_freeze (EvdBufferedOutputStream *self)
{
  g_return_if_fail (EVD_IS_BUFFERED_OUTPUT_STREAM (self));

  self->priv->frozen = TRUE;
}

void
evd_buffered_output_stream_thaw (EvdBufferedOutputStream *self,
                                 gint                     priority)
{
  g_return_if_fail (EVD_IS_BUFFERED_OUTPUT_STREAM (self));

  self->priv->frozen = FALSE;

  if (self->priv->buffer->len > 0)
    self->priv->write_src_id =
      evd_timeout_add (g_main_context_get_thread_default (),
                       0,
                       priority,
                       do_write,
                       self);
}
