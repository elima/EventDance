/*
 * evd-connection.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009/2010, Igalia S.L.
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

#ifdef HAVE_GIO_UNIX
#include <gio/gunixsocketaddress.h>
#endif

#include "evd-connection.h"

#include "evd-error.h"
#include "evd-utils.h"
#include "evd-marshal.h"

#include "evd-socket-input-stream.h"
#include "evd-socket-output-stream.h"
#include "evd-buffered-input-stream.h"
#include "evd-buffered-output-stream.h"
#include "evd-throttled-input-stream.h"
#include "evd-throttled-output-stream.h"
#include "evd-tls-input-stream.h"
#include "evd-tls-output-stream.h"

G_DEFINE_TYPE (EvdConnection, evd_connection, G_TYPE_IO_STREAM)

#define EVD_CONNECTION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                         EVD_TYPE_CONNECTION, \
                                         EvdConnectionPrivate))
#define CLOSED(conn)       (g_io_stream_is_closed (G_IO_STREAM (conn)))
#define READ_PENDING(conn) (conn->priv->buf_input_stream != NULL && \
                            g_input_stream_has_pending (G_INPUT_STREAM (conn->priv->buf_input_stream)))
#define TLS_SESSION(conn)  (evd_connection_get_tls_session (conn))

/* private data */
struct _EvdConnectionPrivate
{
  EvdSocket *socket;

  EvdSocketInputStream *socket_input_stream;
  EvdSocketOutputStream *socket_output_stream;
  EvdTlsInputStream *tls_input_stream;
  EvdTlsOutputStream *tls_output_stream;
  EvdBufferedInputStream *buf_input_stream;
  EvdBufferedOutputStream *buf_output_stream;
  EvdThrottledInputStream *throt_input_stream;
  EvdThrottledOutputStream *throt_output_stream;

  GIOCondition cond;

  gboolean delayed_close;
  gboolean close_locked;

  gint read_src_id;
  gint write_src_id;
  gint close_src_id;

  gboolean tls_handshaking;
  gboolean tls_active;
  EvdTlsSession *tls_session;
  GSimpleAsyncResult *async_result;

  gboolean connected;
  gboolean closing;

  EvdIoStreamGroup *group;

  EvdStreamThrottle *input_throttle;
  EvdStreamThrottle *output_throttle;
};

/* signals */
enum
{
  SIGNAL_CLOSE,
  SIGNAL_GROUP_CHANGED,
  SIGNAL_WRITE,
  SIGNAL_LAST
};

static guint evd_connection_signals[SIGNAL_LAST] = { 0 };

/* properties */
enum
{
  PROP_0,
  PROP_SOCKET,
  PROP_TLS_SESSION,
  PROP_TLS_ACTIVE,
  PROP_GROUP,
  PROP_INPUT_THROTTLE,
  PROP_OUTPUT_THROTTLE
};

static void           evd_connection_class_init        (EvdConnectionClass *class);
static void           evd_connection_init              (EvdConnection *self);

static void           evd_connection_finalize          (GObject *obj);
static void           evd_connection_dispose           (GObject *obj);

static void           evd_connection_set_property      (GObject      *obj,
                                                        guint         prop_id,
                                                        const GValue *value,
                                                        GParamSpec   *pspec);
static void           evd_connection_get_property      (GObject    *obj,
                                                        guint       prop_id,
                                                        GValue     *value,
                                                        GParamSpec *pspec);

static GInputStream  *evd_connection_get_input_stream  (GIOStream *stream);
static GOutputStream *evd_connection_get_output_stream (GIOStream *stream);

static gboolean       evd_connection_close_internal    (GIOStream     *stream,
                                                        GCancellable  *cancellable,
                                                        GError       **error);
static void           evd_connection_close_in_idle     (EvdConnection *self);
static gboolean       evd_connection_close_in_idle_cb  (gpointer user_data);

static void           evd_connection_setup_streams     (EvdConnection *self);
static void           evd_connection_teardown_streams  (EvdConnection *self);

static void           evd_connection_socket_on_status_changed (EvdSocket      *socket,
                                                               EvdSocketState  new_status,
                                                               EvdSocketState  old_status,
                                                               gpointer        user_data);
static void           evd_connection_socket_on_error          (EvdSocket *socket,
                                                               guint32    error_domain,
                                                               gint       error_code,
                                                               gchar     *message,
                                                               gpointer   user_data);

static void           evd_connection_delay_read               (EvdThrottledInputStream *stream,
                                                               guint                    wait,
                                                               gpointer                 user_data);
static void           evd_connection_delay_write              (EvdThrottledOutputStream *stream,
                                                               guint                     wait,
                                                               gpointer                  user_data);

static void
evd_connection_class_init (EvdConnectionClass *class)
{
  GObjectClass *obj_class;
  GIOStreamClass *io_stream_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_connection_dispose;
  obj_class->finalize = evd_connection_finalize;
  obj_class->get_property = evd_connection_get_property;
  obj_class->set_property = evd_connection_set_property;

  io_stream_class = G_IO_STREAM_CLASS (class);
  io_stream_class->get_input_stream = evd_connection_get_input_stream;
  io_stream_class->get_output_stream = evd_connection_get_output_stream;
  io_stream_class->close_fn = evd_connection_close_internal;

  evd_connection_signals[SIGNAL_CLOSE] =
    g_signal_new ("close",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdConnectionClass, close),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  evd_connection_signals[SIGNAL_GROUP_CHANGED] =
    g_signal_new ("group-changed",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdConnectionClass, group_changed),
                  NULL, NULL,
                  evd_marshal_VOID__OBJECT_OBJECT,
                  G_TYPE_NONE,
                  2, G_TYPE_OBJECT, G_TYPE_OBJECT);

  evd_connection_signals[SIGNAL_WRITE] =
    g_signal_new ("write",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdConnectionClass, write),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  g_object_class_install_property (obj_class, PROP_SOCKET,
                                   g_param_spec_object ("socket",
                                                        "The connection's socket",
                                                        "The socket this HTTP connection will use",
                                                        EVD_TYPE_SOCKET,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TLS_SESSION,
                                   g_param_spec_object ("tls",
                                                        "The SSL/TLS session",
                                                        "The underlaying SSL/TLS session object",
                                                        EVD_TYPE_TLS_SESSION,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TLS_ACTIVE,
                                   g_param_spec_boolean ("tls-active",
                                                         "Tells whether SSL/TLS is active",
                                                         "Returns TRUE if connection has SSL/TLS active, FALSE otherwise. SSL/TLS is activated by calling 'starttls' on a connection",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_GROUP,
                                   g_param_spec_object ("group",
                                                        "Connection group",
                                                        "The group this connection belongs to",
                                                        EVD_TYPE_IO_STREAM_GROUP,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_INPUT_THROTTLE,
                                   g_param_spec_object ("input-throttle",
                                                        "Input throttle object",
                                                        "The connection's input throttle object",
                                                        EVD_TYPE_STREAM_THROTTLE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_OUTPUT_THROTTLE,
                                   g_param_spec_object ("output-throttle",
                                                        "Output throttle object",
                                                        "The connection's output throttle object",
                                                        EVD_TYPE_STREAM_THROTTLE,
                                                        G_PARAM_READABLE |
                                                        G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdConnectionPrivate));
}

static void
evd_connection_init (EvdConnection *self)
{
  EvdConnectionPrivate *priv;

  priv = EVD_CONNECTION_GET_PRIVATE (self);
  self->priv = priv;

  priv->tls_handshaking = FALSE;

  priv->delayed_close = FALSE;
  priv->close_locked = FALSE;

  priv->read_src_id = 0;
  priv->write_src_id = 0;
  priv->close_src_id = 0;

  priv->async_result = NULL;
  priv->tls_session = NULL;
  priv->tls_active = FALSE;

  priv->connected = FALSE;
  priv->closing = FALSE;

  priv->group = NULL;

  priv->input_throttle = evd_stream_throttle_new ();
  priv->output_throttle = evd_stream_throttle_new ();

  priv->cond = 0;
}

static void
evd_connection_dispose (GObject *obj)
{
  EvdConnection *self = EVD_CONNECTION (obj);

  if (self->priv->group != NULL)
    {
      g_object_unref (self->priv->group);
      self->priv->group = NULL;
    }

  G_OBJECT_CLASS (evd_connection_parent_class)->dispose (obj);
}

static void
evd_connection_finalize (GObject *obj)
{
  EvdConnection *self = EVD_CONNECTION (obj);

  evd_connection_teardown_streams (self);

  g_object_unref (self->priv->input_throttle);
  g_object_unref (self->priv->output_throttle);

  g_object_unref (self->priv->socket);

  if (self->priv->tls_session != NULL)
    g_object_unref (self->priv->tls_session);

  if (self->priv->async_result != NULL)
    g_object_unref (self->priv->async_result);

  G_OBJECT_CLASS (evd_connection_parent_class)->finalize (obj);
}

static void
evd_connection_set_property (GObject      *obj,
                             guint         prop_id,
                             const GValue *value,
                             GParamSpec   *pspec)
{
  EvdConnection *self;

  self = EVD_CONNECTION (obj);

  switch (prop_id)
    {
    case PROP_SOCKET:
      evd_connection_set_socket (self, g_value_get_object (value));
      break;

    case PROP_GROUP:
      evd_connection_set_group (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_connection_get_property (GObject    *obj,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  EvdConnection *self;

  self = EVD_CONNECTION (obj);

  switch (prop_id)
    {
    case PROP_SOCKET:
      g_value_set_object (value, evd_connection_get_socket (self));
      break;

    case PROP_TLS_SESSION:
      g_value_set_object (value, evd_connection_get_tls_session (self));
      break;

    case PROP_TLS_ACTIVE:
      g_value_set_boolean (value, evd_connection_get_tls_active (self));
      break;

    case PROP_GROUP:
      g_value_set_object (value, self->priv->group);
      break;

    case PROP_INPUT_THROTTLE:
      g_value_set_object (value, self->priv->input_throttle);
      break;

    case PROP_OUTPUT_THROTTLE:
      g_value_set_object (value, self->priv->output_throttle);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static GInputStream *
evd_connection_get_input_stream (GIOStream *stream)
{
  EvdConnection *self = EVD_CONNECTION (stream);

  return G_INPUT_STREAM (self->priv->buf_input_stream);
}

static GOutputStream *
evd_connection_get_output_stream (GIOStream *stream)
{
  EvdConnection *self = EVD_CONNECTION (stream);

  return G_OUTPUT_STREAM (self->priv->buf_output_stream);
}

static gboolean
evd_connection_close_internal (GIOStream     *stream,
                               GCancellable  *cancellable,
                               GError       **error)
{
  EvdConnection *self = EVD_CONNECTION (stream);
  gboolean result = TRUE;
  GError *_error = NULL;

  if (self->priv->closing)
    return TRUE;

  self->priv->closing = TRUE;

  self->priv->connected = FALSE;

  if (self->priv->close_src_id != 0)
    {
      g_source_remove (self->priv->close_src_id);
      self->priv->close_src_id = 0;
      g_object_unref (self);
    }

  if (self->priv->read_src_id != 0)
    {
      g_source_remove (self->priv->read_src_id);
      self->priv->read_src_id = 0;
      g_object_unref (self);
    }

  if (self->priv->write_src_id != 0)
    {
      g_source_remove (self->priv->write_src_id);
      self->priv->write_src_id = 0;
      g_object_unref (self);
    }

  if (self->priv->async_result != NULL)
    {
      GSimpleAsyncResult *res;

      res = self->priv->async_result;
      self->priv->async_result = NULL;

      if (self->priv->tls_handshaking)
        g_simple_async_result_set_error (res,
                                         G_IO_ERROR,
                                         G_IO_ERROR_CLOSED,
                                         "Connection closed during TLS handshake");
      g_simple_async_result_complete (res);
      g_object_unref (res);
    }

  self->priv->tls_handshaking = FALSE;
  self->priv->tls_active = FALSE;

  if (self->priv->tls_output_stream != NULL)
    {
      g_output_stream_clear_pending (G_OUTPUT_STREAM (self->priv->tls_output_stream));
      if (! g_output_stream_close (G_OUTPUT_STREAM (self->priv->tls_output_stream),
                                   NULL,
                                   &_error))
        {
          result = FALSE;
        }
    }

  g_signal_handlers_disconnect_by_func (self->priv->throt_input_stream,
                                        evd_connection_delay_read,
                                        self);

  g_signal_handlers_disconnect_by_func (self->priv->throt_output_stream,
                                        evd_connection_delay_write,
                                        self);

  g_output_stream_clear_pending (G_OUTPUT_STREAM (self->priv->buf_output_stream));
  if (! g_output_stream_close (G_OUTPUT_STREAM (self->priv->buf_output_stream),
                               NULL,
                               _error == NULL ? &_error : NULL))
    {
      result = FALSE;
    }

  g_input_stream_clear_pending (G_INPUT_STREAM (self->priv->buf_input_stream));
  if (! g_input_stream_close (G_INPUT_STREAM (self->priv->buf_input_stream),
                              NULL,
                              (_error == NULL) ? &_error : NULL))
    {
      result = FALSE;
    }

  g_signal_handlers_disconnect_by_func (self->priv->socket,
                                        evd_connection_socket_on_status_changed,
                                        self);
  g_signal_handlers_disconnect_by_func (self->priv->socket,
                                        evd_connection_socket_on_error,
                                        self);
  evd_socket_set_notify_condition_callback (self->priv->socket, NULL, NULL);

  evd_socket_close (self->priv->socket, (_error == NULL) ? &_error : NULL);

  if (_error != NULL)
    g_propagate_error (error, _error);

  g_signal_emit (self, evd_connection_signals[SIGNAL_CLOSE], 0, NULL);

  self->priv->closing = FALSE;

  return result;
}

static void
evd_connection_socket_on_status_changed (EvdSocket      *socket,
                                         EvdSocketState  new_status,
                                         EvdSocketState  old_status,
                                         gpointer        user_data)
{
  EvdConnection *self = EVD_CONNECTION (user_data);

  if (new_status == EVD_SOCKET_STATE_CONNECTED)
    {
      self->priv->connected = TRUE;

      evd_buffered_output_stream_set_auto_flush (self->priv->buf_output_stream,
                                                 TRUE);
    }
  else if (new_status == EVD_SOCKET_STATE_CLOSED)
    {
      if (self->priv->connected)
        evd_connection_close_in_idle (self);
    }
}

static void
evd_connection_socket_on_error (EvdSocket *socket,
                                guint32    error_domain,
                                gint       error_code,
                                gchar     *message,
                                gpointer   user_data)
{
  /* @TODO: report these errors through an own 'error' signal */
}

static gboolean
evd_connection_close_in_idle_cb (gpointer user_data)
{
  EvdConnection *self = EVD_CONNECTION (user_data);
  GError *error = NULL;

  self->priv->close_src_id = 0;

  g_io_stream_clear_pending (G_IO_STREAM (self));
   if (! g_io_stream_close (G_IO_STREAM (self), NULL, &error))
    {
      /* @TODO: handle error */
      g_debug ("error closing connection: %s", error->message);

      g_error_free (error);
    }

  g_object_unref (self);

  return FALSE;
}

static void
evd_connection_close_in_idle (EvdConnection *self)
{
  self->priv->connected = FALSE;

  if (self->priv->close_src_id == 0)
    {
      g_object_ref (self);

      self->priv->close_src_id =
        evd_timeout_add (NULL,
                         0,
                         evd_socket_get_priority (self->priv->socket),
                         evd_connection_close_in_idle_cb,
                         self);
    }
}

static void
evd_connection_tls_handshake (EvdConnection *self)
{
  GError *error = NULL;
  GIOCondition direction;
  gint result;
  GSimpleAsyncResult *res;

  direction = evd_tls_session_get_direction (TLS_SESSION (self));
  if ( (direction == G_IO_IN && self->priv->read_src_id != 0) ||
       (direction == G_IO_OUT && self->priv->write_src_id != 0) )
    return;

  result = evd_tls_session_handshake (TLS_SESSION (self), &error);

  if (result == 0)
    return;

  self->priv->tls_handshaking = FALSE;

  res = self->priv->async_result;
  self->priv->async_result = NULL;

  if (result < 0)
    {
      g_simple_async_result_set_from_error (res, error);
      g_error_free (error);
    }

  g_simple_async_result_complete_in_idle (res);
  g_object_unref (res);

  if (result > 0)
    {
      evd_buffered_output_stream_set_auto_flush (self->priv->buf_output_stream,
                                                 TRUE);

      evd_buffered_input_stream_thaw (self->priv->buf_input_stream,
                                      evd_socket_get_priority (self->priv->socket));
    }
  else
    {
      g_object_ref (self);
      evd_connection_close_in_idle_cb (self);
    }
}

static void
evd_connection_manage_read_condition (EvdConnection *self)
{
  if (self->priv->tls_handshaking)
    evd_connection_tls_handshake (self);
  else
    evd_buffered_input_stream_thaw (self->priv->buf_input_stream,
                                    evd_socket_get_priority (self->priv->socket));
}

static void
evd_connection_manage_write_condition (EvdConnection *self)
{
  if (self->priv->tls_handshaking)
    evd_connection_tls_handshake (self);
  else
    evd_buffered_output_stream_notify_write (self->priv->buf_output_stream);

  if (! CLOSED (self) && self->priv->tls_output_stream != NULL)
    evd_buffered_output_stream_notify_write (EVD_BUFFERED_OUTPUT_STREAM (self->priv->tls_output_stream));

  if (! self->priv->tls_handshaking &&
      evd_connection_get_max_writable (self) > 0)
    {
      g_signal_emit (self, evd_connection_signals[SIGNAL_WRITE], 0, NULL);
    }
}

static gboolean
evd_connection_read_wait_timeout (gpointer user_data)
{
  EvdConnection *self = EVD_CONNECTION (user_data);

  if (! CLOSED (self))
    {
      self->priv->read_src_id = 0;

      evd_connection_manage_read_condition (self);

      if (self->priv->delayed_close &&
          ! READ_PENDING (self))
        {
          g_object_ref (self);
          evd_connection_close_in_idle_cb (self);
        }
    }

  g_object_unref (self);

  return FALSE;
}

static gboolean
evd_connection_write_wait_timeout (gpointer user_data)
{
  EvdConnection *self = EVD_CONNECTION (user_data);

  if (! CLOSED (self))
    {
      self->priv->write_src_id = 0;

      evd_connection_manage_write_condition (self);
    }

  g_object_unref (self);

  return FALSE;
}

static void
evd_connection_socket_on_condition (EvdSocket    *socket,
                                    GIOCondition  condition,
                                    gpointer      user_data)
{
  EvdConnection *self = EVD_CONNECTION (user_data);

  if (CLOSED (self))
    return;

  self->priv->cond = condition;

  if ( (condition & G_IO_IN) > 0 && self->priv->read_src_id == 0)
    evd_connection_manage_read_condition (self);

  if (condition & G_IO_HUP)
    {
      if (self->priv->close_locked
          || self->priv->read_src_id != 0
          || READ_PENDING (self))
        {
          self->priv->delayed_close = TRUE;
        }
      else
        {
          evd_connection_close_in_idle (self);
        }
    }
  else if ( (condition & G_IO_OUT) > 0 && self->priv->write_src_id == 0)
    {
      evd_connection_manage_write_condition (self);
    }
}

static void
evd_connection_socket_input_stream_drained (GInputStream *stream,
                                            gpointer      user_data)
{
  EvdConnection *self = EVD_CONNECTION (user_data);

  if (CLOSED (self))
    return;

  if (self->priv->delayed_close && ! self->priv->close_locked)
    {
      evd_connection_close_in_idle (self);
    }
  else
    {
      GError *error = NULL;

      self->priv->cond &= ~G_IO_IN;

      if (! evd_socket_watch_condition (self->priv->socket,
                                        ~self->priv->cond,
                                        &error))
        {
          /* @TODO: handle error */
          g_warning ("Undandled error: watch socket condition error: %s", error->message);
          g_error_free (error);
        }
    }
}

static void
evd_connection_socket_output_stream_filled (GOutputStream *stream,
                                            gpointer       user_data)
{
  EvdConnection *self = EVD_CONNECTION (user_data);
  GError *error = NULL;

  if (CLOSED (self))
    return;

  self->priv->cond &= ~G_IO_OUT;
  if (! evd_socket_watch_condition (self->priv->socket,
                                    ~self->priv->cond,
                                    &error))
    {
      // @TODO: handle error
      g_warning ("Undandled error: watch socket condition error: %s", error->message);
      g_error_free (error);
    }
}

static void
evd_connection_delay_read (EvdThrottledInputStream *stream,
                           guint                    wait,
                           gpointer                 user_data)
{
  EvdConnection *self = EVD_CONNECTION (user_data);

  if (self->priv->read_src_id == 0)
    {
      g_object_ref (self);

      self->priv->read_src_id =
        evd_timeout_add (NULL,
                         wait,
                         evd_socket_get_priority (self->priv->socket),
                         evd_connection_read_wait_timeout,
                         self);
    }
}

static void
evd_connection_delay_write (EvdThrottledOutputStream *stream,
                            guint                     wait,
                            gpointer                  user_data)
{
  EvdConnection *self = EVD_CONNECTION (user_data);

  if (self->priv->write_src_id == 0)
    {
      g_object_ref (self);

      self->priv->write_src_id =
        evd_timeout_add (NULL,
                         wait,
                         evd_socket_get_priority (self->priv->socket),
                         evd_connection_write_wait_timeout,
                         self);
    }
}

static void
evd_connection_setup_streams (EvdConnection *self)
{
  /* socket input stream */
  self->priv->socket_input_stream =
    evd_socket_input_stream_new (self->priv->socket);

  g_signal_connect (self->priv->socket_input_stream,
                    "drained",
                    G_CALLBACK (evd_connection_socket_input_stream_drained),
                    self);

  /* socket output stream */
  self->priv->socket_output_stream =
    evd_socket_output_stream_new (self->priv->socket);

  g_signal_connect (self->priv->socket_output_stream,
                    "filled",
                    G_CALLBACK (evd_connection_socket_output_stream_filled),
                    self);

  /* throttled input stream */
  self->priv->throt_input_stream =
    evd_throttled_input_stream_new (
                              G_INPUT_STREAM (self->priv->socket_input_stream));

  evd_throttled_input_stream_add_throttle (self->priv->throt_input_stream,
                                           self->priv->input_throttle);

  g_signal_connect (self->priv->throt_input_stream,
                    "delay-read",
                    G_CALLBACK (evd_connection_delay_read),
                    self);

  /* throttled output stream */
  self->priv->throt_output_stream =
    evd_throttled_output_stream_new (
                            G_OUTPUT_STREAM (self->priv->socket_output_stream));

  evd_throttled_output_stream_add_throttle (self->priv->throt_output_stream,
                                            self->priv->output_throttle);

  g_signal_connect (self->priv->throt_output_stream,
                    "delay-write",
                    G_CALLBACK (evd_connection_delay_write),
                    self);

  if (self->priv->group != NULL)
    {
      EvdStreamThrottle *input_throttle;
      EvdStreamThrottle *output_throttle;

      g_object_get (self->priv->group,
                    "input-throttle", &input_throttle,
                    "output-throttle", &output_throttle,
                    NULL);

      evd_throttled_input_stream_add_throttle (self->priv->throt_input_stream,
                                               input_throttle);

      evd_throttled_output_stream_add_throttle (self->priv->throt_output_stream,
                                                output_throttle);

      g_object_unref (input_throttle);
      g_object_unref (output_throttle);
    }

  /* buffered input stream */
  self->priv->buf_input_stream =
    evd_buffered_input_stream_new (G_INPUT_STREAM (self->priv->throt_input_stream));

  /* buffered output stream */
  self->priv->buf_output_stream =
    evd_buffered_output_stream_new (G_OUTPUT_STREAM (self->priv->throt_output_stream));

  if (evd_socket_get_status (self->priv->socket) != EVD_SOCKET_STATE_CONNECTED)
    {
      self->priv->connected = FALSE;
      evd_buffered_input_stream_freeze (self->priv->buf_input_stream);
    }
  else
    {
      self->priv->connected = TRUE;
    }

  evd_buffered_output_stream_set_auto_flush (self->priv->buf_output_stream,
                                             self->priv->connected);
}

static void
evd_connection_teardown_streams (EvdConnection *self)
{
  g_object_unref (self->priv->buf_input_stream);
  g_object_unref (self->priv->buf_output_stream);

  if (self->priv->tls_input_stream != NULL)
    g_object_unref (self->priv->tls_input_stream);

  if (self->priv->tls_output_stream != NULL)
    g_object_unref (self->priv->tls_output_stream);

  g_object_unref (self->priv->throt_input_stream);
  g_object_unref (self->priv->throt_output_stream);

  g_object_unref (self->priv->socket_input_stream);
  g_object_unref (self->priv->socket_output_stream);
}

static void
evd_connection_shutdown_on_flush (GObject      *obj,
                                  GAsyncResult *res,
                                  gpointer      user_data)
{
  EvdConnection *self = EVD_CONNECTION (user_data);

  g_output_stream_flush_finish (G_OUTPUT_STREAM (obj), res, NULL);

  if (self->priv->tls_active)
    evd_tls_session_close (self->priv->tls_session, NULL);

  evd_socket_shutdown (evd_connection_get_socket (self),
                       TRUE,
                       TRUE,
                       NULL);

  g_object_unref (self);
}

/* public methods */

EvdConnection *
evd_connection_new (EvdSocket *socket)
{
  EvdConnection *self;

  g_return_val_if_fail (EVD_IS_SOCKET (socket), NULL);

  self = g_object_new (EVD_TYPE_CONNECTION,
                       "socket", socket,
                       NULL);

  return self;
}

void
evd_connection_set_socket (EvdConnection *self,
                           EvdSocket     *socket)
{
  g_return_if_fail (EVD_IS_CONNECTION (self));
  g_return_if_fail (EVD_IS_SOCKET (socket));

  if (self->priv->socket != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->priv->socket,
                                            evd_connection_socket_on_status_changed,
                                            self);
      g_signal_handlers_disconnect_by_func (self->priv->socket,
                                            evd_connection_socket_on_error,
                                            self);
      evd_socket_set_notify_condition_callback (self->priv->socket, NULL, NULL);
      g_object_unref (self->priv->socket);
    }

  self->priv->socket = socket;
  g_object_ref (self->priv->socket);
  g_signal_connect (self->priv->socket,
                    "state-changed",
                    G_CALLBACK (evd_connection_socket_on_status_changed),
                    self);
  g_signal_connect (self->priv->socket,
                    "error",
                    G_CALLBACK (evd_connection_socket_on_error),
                    self);
  evd_socket_set_notify_condition_callback (self->priv->socket,
                                            evd_connection_socket_on_condition,
                                            self);

  self->priv->tls_handshaking = FALSE;
  self->priv->tls_active = FALSE;

  if (self->priv->socket_input_stream == NULL)
    {
        /* create streams for the first time */
      evd_connection_setup_streams (self);
    }
  else if (CLOSED (self))
    {
      /* this is nasty but there is no way to reset the GIO streams after
         they are closed, so we have to re-create the entire pipeline. */
      evd_connection_teardown_streams (self);
      evd_connection_setup_streams (self);

      /* reset 'closed' state of the GIOStream */
      g_object_set (self, "closed", FALSE, NULL);
    }
  else
    {
      /* update new socket in socket input streams */
      evd_socket_input_stream_set_socket (self->priv->socket_input_stream,
                                          self->priv->socket);

      evd_socket_output_stream_set_socket (self->priv->socket_output_stream,
                                           self->priv->socket);
    }

  evd_connection_socket_on_condition (self->priv->socket,
                                  evd_socket_get_condition (self->priv->socket),
                                  self);
}

/**
 * evd_connection_get_socket:
 *
 * Returns: (transfer none): The #EvdSocket object
 **/
EvdSocket *
evd_connection_get_socket (EvdConnection *self)
{
  g_return_val_if_fail (EVD_IS_CONNECTION (self), NULL);

  return self->priv->socket;
}

/**
 * evd_connection_get_tls_session:
 *
 * Returns: (transfer none): The #EvdTlsSession object
 **/
EvdTlsSession *
evd_connection_get_tls_session (EvdConnection *self)
{
  g_return_val_if_fail (EVD_IS_CONNECTION (self), NULL);

  if (self->priv->tls_session == NULL)
    self->priv->tls_session = evd_tls_session_new ();

  return self->priv->tls_session;
}

void
evd_connection_starttls (EvdConnection       *self,
                         EvdTlsMode           mode,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
  EvdTlsSession *session;

  g_return_if_fail (EVD_IS_CONNECTION (self));
  g_return_if_fail (mode == EVD_TLS_MODE_CLIENT || mode == EVD_TLS_MODE_SERVER);

  /* @TODO: use cancellable object for something */

  self->priv->async_result =
    g_simple_async_result_new (G_OBJECT (self),
                               callback,
                               user_data,
                               evd_connection_starttls);

  if (self->priv->tls_active)
    {
      g_simple_async_result_set_error (self->priv->async_result,
                                       G_IO_ERROR,
                                       G_IO_ERROR_BUSY,
                                       "SSL/TLS was already started");
      g_simple_async_result_complete_in_idle (self->priv->async_result);
      g_object_unref (self->priv->async_result);
      self->priv->async_result = NULL;

      return;
    }

  self->priv->tls_active = TRUE;

  session = TLS_SESSION (self);

  g_object_set (session, "mode", mode, NULL);

  self->priv->tls_input_stream =
    evd_tls_input_stream_new (session,
                              G_INPUT_STREAM (self->priv->throt_input_stream));

  g_filter_input_stream_set_close_base_stream (
    G_FILTER_INPUT_STREAM (self->priv->buf_input_stream), FALSE);
  g_object_unref (self->priv->buf_input_stream);

  self->priv->buf_input_stream =
    evd_buffered_input_stream_new (
      G_INPUT_STREAM (self->priv->tls_input_stream));

  self->priv->tls_output_stream =
    evd_tls_output_stream_new (session,
                               G_OUTPUT_STREAM (self->priv->throt_output_stream));

  g_filter_output_stream_set_close_base_stream (
    G_FILTER_OUTPUT_STREAM (self->priv->buf_output_stream), FALSE);
  g_object_unref (self->priv->buf_output_stream);

  self->priv->buf_output_stream =
    evd_buffered_output_stream_new (
      G_OUTPUT_STREAM (self->priv->tls_output_stream));

  evd_buffered_input_stream_freeze (self->priv->buf_input_stream);
  evd_buffered_output_stream_set_auto_flush (self->priv->buf_output_stream,
                                             FALSE);

  self->priv->tls_handshaking = TRUE;

  if (mode == EVD_TLS_MODE_CLIENT && self->priv->cond & G_IO_OUT)
    evd_connection_tls_handshake (self);
}

gboolean
evd_connection_starttls_finish (EvdConnection  *self,
                                GAsyncResult   *result,
                                GError        **error)
{
  gboolean res;

  g_return_val_if_fail (EVD_IS_CONNECTION (self), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                 G_OBJECT (self),
                                                 evd_connection_starttls),
                        FALSE);

  res =
    ! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                             error);

  return res;
}

gboolean
evd_connection_get_tls_active (EvdConnection *self)
{
  g_return_val_if_fail (EVD_IS_CONNECTION (self), FALSE);

  return self->priv->tls_active;
}

gsize
evd_connection_get_max_readable (EvdConnection *self)
{
  g_return_val_if_fail (EVD_IS_CONNECTION (self), 0);

  if ((self->priv->cond & G_IO_IN) == 0 ||
      self->priv->throt_input_stream == NULL ||
      g_io_stream_is_closed (G_IO_STREAM (self)))
    {
      return 0;
    }
  else
    {
      return
        evd_throttled_input_stream_get_max_readable (self->priv->throt_input_stream,
                                                     NULL);
    }
}

gsize
evd_connection_get_max_writable (EvdConnection *self)
{
  g_return_val_if_fail (EVD_IS_CONNECTION (self), 0);

  if ((self->priv->cond & G_IO_OUT) == 0 ||
      self->priv->throt_output_stream == NULL ||
      g_io_stream_is_closed (G_IO_STREAM (self)))
    {
      return 0;
    }
  else
    {
      return
        evd_throttled_output_stream_get_max_writable (self->priv->throt_output_stream,
                                                      NULL);
    }
}

gboolean
evd_connection_is_connected (EvdConnection *self)
{
  g_return_val_if_fail (EVD_IS_CONNECTION (self), FALSE);

  return self->priv->connected;
}

gint
evd_connection_get_priority (EvdConnection *self)
{
  g_return_val_if_fail (EVD_IS_CONNECTION (self), 0);

  return evd_socket_get_priority (self->priv->socket);
}

gboolean
evd_connection_set_group (EvdConnection    *self,
                          EvdIoStreamGroup *group)
{
  EvdIoStreamGroup *old_group = NULL;
  EvdStreamThrottle *input_throttle;
  EvdStreamThrottle *output_throttle;


  g_return_val_if_fail (EVD_IS_CONNECTION (self), FALSE);
  g_return_val_if_fail (group == NULL || EVD_IS_IO_STREAM_GROUP (group),
                        FALSE);

  if (group == self->priv->group)
    return FALSE;

  if (self->priv->group != NULL)
    {
      old_group = self->priv->group;
      self->priv->group = NULL;

      g_object_get (old_group,
                    "input-throttle", &input_throttle,
                    "output-throttle", &output_throttle,
                    NULL);

      evd_throttled_input_stream_remove_throttle (self->priv->throt_input_stream,
                                                  input_throttle);
      evd_throttled_output_stream_remove_throttle (self->priv->throt_output_stream,
                                                   output_throttle);

      g_object_unref (input_throttle);
      g_object_unref (output_throttle);
    }

  self->priv->group = group;

  if (group != NULL)
    {
      g_object_ref (group);

      g_object_get (group,
                    "input-throttle", &input_throttle,
                    "output-throttle", &output_throttle,
                    NULL);

      evd_throttled_input_stream_add_throttle (self->priv->throt_input_stream,
                                               input_throttle);
      evd_throttled_output_stream_add_throttle (self->priv->throt_output_stream,
                                                output_throttle);

      g_object_unref (input_throttle);
      g_object_unref (output_throttle);
    }

  g_signal_emit (self,
                 evd_connection_signals[SIGNAL_GROUP_CHANGED],
                 0,
                 group,
                 old_group,
                 NULL);

  if (old_group != NULL)
    g_object_unref (old_group);

  return TRUE;
}

/**
 * evd_connection_get_group:
 *
 * Returns: (transfer none):
 **/
EvdIoStreamGroup *
evd_connection_get_group (EvdConnection *self)
{
  g_return_val_if_fail (EVD_IS_CONNECTION (self), NULL);

  return self->priv->group;
}

void
evd_connection_lock_close (EvdConnection *self)
{
  g_return_if_fail (EVD_IS_CONNECTION (self));

  self->priv->close_locked = TRUE;
}

void
evd_connection_unlock_close (EvdConnection *self)
{
  g_return_if_fail (EVD_IS_CONNECTION (self));

  self->priv->close_locked = FALSE;

  if (self->priv->delayed_close)
    evd_connection_close_in_idle (self);
}

void
evd_connection_flush_and_shutdown (EvdConnection  *self,
                                   GCancellable   *cancellable)
{
  GOutputStream *stream;

  g_return_if_fail (EVD_IS_CONNECTION (self));

  stream = g_io_stream_get_output_stream (G_IO_STREAM (self));

  g_object_ref (self);
  g_output_stream_flush_async (stream,
                            evd_connection_get_priority (self),
                            cancellable,
                            evd_connection_shutdown_on_flush,
                            self);
}

EvdStreamThrottle *
evd_connection_get_input_throttle (EvdConnection *self)
{
  g_return_val_if_fail (EVD_IS_CONNECTION (self), NULL);

  return self->priv->input_throttle;
}

EvdStreamThrottle *
evd_connection_get_output_throttle (EvdConnection *self)
{
  g_return_val_if_fail (EVD_IS_CONNECTION (self), NULL);

  return self->priv->output_throttle;
}

gchar *
evd_connection_get_remote_address_as_string (EvdConnection  *self,
                                             GError        **error)
{
  GSocket *socket;
  GSocketAddress *sock_addr;
  gchar *addr_str = NULL;
  GSocketFamily family;

  g_return_val_if_fail (EVD_IS_CONNECTION (self), NULL);

  socket = evd_socket_get_socket (self->priv->socket);
  sock_addr = g_socket_get_remote_address (socket, error);
  if (sock_addr == NULL)
    return NULL;

  family = g_socket_address_get_family (sock_addr);
  if (family == G_SOCKET_FAMILY_IPV4 || family == G_SOCKET_FAMILY_IPV6)
    {
      /* inet socket address */
      GInetAddress *inet_addr;

      inet_addr =
        g_inet_socket_address_get_address (G_INET_SOCKET_ADDRESS (sock_addr));
      addr_str = g_inet_address_to_string (inet_addr);
    }
  else
    {
      /* unix socket address */
      GUnixSocketAddress *unix_addr = G_UNIX_SOCKET_ADDRESS (sock_addr);

      addr_str = g_strdup (g_unix_socket_address_get_path (unix_addr));
    }

  g_object_unref (sock_addr);

  return addr_str;
}
