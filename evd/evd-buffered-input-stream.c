/*
 * evd-buffered-input-stream.c
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
#include "evd-buffered-input-stream.h"

#define DOMAIN_QUARK_STRING "org.eventdance.lib.buffered-input-stream"

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
};

/* properties */
enum
{
  PROP_0,
  PROP_BUFFER_SIZE
};

static GQuark evd_buffered_input_stream_err_domain;

static void     evd_buffered_input_stream_class_init         (EvdBufferedInputStreamClass *class);
static void     evd_buffered_input_stream_init               (EvdBufferedInputStream *self);
static void     evd_buffered_input_stream_finalize           (GObject *obj);

static void     evd_buffered_input_stream_set_property       (GObject      *obj,
                                                              guint         prop_id,
                                                              const GValue *value,
                                                              GParamSpec   *pspec);
static void     evd_buffered_input_stream_get_property       (GObject    *obj,
                                                              guint       prop_id,
                                                              GValue     *value,
                                                              GParamSpec *pspec);

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

  obj_class->get_property = evd_buffered_input_stream_get_property;
  obj_class->set_property = evd_buffered_input_stream_set_property;
  obj_class->finalize = evd_buffered_input_stream_finalize;

  input_stream_class = G_INPUT_STREAM_CLASS (class);
  input_stream_class->read_fn = evd_buffered_input_stream_read;
  input_stream_class->read_async = evd_buffered_input_stream_read_async;
  input_stream_class->read_finish = evd_buffered_input_stream_read_finish;
  input_stream_class->close_fn = evd_buffered_input_stream_close;

  g_type_class_add_private (obj_class, sizeof (EvdBufferedInputStreamPrivate));

  evd_buffered_input_stream_err_domain =
    g_quark_from_static_string (DOMAIN_QUARK_STRING);
}

static void
evd_buffered_input_stream_init (EvdBufferedInputStream *self)
{
  EvdBufferedInputStreamPrivate *priv;

  priv = EVD_BUFFERED_INPUT_STREAM_GET_PRIVATE (self);
  self->priv = priv;

  priv->buffer = g_string_new ("");

  priv->async_buffer = NULL;
}

static void
evd_buffered_input_stream_finalize (GObject *obj)
{
  EvdBufferedInputStream *self = EVD_BUFFERED_INPUT_STREAM (obj);

  g_string_free (self->priv->buffer, TRUE);

  if (self->priv->async_result != NULL)
    g_object_unref (self->priv->async_result);

  G_OBJECT_CLASS (evd_buffered_input_stream_parent_class)->finalize (obj);
}

static void
evd_buffered_input_stream_set_property (GObject      *obj,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  EvdBufferedInputStream *self;

  self = EVD_BUFFERED_INPUT_STREAM (obj);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_buffered_input_stream_get_property (GObject    *obj,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  EvdBufferedInputStream *self;

  self = EVD_BUFFERED_INPUT_STREAM (obj);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
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
  GInputStream *base_stream;
  gssize read_from_buf = 0;
  gssize read_from_stream = 0;

  if (self->priv->buffer->len > 0)
    {
      read_from_buf = MIN (self->priv->buffer->len, size);
      g_memmove (buffer, self->priv->buffer->str, read_from_buf);
      size -= read_from_buf;

      buf = (gchar *) ( ((guintptr) buffer) + read_from_buf);
    }
  else
    {
      buf = buffer;
    }

  base_stream =
    g_filter_input_stream_get_base_stream (G_FILTER_INPUT_STREAM (self));

  read_from_stream = g_input_stream_read (base_stream,
                                          buf,
                                          size,
                                          cancellable,
                                          error);

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
    return 0;

  size =
    G_INPUT_STREAM_GET_CLASS (self)->read_fn (G_INPUT_STREAM (self),
                                              self->priv->async_buffer,
                                              self->priv->requested_size,
                                              NULL,
                                              &error);
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

  self->priv->read_src_id =
    evd_timeout_add (g_main_context_get_thread_default (),
                     0,
                     do_read,
                     self);
}

static gssize
evd_buffered_input_stream_read_finish (GInputStream  *stream,
                                       GAsyncResult  *result,
                                       GError       **error)
{
  EvdBufferedInputStream *self = EVD_BUFFERED_INPUT_STREAM (stream);

  g_input_stream_clear_pending (stream);

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

  if (self->priv->async_result != NULL)
    {
      GSimpleAsyncResult *res;

      res = self->priv->async_result;
      self->priv->async_result = NULL;

      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);
    }

  if (self->priv->read_src_id != 0)
    {
      g_source_remove (self->priv->read_src_id);
      self->priv->read_src_id = 0;
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
        *error = g_error_new (evd_buffered_input_stream_err_domain,
                              EVD_ERROR_BUFFER_FULL,
                              "Buffer is full");

      return -1;
    }
  else
    {
      g_string_append_len (self->priv->buffer, buffer, size);

      return size;
    }
}

void
evd_buffered_input_stream_read_str_async (EvdBufferedInputStream *self,
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

gchar *
evd_buffered_input_stream_read_str_finish (EvdBufferedInputStream  *self,
                                           GAsyncResult            *result,
                                           GError                 **error)
{
  gchar *buf = NULL;
  gssize size;

  g_return_val_if_fail (EVD_IS_BUFFERED_INPUT_STREAM (self), NULL);
  g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

  if ( (size = g_input_stream_read_finish (G_INPUT_STREAM (self),
                                           result,
                                           error)) < 0)
    {
      g_free (self->priv->async_buffer);
    }
  else
    {
      buf = self->priv->async_buffer;
    }

  self->priv->async_buffer = NULL;

  return buf;
}

void
evd_buffered_input_stream_notify_read (EvdBufferedInputStream *self)
{
  g_return_if_fail (EVD_IS_BUFFERED_INPUT_STREAM (self));

  if (self->priv->async_result != NULL && self->priv->read_src_id == 0)
    self->priv->read_src_id =
      evd_timeout_add (g_main_context_get_thread_default (),
                       0,
                       do_read,
                       self);
}
