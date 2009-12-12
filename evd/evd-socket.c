/*
 * evd-socket.c
 *
 * EventDance project - An event distribution framework (http://eventdance.org)
 *
 * Copyright (C) 2009, Igalia S.L.
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
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>

#include "evd-marshal.h"
#include "evd-socket-manager.h"
#include "evd-stream-protected.h"

#include "evd-socket.h"
#include "evd-socket-protected.h"

#define DEFAULT_CONNECT_TIMEOUT 0 /* no timeout */
#define DOMAIN_QUARK_STRING     "org.eventdance.glib.socket"

#define MAX_BLOCK_SIZE          0xFFFF
#define MAX_READ_BUFFER_SIZE    0xFFFF
#define MAX_WRITE_BUFFER_SIZE   0xFFFF

G_DEFINE_TYPE (EvdSocket, evd_socket, EVD_TYPE_STREAM)

#define EVD_SOCKET_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                     EVD_TYPE_SOCKET, \
                                     EvdSocketPrivate))

/* private data */
struct _EvdSocketPrivate
{
  GSocket         *socket;
  GSocketFamily    family;
  GSocketType      type;
  GSocketProtocol  protocol;

  EvdSocketState   status;
  GMainContext    *context;

  guint         connect_timeout;
  guint         connect_timeout_src_id;
  GCancellable *connect_cancellable;

  GQueue *event_queue_cache;

  EvdSocketGroup *group;

  gboolean auto_write;

  gint read_src_id;
  gint write_src_id;

  GString *read_buffer;
  GString *write_buffer;

  GIOCondition cond;

  gint actual_priority;
  gint priority;
};

/* signals */
enum
{
  SIGNAL_ERROR,
  SIGNAL_STATE_CHANGED,
  SIGNAL_CLOSE,
  SIGNAL_NEW_CONNECTION,
  SIGNAL_LAST
};

static guint evd_socket_signals[SIGNAL_LAST] = { 0 };

/* properties */
enum
{
  PROP_0,
  PROP_SOCKET,
  PROP_FAMILY,
  PROP_TYPE,
  PROP_PROTOCOL,
  PROP_READ_CLOSURE,
  PROP_CONNECT_TIMEOUT,
  PROP_AUTO_WRITE,
  PROP_GROUP,
  PROP_PRIORITY
};

static void     evd_socket_class_init         (EvdSocketClass *class);
static void     evd_socket_init               (EvdSocket *self);

static void     evd_socket_finalize           (GObject *obj);
static void     evd_socket_dispose            (GObject *obj);

static void     evd_socket_set_property       (GObject      *obj,
                                               guint         prop_id,
                                               const GValue *value,
                                               GParamSpec   *pspec);
static void     evd_socket_get_property       (GObject    *obj,
                                               guint       prop_id,
                                               GValue     *value,
                                               GParamSpec *pspec);

static gboolean evd_socket_watch              (EvdSocket  *self,
                                               GError    **error);
static gboolean evd_socket_unwatch            (EvdSocket  *self,
                                               GError    **error);

static void     evd_socket_remove_from_event_cache (EvdSocket *socket);

static guint    evd_socket_timeout_add        (EvdSocket   *self,
                                               guint        timeout,
                                               GSourceFunc  callback,
                                               gpointer     user_data);

static gssize   evd_socket_write_internal     (EvdSocket    *self,
                                               const gchar  *buf,
                                               gsize         size,
                                               GError      **error);

static gboolean evd_socket_cleanup            (EvdSocket  *self,
                                               GError    **error);

static void
evd_socket_class_init (EvdSocketClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_socket_dispose;
  obj_class->finalize = evd_socket_finalize;
  obj_class->get_property = evd_socket_get_property;
  obj_class->set_property = evd_socket_set_property;

  class->event_handler = NULL;
  class->invoke_on_read = evd_socket_invoke_on_read;

  /* install signals */
  evd_socket_signals[SIGNAL_ERROR] =
    g_signal_new ("error",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdSocketClass, error),
                  NULL, NULL,
                  evd_marshal_VOID__INT_STRING,
                  G_TYPE_NONE, 2,
                  G_TYPE_INT,
                  G_TYPE_STRING);

  evd_socket_signals[SIGNAL_STATE_CHANGED] =
    g_signal_new ("state-changed",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdSocketClass, state_changed),
                  NULL, NULL,
                  evd_marshal_VOID__UINT_UINT,
                  G_TYPE_NONE, 2,
                  G_TYPE_UINT,
                  G_TYPE_UINT);

  evd_socket_signals[SIGNAL_CLOSE] =
    g_signal_new ("close",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdSocketClass, close),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  evd_socket_signals[SIGNAL_NEW_CONNECTION] =
    g_signal_new ("new-connection",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdSocketClass, new_connection),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__OBJECT,
                  G_TYPE_NONE, 1,
                  EVD_TYPE_SOCKET);

  /* install properties */
  g_object_class_install_property (obj_class, PROP_SOCKET,
                                   g_param_spec_object ("socket",
                                                       "The actual GSocket",
                                                       "The underlaying socket",
                                                       G_TYPE_SOCKET,
                                                       G_PARAM_READABLE |
                                                       G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_FAMILY,
                                   g_param_spec_enum ("family",
                                                      "Socket family",
                                                      "The sockets address family",
                                                      G_TYPE_SOCKET_FAMILY,
                                                      G_SOCKET_FAMILY_INVALID,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TYPE,
                                   g_param_spec_enum ("type",
                                                      "Socket type",
                                                      "The sockets type",
                                                      G_TYPE_SOCKET_TYPE,
                                                      G_SOCKET_TYPE_INVALID,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_PROTOCOL,
                                   g_param_spec_enum ("protocol",
                                                      "Socket protocol",
                                                      "The id of the protocol to use, or -1 for unknown",
                                                      G_TYPE_SOCKET_PROTOCOL,
                                                      G_SOCKET_PROTOCOL_UNKNOWN,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_READ_CLOSURE,
                                   g_param_spec_boxed ("read-handler",
                                                       "Read closure",
                                                       "The callback closure that will be invoked when data is ready to be read",
                                                       G_TYPE_CLOSURE,
                                                       G_PARAM_READWRITE));

  g_object_class_install_property (obj_class, PROP_CONNECT_TIMEOUT,
                                   g_param_spec_uint ("connect-timeout",
                                                      "Connect timeout",
                                                      "The timeout in seconds to wait for a connect operation",
                                                      0,
                                                      G_MAXUINT,
                                                      DEFAULT_CONNECT_TIMEOUT,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_GROUP,
                                   g_param_spec_object ("group",
                                                        "Socket group",
                                                        "The socket group owning this socket",
                                                        EVD_TYPE_SOCKET_GROUP,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_AUTO_WRITE,
                                   g_param_spec_boolean ("auto-write",
                                                         "Enables/disables auto-write support on socket",
                                                         "A socket with auto-write on, will automatically retry uncomplete data writes",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_PRIORITY,
                                   g_param_spec_int ("priority",
                                                     "The priority of socket's events",
                                                     "The priority of the socket when dispatching its events in the loop",
                                                     G_PRIORITY_HIGH,
                                                     G_PRIORITY_LOW,
                                                     G_PRIORITY_DEFAULT,
                                                     G_PARAM_READWRITE |
                                                     G_PARAM_STATIC_STRINGS));

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdSocketPrivate));
}

static void
evd_socket_init (EvdSocket *self)
{
  EvdSocketPrivate *priv;

  priv = EVD_SOCKET_GET_PRIVATE (self);
  self->priv = priv;

  /* initialize private members */
  priv->socket   = NULL;
  priv->family   = G_SOCKET_FAMILY_INVALID;
  priv->type     = G_SOCKET_TYPE_INVALID;
  priv->protocol = G_SOCKET_PROTOCOL_UNKNOWN;

  priv->connect_timeout = DEFAULT_CONNECT_TIMEOUT;
  priv->connect_cancellable = NULL;

  priv->status = EVD_SOCKET_STATE_CLOSED;

  priv->auto_write   = FALSE;
  priv->read_buffer  = g_string_new ("");
  priv->write_buffer = g_string_new ("");

  priv->context = g_main_context_get_thread_default ();
  /* TODO: check if we should 'ref' the context */

  priv->read_src_id = 0;
  priv->write_src_id = 0;

  priv->cond = 0;

  priv->priority        = G_PRIORITY_DEFAULT;
  priv->actual_priority = G_PRIORITY_DEFAULT;

  evd_socket_manager_ref ();
}

static void
evd_socket_dispose (GObject *obj)
{
  EvdSocket *self = EVD_SOCKET (obj);

  evd_socket_cleanup (self, NULL);

  evd_socket_set_group (self, NULL);

  G_OBJECT_CLASS (evd_socket_parent_class)->dispose (obj);
}

static void
evd_socket_finalize (GObject *obj)
{
  G_OBJECT_CLASS (evd_socket_parent_class)->finalize (obj);

  //  g_debug ("[EvdSocket 0x%X] Socket finalized", (guintptr) obj);

  evd_socket_manager_unref ();
}

static void
evd_socket_set_property (GObject      *obj,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  EvdSocket *self;

  self = EVD_SOCKET (obj);

  switch (prop_id)
    {
    case PROP_FAMILY:
      self->priv->family = g_value_get_enum (value);
      break;

    case PROP_TYPE:
      self->priv->type = g_value_get_enum (value);
      break;

    case PROP_PROTOCOL:
      self->priv->protocol = g_value_get_enum (value);
      break;

    case PROP_CONNECT_TIMEOUT:
      self->priv->connect_timeout = g_value_get_uint (value);
      break;

    case PROP_GROUP:
      {
        EvdSocketGroup *group;

        if (self->priv->group != NULL)
          evd_socket_group_remove (self->priv->group, self);

        group = g_value_get_object (value);
        if (group != NULL)
          evd_socket_group_add (group, self);

        break;
      }

    case PROP_AUTO_WRITE:
      if (self->priv->auto_write == TRUE)
        {
          g_string_free (self->priv->write_buffer, TRUE);
          self->priv->write_buffer = g_string_new ("");
        }
      self->priv->auto_write = g_value_get_boolean (value);
      break;

    case PROP_PRIORITY:
      evd_socket_set_priority (self, g_value_get_uint (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_socket_get_property (GObject    *obj,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  EvdSocket *self;

  self = EVD_SOCKET (obj);

  switch (prop_id)
    {
    case PROP_SOCKET:
      g_value_set_object (value, self->priv->socket);
      break;

    case PROP_FAMILY:
      if (self->priv->socket != NULL)
        g_value_set_enum (value, g_socket_get_family (self->priv->socket));
      else
        g_value_set_enum (value, self->priv->family);
      break;

    case PROP_TYPE:
      if (self->priv->socket != NULL)
        {
          GSocketType type;
          g_object_get (self->priv->socket, "type", &type, NULL);
          g_value_set_enum (value, type);
        }
      else
        g_value_set_enum (value, self->priv->type);

      break;

    case PROP_PROTOCOL:
      if (self->priv->socket != NULL)
        g_value_set_enum (value, g_socket_get_protocol (self->priv->socket));
      else
        g_value_set_enum (value, self->priv->protocol);
      break;

    case PROP_CONNECT_TIMEOUT:
      g_value_set_uint (value, self->priv->connect_timeout);
      break;

    case PROP_GROUP:
      g_value_set_object (value, self->priv->group);
      break;

    case PROP_AUTO_WRITE:
      g_value_set_boolean (value, self->priv->auto_write);
      break;

    case PROP_PRIORITY:
      g_value_set_uint (value, self->priv->priority);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_socket_set_socket (EvdSocket *self, GSocket *socket)
{
  self->priv->socket = socket;
  g_object_ref (socket);

  g_object_set (socket,
                "blocking", FALSE,
                "keepalive", TRUE,
                NULL);
}

static gboolean
evd_socket_check (EvdSocket  *self,
                  GError    **error)
{
  GSocket *socket;

  if (self->priv->socket != NULL)
    return TRUE;

  socket = g_socket_new (self->priv->family,
                         self->priv->type,
                         self->priv->protocol,
                         error);

  if (socket != NULL)
    {
      evd_socket_set_socket (self, socket);
      return TRUE;
    }
  else
    return FALSE;
}

static gboolean
evd_socket_watch (EvdSocket *self, GError **error)
{
  return evd_socket_manager_add_socket (self, error);
}

static gboolean
evd_socket_unwatch (EvdSocket *self, GError **error)
{
  return evd_socket_manager_del_socket (self, error);
}

static void
evd_socket_invoke_on_read_internal (EvdSocket *self)
{
  EvdSocketClass *class = EVD_SOCKET_GET_CLASS (self);

  if (class->invoke_on_read != NULL)
    class->invoke_on_read (self);
}

static void
evd_socket_invoke_on_write (EvdSocket *self)
{
  GClosure *closure = NULL;

  closure = evd_stream_get_on_write (EVD_STREAM (self));
  if (closure != NULL)
    {
      GValue params = { 0, };

      g_value_init (&params, EVD_TYPE_SOCKET);
      g_value_set_object (&params, self);

      g_object_ref (self);
      g_closure_invoke (closure, NULL, 1, &params, NULL);
      g_object_unref (self);

      g_value_unset (&params);
    }
}

static void
evd_socket_figureout_from_address (EvdSocket *self,
                                   GSocketAddress *address)
{
  self->priv->family = g_socket_address_get_family (address);

  if (self->priv->type == G_SOCKET_TYPE_INVALID)
    {
      if (self->priv->protocol == G_SOCKET_PROTOCOL_UDP)
        self->priv->type = G_SOCKET_TYPE_DATAGRAM;
      else
        self->priv->type = G_SOCKET_TYPE_STREAM;
    }

  if (self->priv->protocol == G_SOCKET_PROTOCOL_UNKNOWN)
    self->priv->protocol = G_SOCKET_PROTOCOL_DEFAULT;
}

static gboolean
evd_socket_connect_timeout (gpointer user_data)
{
  EvdSocket *self = EVD_SOCKET (user_data);
  GError *error = NULL;

  /* emit 'connect-timeout' error */
  error = g_error_new (g_quark_from_static_string (DOMAIN_QUARK_STRING),
                       EVD_SOCKET_ERROR_CONNECT_TIMEOUT,
                       "Connect timeout");
  evd_socket_throw_error (self, error);

  self->priv->connect_timeout_src_id = 0;
  if (! evd_socket_close (self, &error))
    {
      /* emit 'close' error signal */
      error->code = EVD_SOCKET_ERROR_CLOSE;
      evd_socket_throw_error (self, error);

      return FALSE;
    }

  return FALSE;
}

static gboolean
evd_socket_is_connected (EvdSocket *self, GError **error)
{
  if (self->priv->socket == NULL)
    {
      *error = g_error_new (g_quark_from_static_string (DOMAIN_QUARK_STRING),
                            EVD_SOCKET_ERROR_NOT_CONNECTED,
                            "Socket is not connected");

      return FALSE;
    }

  return TRUE;
}

static void
evd_socket_remove_from_event_cache (EvdSocket *socket)
{
  GQueue *queue;
  EvdSocketEvent *msg;
  guint i;

  queue = socket->priv->event_queue_cache;

  if (queue == NULL)
    return;

  i = 0;
  while (i<g_queue_get_length (queue))
    {
      msg = g_queue_peek_nth (queue, i);

      if (msg->socket == socket)
        {
          EvdSocketEvent *event = (EvdSocketEvent *) msg;

          g_queue_remove (queue, (gconstpointer) msg);
          g_object_unref (event->socket);
          g_free (event);
        }
      else
        i++;
    }
}

static gssize
evd_socket_read_internal (EvdSocket *self,
                          gchar     *buffer,
                          gsize      size,
                          GError   **error)
{
  GError *_error = NULL;
  gssize read_from_buf = 0;
  gssize read_from_socket = 0;
  gchar *buf;

  if (self->priv->read_buffer->len > 0)
    {
      read_from_buf = MIN (self->priv->read_buffer->len, size);
      g_memmove (buffer, self->priv->read_buffer->str, read_from_buf);
      size -= read_from_buf;
    }
  else
    buf = buffer;

  if (size > 0)
    {
      buf = (gchar *) ( ((guintptr) buffer) + read_from_buf);
      if ( (read_from_socket = g_socket_receive (self->priv->socket,
                                                 buf,
                                                 size,
                                                 NULL,
                                                 &_error)) < 0)
        {
          if ( (_error)->code == G_IO_ERROR_WOULD_BLOCK)
            {
              g_error_free (_error);
              read_from_socket = 0;
            }
          else
            {
              if (error != NULL)
                *error = _error;
              return -1;
            }
        }
      else
        {
          if ( (read_from_socket > 0) && (read_from_socket < size) )
            self->priv->cond &= (~ G_IO_IN);
        }
    }

  if (read_from_buf > 0)
    {
      g_string_erase (self->priv->read_buffer, 0, read_from_buf);

      if ( (self->priv->read_buffer->len == 0) && (read_from_socket == 0) )
        self->priv->cond &= (~ G_IO_IN);
    }

  return read_from_buf + read_from_socket;
}

static gboolean
evd_socket_write_wait_timeout (gpointer user_data)
{
  EvdSocket *self = EVD_SOCKET (user_data);

  self->priv->write_src_id = 0;

  if (self->priv->write_buffer->len > 0)
    {
      GError *error = NULL;
      gsize size;
      gssize actual_size;

      size = MIN (self->priv->write_buffer->len, MAX_BLOCK_SIZE);

      if ( (actual_size = evd_socket_write_internal (self,
                                                     self->priv->write_buffer->str,
                                                     size,
                                                     &error)) < 0)
        {
          evd_socket_throw_error (self, error);
        }
      else
        {
          if (actual_size > 0)
            g_string_erase (self->priv->write_buffer, 0, actual_size);
        }
    }

  if ( (self->priv->cond & G_IO_OUT) > 0)
    evd_socket_invoke_on_write (self);

  return FALSE;
}

static gssize
evd_socket_write_internal (EvdSocket    *self,
                           const gchar  *buf,
                           gsize         size,
                           GError      **error)
{
  gssize actual_size = 0;
  gsize limited_size;
  guint _retry_wait = 0;

  if (! evd_socket_is_connected (self, error))
    return FALSE;

  /* handle latency and bandwidth */
  limited_size = evd_stream_request_write (EVD_STREAM (self),
                                           size,
                                           &_retry_wait);

  if ( (limited_size > 0) && (self->priv->group != NULL) )
    limited_size = evd_stream_request_write (EVD_STREAM (self->priv->group),
                                             limited_size,
                                             &_retry_wait);

  if (limited_size > 0)
    {
      actual_size = g_socket_send (self->priv->socket,
                                   buf,
                                   limited_size,
                                   NULL,
                                   error);

      if (actual_size >= 0)
        {
          if (actual_size > 0)
            {
              if (self->priv->group != NULL)
                evd_stream_report_write (EVD_STREAM (self->priv->group),
                                     actual_size);

              evd_stream_report_write (EVD_STREAM (self),
                                       actual_size);
            }

          if (actual_size < limited_size)
            {
              self->priv->cond &= (~ G_IO_OUT);
            }
        }
    }

  if (actual_size < size)
    {
      /* wait and re-schedule a write-event */
      if (self->priv->write_src_id == 0)
        self->priv->write_src_id =
          evd_socket_timeout_add (self,
                                  _retry_wait,
                                  (GSourceFunc) evd_socket_write_wait_timeout,
                                  (gpointer) self);
    }

  return actual_size;
}

static guint
evd_socket_timeout_add (EvdSocket   *self,
                        guint        timeout,
                        GSourceFunc  callback,
                        gpointer     user_data)
{
  guint src_id;
  GSource *src;

  if (timeout == 0)
    src = g_idle_source_new ();
  else
    src = g_timeout_source_new (timeout);

  g_source_set_callback (src,
                         callback,
                         user_data,
                         NULL);
  src_id = g_source_attach (src, self->priv->context);
  g_source_unref (src);

  return src_id;
}

static gboolean
evd_socket_read_wait_timeout (gpointer user_data)
{
  EvdSocket *self = EVD_SOCKET (user_data);

  self->priv->read_src_id = 0;
  evd_socket_invoke_on_read_internal (self);

  return FALSE;
}

static gboolean
evd_socket_read_buffer_add_data (EvdSocket    *self,
                                 const gchar  *buf,
                                 gsize         size,
                                 GError      **error)
{
  if (self->priv->read_buffer->len + size > MAX_READ_BUFFER_SIZE)
    {
      if (error != NULL)
        *error = g_error_new (g_quark_from_static_string (DOMAIN_QUARK_STRING),
                              EVD_SOCKET_ERROR_BUFFER_OVERFLOW,
                              "Read buffer is full");

      return FALSE;
    }
  else
    {
      g_string_append_len (self->priv->read_buffer,
                           buf,
                           size);

      return TRUE;
    }
}

static gboolean
evd_socket_write_buffer_add_data (EvdSocket    *self,
                                  const gchar  *buf,
                                  gsize         size,
                                  GError      **error)
{
  if (self->priv->write_buffer->len + size > MAX_WRITE_BUFFER_SIZE)
    {
      if (error != NULL)
        *error = g_error_new (g_quark_from_static_string (DOMAIN_QUARK_STRING),
                              EVD_SOCKET_ERROR_BUFFER_OVERFLOW,
                              "Write buffer is full");

      return FALSE;
    }
  else
    {
      g_string_append_len (self->priv->write_buffer,
                           buf,
                           size);

      return TRUE;
    }
}

static gboolean
evd_socket_cleanup (EvdSocket *self, GError **error)
{
  EvdSocketClass *class = EVD_SOCKET_GET_CLASS (self);

  if (class->cleanup != NULL)
    return class->cleanup (self, error);
  else
    return evd_socket_cleanup_protected (self, error);
}

/* protected methods */

void
evd_socket_set_status (EvdSocket *self, EvdSocketState status)
{
  EvdSocketState old_status;

  old_status = self->priv->status;
  self->priv->status = status;

  g_signal_emit (self,
                 evd_socket_signals[SIGNAL_STATE_CHANGED],
                 0,
                 status,
                 old_status,
                 NULL);
}

void
evd_socket_throw_error (EvdSocket *self, GError *error)
{
  g_signal_emit (self,
                 evd_socket_signals[SIGNAL_ERROR],
                 0,
                 error->code,
                 error->message,
                 NULL);
}

gboolean
evd_socket_event_handler (gpointer data)
{
  EvdSocketEvent *event = (EvdSocketEvent *) data;
  EvdSocket *socket;
  GIOCondition condition;
  EvdSocketClass *class;
  GError *error = NULL;
  gboolean dont_free = FALSE;

  socket = event->socket;

  if (! EVD_IS_SOCKET (socket))
    {
      g_free (event);
      return FALSE;
    }

  g_object_ref (socket);

  condition = event->condition;

  class = EVD_SOCKET_GET_CLASS (socket);
  if (class->event_handler != NULL)
    class->event_handler (socket, condition);
  else
    {
      if (socket->priv->status == EVD_SOCKET_STATE_LISTENING)
        {
          EvdSocket *client;

          while ( (socket->priv->status == EVD_SOCKET_STATE_LISTENING) &&
                  ((client = evd_socket_accept (socket, &error)) != NULL) )
            {
              /* TODO: allow external function to decide whether to
                 accept/refuse the new connection */

              /* fire 'new-connection' signal */
              g_signal_emit (socket,
                             evd_socket_signals[SIGNAL_NEW_CONNECTION],
                             0,
                             client, NULL);
              g_object_unref (client);
            }

          if (error != NULL)
            {
              if (error->code != G_IO_ERROR_WOULD_BLOCK)
                {
                  /* error accepting connection, emit 'error' signal */
                  error->code = EVD_SOCKET_ERROR_ACCEPT;
                  evd_socket_throw_error (socket, error);

                  evd_socket_timeout_add (socket,
                                          0,
                                          evd_socket_event_handler,
                                          event);
                  dont_free = TRUE;
                }

              if (error != NULL)
                g_error_free (error);
            }
        }
      else
        {
          if (condition & G_IO_ERR)
            {
              /* socket error, emit 'error' signal */
              error = g_error_new (g_quark_from_string (DOMAIN_QUARK_STRING),
                                   EVD_SOCKET_ERROR_UNKNOWN,
                                   "Socket error");

              evd_socket_throw_error (socket, error);
              evd_socket_close (socket, &error);
            }
          else if (condition & G_IO_HUP)
            {
              evd_socket_close (socket, &error);
            }
          else if (socket->priv->status != EVD_SOCKET_STATE_CLOSED)
            {
              if (condition & G_IO_OUT)
                {
                  if (socket->priv->status == EVD_SOCKET_STATE_CONNECTING)
                    {
                      /* restore priority */
                      socket->priv->actual_priority = socket->priv->priority;

                      /* remove any connect_timeout src */
                      if (socket->priv->connect_timeout_src_id > 0)
                        g_source_remove (socket->priv->connect_timeout_src_id);

                      evd_socket_set_status (socket, EVD_SOCKET_STATE_CONNECTED);
                    }

                  if ( (socket->priv->cond & G_IO_OUT) == 0)
                    {
                      socket->priv->cond |= G_IO_OUT;
                      evd_socket_invoke_on_write (socket);
                    }
                }

              if (condition & G_IO_IN)
                {
                  socket->priv->cond |= G_IO_IN;
                  evd_socket_invoke_on_read_internal (socket);
                }
            }
        }
    }

  if (! dont_free)
    g_free (event);

  g_object_unref (socket);

  return FALSE;
}

gboolean
evd_socket_event_list_handler (gpointer data)
{
  GQueue *queue = data;
  EvdSocketEvent *msg;

  while ( (msg = (EvdSocketEvent *) g_queue_pop_head (queue)) != NULL)
    {
      EvdSocket *socket;

      socket = msg->socket;
      if (! EVD_IS_SOCKET (socket))
        {
          g_free (msg);
        }
      else
        {
          socket->priv->event_queue_cache = queue;
          evd_socket_event_handler ((gpointer) msg);
          socket->priv->event_queue_cache = NULL;
        }
    }

  g_queue_free (queue);

  return FALSE;
}

void
evd_socket_set_group (EvdSocket *self, EvdSocketGroup *group)
{
  self->priv->group = group;
  if (group != NULL)
    {
      if (self->priv->read_buffer->len > 0)
        evd_socket_invoke_on_read_internal (self);

      if (evd_socket_can_write (self))
        evd_socket_invoke_on_write (self);
    }
}

void
evd_socket_set_actual_priority (EvdSocket *self, gint priority)
{
  self->priv->actual_priority = priority;
}

gint
evd_socket_get_actual_priority (EvdSocket *self)
{
  return self->priv->actual_priority;
}

void
evd_socket_invoke_on_read (EvdSocket *self)
{
  GClosure *closure = NULL;

  closure = evd_stream_get_on_read (EVD_STREAM (self));
  if (closure != NULL)
    {
      GValue params = { 0, };

      g_value_init (&params, EVD_TYPE_SOCKET);
      g_value_set_object (&params, self);

      g_object_ref (self);
      g_closure_invoke (closure, NULL, 1, &params, NULL);
      g_object_unref (self);

      g_value_unset (&params);
    }
}

gboolean
evd_socket_cleanup_protected (EvdSocket *self, GError **error)
{
  gboolean result = TRUE;

  evd_socket_remove_from_event_cache (self);

  if (self->priv->connect_cancellable != NULL)
    {
      g_object_unref (self->priv->connect_cancellable);
      self->priv->connect_cancellable = NULL;
    }

  if (self->priv->connect_timeout_src_id != 0)
    {
      g_source_remove (self->priv->connect_timeout_src_id);
      self->priv->connect_timeout_src_id = 0;
    }

  if (self->priv->read_src_id != 0)
    {
      g_source_remove (self->priv->read_src_id);
      self->priv->read_src_id = 0;
    }

  if (self->priv->write_src_id != 0)
    {
      g_source_remove (self->priv->write_src_id);
      self->priv->write_src_id = 0;
    }

  if (self->priv->read_buffer != NULL)
    {
      g_string_free (self->priv->read_buffer, TRUE);
      self->priv->read_buffer = NULL;
    }

  if (self->priv->write_buffer != NULL)
    {
      g_string_free (self->priv->write_buffer, TRUE);
      self->priv->write_buffer = NULL;
    }

  if (self->priv->socket != NULL)
    {
      if (! g_socket_is_closed (self->priv->socket))
        {
          if ( (! evd_socket_unwatch (self, error)) ||
               (! g_socket_close (self->priv->socket, error)) )
            result = FALSE;
        }

      g_object_unref (self->priv->socket);
      self->priv->socket = NULL;

      self->priv->status = EVD_SOCKET_STATE_CLOSED;
    }

  return result;
}

/* public methods */

EvdSocket *
evd_socket_new (void)
{
  EvdSocket *self;

  self = g_object_new (EVD_TYPE_SOCKET, NULL);

  return self;
}

GSocket *
evd_socket_get_socket (EvdSocket *self)
{
  return self->priv->socket;
}

GMainContext *
evd_socket_get_context (EvdSocket *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), NULL);

  return self->priv->context;
}

GSocketFamily
evd_socket_get_family (EvdSocket *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), 0);

  return self->priv->family;
}

EvdSocketState
evd_socket_get_status (EvdSocket *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), 0);

  return self->priv->status;
}

EvdSocketGroup *
evd_socket_get_group (EvdSocket *self)
{
  return self->priv->group;
}

gint
evd_socket_get_priority (EvdSocket *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), G_PRIORITY_DEFAULT);

  return self->priv->priority;
}

void
evd_socket_set_priority (EvdSocket *self, gint priority)
{
  g_return_if_fail (EVD_IS_SOCKET (self));
  g_return_if_fail ( (priority <= G_PRIORITY_LOW) &&
                     (priority >= G_PRIORITY_HIGH));

  if (self->priv->actual_priority == self->priv->priority)
    self->priv->actual_priority = priority;
  self->priv->priority = priority;
}

gboolean
evd_socket_close (EvdSocket *self, GError **error)
{
  gboolean result;
  gboolean fire_on_close = FALSE;

  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  fire_on_close = (self->priv->status != EVD_SOCKET_STATE_CLOSED);
  result = evd_socket_cleanup (self, error);

  self->priv->read_buffer = g_string_new ("");
  self->priv->write_buffer = g_string_new ("");

  /* fire 'close' signal */
  if (fire_on_close)
    {
      g_object_ref (self);
      g_signal_emit (self, evd_socket_signals[SIGNAL_CLOSE], 0, NULL);
      g_object_unref (self);
    }

  return result;
}

gboolean
evd_socket_bind (EvdSocket       *self,
                 GSocketAddress  *address,
                 gboolean         allow_reuse,
                 GError         **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);
  g_return_val_if_fail (G_IS_SOCKET_ADDRESS (address), FALSE);

  if (self->priv->status != EVD_SOCKET_STATE_CLOSED)
    if (! evd_socket_close (self, error))
      return FALSE;

  evd_socket_figureout_from_address (self, address);

  if (! evd_socket_check (self, error))
    return FALSE;

  if (g_socket_bind (self->priv->socket,
                     address,
                     allow_reuse,
                     error))
    {
      evd_socket_set_status (self, EVD_SOCKET_STATE_BOUND);

      return TRUE;
    }

  return FALSE;
}

gboolean
evd_socket_listen (EvdSocket *self, GError **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  if (! evd_socket_check (self, error))
    return FALSE;

  if (self->priv->status != EVD_SOCKET_STATE_BOUND)
    {
      *error = g_error_new (g_quark_from_static_string (DOMAIN_QUARK_STRING),
                            EVD_SOCKET_ERROR_NOT_BOUND,
                            "Socket is not bound to an address");

      return FALSE;
    }

  g_socket_set_listen_backlog (self->priv->socket, 10000); /* TODO: change by a max-conn prop */
  if (g_socket_listen (self->priv->socket, error))
    if (evd_socket_watch (self, error))
      {
        self->priv->actual_priority = G_PRIORITY_HIGH + 1;
        evd_socket_set_status (self, EVD_SOCKET_STATE_LISTENING);

        return TRUE;
      }

  return FALSE;
}

EvdSocket *
evd_socket_accept (EvdSocket *self, GError **error)
{
  EvdSocket *client = NULL;
  GSocket *client_socket;

  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  if ( (client_socket = g_socket_accept (self->priv->socket, NULL, error)) != NULL)
    {
      client = EVD_SOCKET (g_object_new (G_OBJECT_TYPE (self), NULL, NULL));
      evd_socket_set_socket (client, client_socket);

      if (evd_socket_watch (client, error))
        {
          evd_socket_set_status (client, EVD_SOCKET_STATE_CONNECTED);

          return client;
        }
    }

  return NULL;
}

gboolean
evd_socket_connect_to (EvdSocket        *self,
                       GSocketAddress   *address,
                       GError          **error)
{
  GError *_error = NULL;

  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);
  g_return_val_if_fail (G_IS_SOCKET_ADDRESS (address), FALSE);

  evd_socket_figureout_from_address (self, address);

  if (! evd_socket_check (self, error))
    return FALSE;

  /* if socket not closed, close it first */
  if ( (self->priv->status == EVD_SOCKET_STATE_CONNECTED) ||
       (self->priv->status == EVD_SOCKET_STATE_CONNECTING) ||
       (self->priv->status == EVD_SOCKET_STATE_LISTENING) )
    if (! evd_socket_close (self, error))
      return FALSE;

  /* launch connect timeout */
  if (self->priv->connect_timeout > 0)
    self->priv->connect_timeout_src_id =
      g_timeout_add (self->priv->connect_timeout * 1000,
                     (GSourceFunc) evd_socket_connect_timeout,
                     (gpointer) self);

  if (self->priv->connect_cancellable == NULL)
    self->priv->connect_cancellable = g_cancellable_new ();

  if (! g_socket_connect (self->priv->socket,
                          address,
                          self->priv->connect_cancellable,
                          &_error))
    {
      /* an error ocurred, but error-pending
         is normal as on async ops */
      if ((_error)->code != G_IO_ERROR_PENDING)
        {
          if (error != NULL)
            *error = _error;

          return FALSE;
        }
      else
        {
          g_error_free (_error);
          _error = NULL;
        }
    }

  /* g_socket_connect returns TRUE on a non-blocking socket, however
     fills error with "connection in progress" hint */
  if (_error != NULL)
    {
      g_error_free (_error);
      _error = NULL;
    }

  if (! evd_socket_watch (self, error))
    {
      evd_socket_cleanup (self, NULL);
      return FALSE;
    }
  else
    {
      self->priv->actual_priority = G_PRIORITY_HIGH + 2;
      evd_socket_set_status (self, EVD_SOCKET_STATE_CONNECTING);

      return TRUE;
    }
}

gboolean
evd_socket_cancel_connect (EvdSocket *self, GError **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  if (self->priv->status == EVD_SOCKET_STATE_CONNECTING)
    {
      if (self->priv->connect_timeout_src_id > 0)
        g_source_remove (self->priv->connect_timeout_src_id);

      g_cancellable_cancel (self->priv->connect_cancellable);

      return evd_socket_close (self, error);
    }
  else
    {
      if (error != NULL)
        *error = g_error_new (g_quark_from_static_string (DOMAIN_QUARK_STRING),
                              EVD_SOCKET_ERROR_NOT_CONNECTING,
                              "Socket is not connecting");

      return FALSE;
    }
}

void
evd_socket_set_read_handler (EvdSocket            *self,
                             EvdSocketReadHandler  handler,
                             gpointer              user_data)
{
  GClosure *closure;

  g_return_if_fail (EVD_IS_SOCKET (self));

  if (handler == NULL)
    {
      evd_stream_set_on_read (EVD_STREAM (self), NULL);
      return;
    }

  closure = g_cclosure_new (G_CALLBACK (handler),
                            user_data,
                            NULL);

  if (G_CLOSURE_NEEDS_MARSHAL (closure))
    {
      GClosureMarshal marshal = g_cclosure_marshal_VOID__VOID;

      g_closure_set_marshal (closure, marshal);
    }

  evd_stream_set_on_read (EVD_STREAM (self), closure);
}

void
evd_socket_set_write_handler (EvdSocket             *self,
                              EvdSocketWriteHandler  handler,
                              gpointer               user_data)
{
  GClosure *closure;

  g_return_if_fail (EVD_IS_SOCKET (self));

  if (handler == NULL)
    {
      evd_stream_set_on_write (EVD_STREAM (self), NULL);
      return;
    }

  closure = g_cclosure_new (G_CALLBACK (handler),
                            user_data,
                            NULL);

  if (G_CLOSURE_NEEDS_MARSHAL (closure))
    {
      GClosureMarshal marshal = g_cclosure_marshal_VOID__VOID;

      g_closure_set_marshal (closure, marshal);
    }

  evd_stream_set_on_write (EVD_STREAM (self), closure);
}

gssize
evd_socket_read_buffer (EvdSocket *self,
                        gchar     *buffer,
                        gsize      size,
                        GError   **error)
{
  gssize actual_size = -1;
  gsize limited_size;
  guint _retry_wait = 0;

  g_return_val_if_fail (EVD_IS_SOCKET (self), -1);
  g_return_val_if_fail (buffer != NULL, -1);
  g_return_val_if_fail (size > 0, -1);

  if (! evd_socket_is_connected (self, error))
    return -1;

  /* handle latency and bandwidth */
  limited_size = evd_stream_request_read (EVD_STREAM (self),
                                          size,
                                          &_retry_wait);

  if ( (limited_size > 0) && (self->priv->group != NULL) )
    limited_size = evd_stream_request_read (EVD_STREAM (self->priv->group),
                                            limited_size,
                                            &_retry_wait);

  if (limited_size == 0)
    {
      if (self->priv->read_src_id == 0)
        self->priv->read_src_id =
          evd_socket_timeout_add (self,
                                  _retry_wait,
                                  (GSourceFunc) evd_socket_read_wait_timeout,
                                  (gpointer) self);

      self->priv->cond &= (~ G_IO_IN);
      return 0;
    }

  if ( (actual_size = evd_socket_read_internal (self,
                                                buffer,
                                                limited_size,
                                                error)) > 0)
    {
      if (self->priv->group != NULL)
        evd_stream_report_read (EVD_STREAM (self->priv->group),
                                (gsize) actual_size);

      evd_stream_report_read (EVD_STREAM (self),
                              (gsize) actual_size);

      /* check if we should keep reading after retry_wait miliseconds */
      if ( (self->priv->read_src_id == 0) && (actual_size == limited_size) )
        {
          self->priv->read_src_id =
            evd_socket_timeout_add (self,
                                    _retry_wait,
                                    (GSourceFunc) evd_socket_read_wait_timeout,
                                    (gpointer) self);
        }
    }

  return actual_size;
}

gchar *
evd_socket_read (EvdSocket *self,
                 gsize     *size,
                 GError   **error)
{
  gchar *buf;

  g_return_val_if_fail (EVD_IS_SOCKET (self), NULL);
  g_return_val_if_fail (*size > 0, NULL);

  buf = g_new0 (gchar, *size);
  buf[0] = 0;

  if ( (*size = evd_socket_read_buffer (self,
                                        buf,
                                        *size,
                                        error)) > 0)
    return buf;
  else
    g_free (buf);

  return NULL;
}

gssize
evd_socket_write_buffer (EvdSocket    *self,
                         const gchar  *buf,
                         gsize         size,
                         GError      **error)
{
  gsize orig_size = size;

  g_return_val_if_fail (EVD_IS_SOCKET (self), -1);
  g_return_val_if_fail (buf != NULL, -1);
  g_return_val_if_fail (size > 0, -1);

  if (! evd_socket_is_connected (self, error))
    return FALSE;

  /* check if there is data pending to be written */
  if (self->priv->write_buffer->len > 0)
    {
      if (! evd_socket_write_buffer_add_data (self, buf, size, error))
        return -1;
      else
        return 0;
    }
  else
    {
      gsize actual_size;

      actual_size = evd_socket_write_internal (self,
                                               buf,
                                               size,
                                               error);

      if ( (self->priv->auto_write) &&
           (actual_size < orig_size) && (actual_size >= 0) )
        {
          if (! evd_socket_write_buffer_add_data (self,
                                   (gchar *) (((guintptr) buf) + (actual_size)),
                                   orig_size - actual_size,
                                   error))
            return -1;
          else
            return 0;
        }

      return actual_size;
    }
}

gssize
evd_socket_write (EvdSocket    *self,
                  const gchar  *buf,
                  GError      **error)
{
  gsize size;

  size = strlen (buf);

  return evd_socket_write_buffer (self, buf, size, error);
}

gssize
evd_socket_unread_buffer (EvdSocket    *self,
                          const gchar  *buffer,
                          gsize         size,
                          GError      **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), -1);
  g_return_val_if_fail (buffer != NULL, -1);
  g_return_val_if_fail (size > 0, -1);

  if (! evd_socket_read_buffer_add_data (self,
                                         buffer,
                                         size,
                                         error))
    {
      return -1;
    }
  else
    {
      self->priv->cond |= G_IO_IN;

      /* TODO: Should we invoke on-read handler here? */

      return size;
    }
}

gssize
evd_socket_unread (EvdSocket    *self,
                   const gchar  *buffer,
                   GError      **error)
{
  g_return_val_if_fail (buffer != NULL, -1);

  return evd_socket_unread_buffer (self, buffer, strlen (buffer), error);
}

gboolean
evd_socket_has_write_data_pending (EvdSocket *self)
{
  return (self->priv->write_buffer->len > 0);
}

gsize
evd_socket_get_max_readable (EvdSocket *self)
{
  gsize size = MAX_BLOCK_SIZE;
  gsize limited_size;

  limited_size = evd_stream_request_read (EVD_STREAM (self),
                                          size,
                                          NULL);

  if ( (limited_size > 0) && (self->priv->group != NULL) )
    limited_size = evd_stream_request_read (EVD_STREAM (self->priv->group),
                                            limited_size,
                                            NULL);

  return limited_size;
}

gsize
evd_socket_get_max_writable (EvdSocket *self)
{
  gsize size = MAX_BLOCK_SIZE;
  gsize limited_size;

  limited_size = evd_stream_request_write (EVD_STREAM (self),
                                           size,
                                           NULL);

  if ( (limited_size > 0) && (self->priv->group != NULL) )
    limited_size = evd_stream_request_write (EVD_STREAM (self->priv->group),
                                             limited_size,
                                             NULL);

  return limited_size;
}

gboolean
evd_socket_can_read (EvdSocket *self)
{
  return ( (self->priv->cond & G_IO_IN) > 0);
}

gboolean
evd_socket_can_write (EvdSocket *self)
{
  return ( (self->priv->cond & G_IO_OUT) > 0);
}

GSocketAddress *
evd_socket_get_remote_address (EvdSocket  *self,
                               GError    **error)
{
  if (self->priv->socket == NULL)
    return NULL;
  else
    return g_socket_get_remote_address (self->priv->socket, error);
}

GSocketAddress *
evd_socket_get_local_address (EvdSocket  *self,
                              GError    **error)
{
  if (self->priv->socket == NULL)
    return NULL;
  else
    return g_socket_get_local_address (self->priv->socket, error);
}
