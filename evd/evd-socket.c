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

#include "evd-utils.h"
#include "evd-marshal.h"
#include "evd-socket-manager.h"
#include "evd-stream-protected.h"
#include "evd-resolver.h"

#include "evd-socket-input-stream.h"
#include "evd-socket-output-stream.h"
#include "evd-tls-input-stream.h"
#include "evd-tls-output-stream.h"

#include "evd-socket.h"
#include "evd-socket-protected.h"

#define DOMAIN_QUARK_STRING     "org.eventdance.lib.socket"

#define MAX_BLOCK_SIZE          G_MAXUINT16
#define MAX_READ_BUFFER_SIZE    G_MAXUINT16
#define MAX_WRITE_BUFFER_SIZE   G_MAXUINT16

#define TLS_ENABLED(socket)       (socket->priv->tls_enabled == TRUE)
#define TLS_SESSION(socket)       evd_socket_stream_get_tls_session (EVD_SOCKET_STREAM (socket))
#define TLS_AUTOSTART(socket)     evd_socket_stream_get_tls_autostart (EVD_SOCKET_STREAM (socket))
#define TLS_INPUT_STREAM(socket)  G_INPUT_STREAM (socket->priv->tls_input_stream)
#define TLS_OUTPUT_STREAM(socket) G_OUTPUT_STREAM (socket->priv->tls_output_stream)
#define TLS_READ_PENDING(socket)  g_input_stream_has_pending (TLS_INPUT_STREAM(socket))

G_DEFINE_TYPE (EvdSocket, evd_socket, EVD_TYPE_SOCKET_STREAM)

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
  EvdSocketState   sub_status;
  GMainContext    *context;

  EvdSocketGroup *group;

  gboolean auto_write;

  gint read_src_id;
  gint write_src_id;
  gboolean awaiting_read;

  GString *read_buffer;
  GString *write_buffer;

  GIOCondition cond;
  GIOCondition watched_cond;
  gboolean watched;

  gint actual_priority;
  gint priority;

  EvdResolverRequest *resolve_request;

  gboolean bind_allow_reuse;

  gboolean tls_enabled;
  gboolean delayed_close;

  guint         event_handler_src_id;
  GIOCondition  new_cond;
  GMutex       *mutex;

  EvdSocketInputStream  *socket_input_stream;
  EvdSocketOutputStream *socket_output_stream;
  EvdTlsInputStream     *tls_input_stream;
  EvdTlsOutputStream    *tls_output_stream;
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
  PROP_AUTO_WRITE,
  PROP_GROUP,
  PROP_PRIORITY,
  PROP_STATUS,
  PROP_TLS_ACTIVE
};

static GQuark evd_socket_err_domain;

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

static void     evd_socket_closure_changed    (EvdStream *stream);

static gssize   evd_socket_write_internal     (EvdSocket    *self,
                                               const gchar  *buf,
                                               gsize         size,
                                               GError      **error);

static gboolean evd_socket_cleanup            (EvdSocket  *self,
                                               GError    **error);

static gboolean evd_socket_read_wait_timeout  (gpointer user_data);

static void     evd_socket_invoke_on_write_internal (EvdSocket *self);

static void     evd_socket_manage_read_condition    (EvdSocket *self);
static void     evd_socket_manage_write_condition   (EvdSocket *self);

static void     evd_socket_copy_properties          (EvdStream *self,
                                                     EvdStream *target);

static void
evd_socket_class_init (EvdSocketClass *class)
{
  GObjectClass *obj_class;
  EvdStreamClass *stream_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_socket_dispose;
  obj_class->finalize = evd_socket_finalize;
  obj_class->get_property = evd_socket_get_property;
  obj_class->set_property = evd_socket_set_property;

  class->handle_condition = NULL;
  class->invoke_on_read = evd_socket_invoke_on_read;

  stream_class = EVD_STREAM_CLASS (class);
  stream_class->read_closure_changed = evd_socket_closure_changed;
  stream_class->write_closure_changed = evd_socket_closure_changed;
  stream_class->copy_properties = evd_socket_copy_properties;

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

  g_object_class_install_property (obj_class, PROP_STATUS,
                                   g_param_spec_uint ("status",
                                                      "Socket status",
                                                      "The current status of the socket (closed, connected, listening, etc)",
                                                      0,
                                                      EVD_SOCKET_STATE_LISTENING,
                                                      EVD_SOCKET_STATE_CLOSED,
                                                      G_PARAM_READABLE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TLS_ACTIVE,
                                   g_param_spec_boolean ("tls-active",
                                                         "Tells whether SSL/TLS is active",
                                                         "Returns TRUE if socket has SSL/TLS active, FALSE otherwise. SSL/TLS is activated by calling 'starttls' on a socket",
                                                         FALSE,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdSocketPrivate));

  evd_socket_err_domain = g_quark_from_static_string (DOMAIN_QUARK_STRING);
}

static void
evd_socket_init (EvdSocket *self)
{
  EvdSocketPrivate *priv;

  priv = EVD_SOCKET_GET_PRIVATE (self);
  self->priv = priv;

  priv->socket   = NULL;
  priv->family   = G_SOCKET_FAMILY_INVALID;
  priv->type     = G_SOCKET_TYPE_INVALID;
  priv->protocol = G_SOCKET_PROTOCOL_UNKNOWN;

  priv->group = NULL;

  priv->status     = EVD_SOCKET_STATE_CLOSED;
  priv->sub_status = EVD_SOCKET_STATE_CLOSED;

  priv->auto_write   = FALSE;
  priv->read_buffer  = g_string_new ("");
  priv->write_buffer = g_string_new ("");

  priv->context = g_main_context_get_thread_default ();
  if (priv->context != NULL)
    g_main_context_ref (priv->context);

  priv->read_src_id = 0;
  priv->write_src_id = 0;
  priv->awaiting_read = FALSE;

  priv->cond = 0;
  priv->watched_cond = G_IO_IN | G_IO_OUT;
  priv->watched = FALSE;


  priv->priority        = G_PRIORITY_DEFAULT;
  priv->actual_priority = G_PRIORITY_DEFAULT;

  priv->resolve_request = NULL;

  priv->tls_enabled = FALSE;
  priv->delayed_close = FALSE;

  priv->event_handler_src_id = 0;
  priv->new_cond = 0;

  priv->mutex = g_mutex_new ();

  priv->socket_input_stream = NULL;
  priv->socket_output_stream = NULL;
  priv->tls_output_stream = NULL;
  priv->tls_input_stream = NULL;
}

static void
evd_socket_dispose (GObject *obj)
{
  EvdSocket *self = EVD_SOCKET (obj);

  evd_socket_cleanup (self, NULL);

  evd_socket_set_group (self, NULL);

  if (self->priv->resolve_request != NULL)
    {
      g_object_unref (self->priv->resolve_request);
      self->priv->resolve_request = NULL;
    }

  if (self->priv->context != NULL)
    {
      g_main_context_unref (self->priv->context);
      self->priv->context = NULL;
    }

  G_OBJECT_CLASS (evd_socket_parent_class)->dispose (obj);
}

static void
evd_socket_finalize (GObject *obj)
{
  EvdSocket *self = EVD_SOCKET (obj);

  g_mutex_free (self->priv->mutex);

  G_OBJECT_CLASS (evd_socket_parent_class)->finalize (obj);

  //  g_debug ("[EvdSocket 0x%X] Socket finalized", (guintptr) obj);
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

    case PROP_GROUP:
      g_value_set_object (value, evd_socket_get_group (self));
      break;

    case PROP_AUTO_WRITE:
      g_value_set_boolean (value, self->priv->auto_write);
      break;

    case PROP_PRIORITY:
      g_value_set_int (value, self->priv->priority);
      break;

    case PROP_STATUS:
      g_value_set_uint (value, self->priv->status);
      break;

    case PROP_TLS_ACTIVE:
      g_value_set_boolean (value, self->priv->tls_enabled);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_socket_closure_changed (EvdStream *stream)
{
  EvdSocket *self = EVD_SOCKET (stream);

  if (self->priv->group != NULL)
    evd_socket_group_remove (self->priv->group, self);
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
evd_socket_watch (EvdSocket *self, GIOCondition cond, GError **error)
{
  if ( (! self->priv->watched &&
        evd_socket_manager_add_socket (self, cond, error)) ||
       (self->priv->watched &&
        evd_socket_manager_mod_socket (self, cond, error)) )
    {
      self->priv->watched = TRUE;
      return TRUE;
    }
  else
    return FALSE;
}

static gboolean
evd_socket_unwatch (EvdSocket *self, GError **error)
{
  if (evd_socket_manager_del_socket (self, error))
    {
      self->priv->watched = FALSE;
      return TRUE;
    }
  else
    return FALSE;
}

static gboolean
evd_socket_tls_handshake (gpointer user_data)
{
  EvdSocket *self = EVD_SOCKET (user_data);
  GError *error = NULL;

  if (evd_tls_session_handshake (TLS_SESSION (self), &error))
    {
      self->priv->cond = G_IO_OUT;
      self->priv->watched_cond = G_IO_IN | G_IO_OUT;

      if (evd_socket_watch (self, self->priv->watched_cond, &error))
        {
          evd_socket_set_status (self, EVD_SOCKET_STATE_CONNECTED);
          evd_socket_invoke_on_write_internal (self);
        }
      else
        {
          evd_socket_throw_error (self, error);
          evd_socket_close (self, NULL);
        }
    }
  else if (error == NULL)
    {
      /* update socket conditions to watch, based on
         session's record direction */
      self->priv->watched_cond =
        evd_tls_session_get_direction (TLS_SESSION (self));
      evd_socket_watch (self, self->priv->watched_cond, &error);
    }

  if (error != NULL)
    {
      evd_socket_throw_error (self, error);
      evd_socket_close (self, NULL);
    }

  return FALSE;
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
evd_socket_invoke_on_write_internal (EvdSocket *self)
{
  /* TODO: add an 'invoke_on_write' virtual method and call it here */

  evd_socket_invoke_on_write (self);
}

static gboolean
evd_socket_check_address (EvdSocket       *self,
                          GSocketAddress  *address,
                          GError         **error)
{
  if (self->priv->family == G_SOCKET_FAMILY_INVALID)
    self->priv->family = g_socket_address_get_family (address);
  else if (self->priv->family != g_socket_address_get_family (address))
    {
      if (error != NULL)
        *error = g_error_new (evd_socket_err_domain,
                              EVD_SOCKET_ERROR_INVALID_ADDRESS,
                              "Socket family and address family mismatch");

      return FALSE;
    }

  if (self->priv->type == G_SOCKET_TYPE_INVALID)
    {
      if (self->priv->protocol == G_SOCKET_PROTOCOL_UDP)
        self->priv->type = G_SOCKET_TYPE_DATAGRAM;
      else
        self->priv->type = G_SOCKET_TYPE_STREAM;
    }

  if (self->priv->protocol == G_SOCKET_PROTOCOL_UNKNOWN)
    self->priv->protocol = G_SOCKET_PROTOCOL_DEFAULT;

  return TRUE;
}

static gboolean
evd_socket_is_connected (EvdSocket *self, GError **error)
{
  if (self->priv->socket == NULL)
    {
      *error = g_error_new (evd_socket_err_domain,
                            EVD_SOCKET_ERROR_NOT_CONNECTED,
                            "Socket is not connected");

      return FALSE;
    }

  return TRUE;
}

static void
evd_socket_input_stream_drained (GInputStream *stream,
                                 gpointer      user_data)
{
  EvdSocket *self = EVD_SOCKET (user_data);
  GError *error = NULL;

  self->priv->cond &= ~G_IO_IN;

  self->priv->watched_cond |= G_IO_IN;
  if (! evd_socket_watch (self, self->priv->watched_cond, &error))
    evd_socket_throw_error (self, error);
}

static void
evd_socket_output_stream_filled (GOutputStream *stream,
                                 gpointer       user_data)
{
  EvdSocket *self = EVD_SOCKET (user_data);
  GError *error = NULL;

  self->priv->cond &= (~ G_IO_OUT);

  self->priv->watched_cond |= G_IO_OUT;
  if (! evd_socket_watch (self, self->priv->watched_cond, &error))
    evd_socket_throw_error (self, error);
}

static void
evd_socket_setup_streams (EvdSocket *self)
{
  if (self->priv->socket_input_stream == NULL)
    {
      self->priv->socket_input_stream =
        evd_socket_input_stream_new (self);

      g_signal_connect (self->priv->socket_input_stream,
                        "drained",
                        G_CALLBACK (evd_socket_input_stream_drained),
                        self);
    }

  if (self->priv->socket_output_stream == NULL)
    {
      self->priv->socket_output_stream =
        evd_socket_output_stream_new (self);

      g_signal_connect (self->priv->socket_output_stream,
                        "filled",
                        G_CALLBACK (evd_socket_output_stream_filled),
                        self);
    }
}

static gssize
evd_socket_read_filtered (EvdSocket *self,
                          gchar     *buffer,
                          gsize      size,
                          GError   **error)
{
  gssize actual_size = 0;
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

  if (limited_size > 0)
    {
      if ( (actual_size =
            g_input_stream_read (G_INPUT_STREAM (self->priv->socket_input_stream),
                                 buffer,
                                 limited_size,
                                 NULL,
                                 error)) < 0)
        {
          return -1;
        }
      else if (actual_size > 0)
        {
          if (self->priv->group != NULL)
            evd_stream_report_read (EVD_STREAM (self->priv->group),
                                    actual_size);

          evd_stream_report_read (EVD_STREAM (self),
                                  actual_size);
        }
    }

  if (limited_size < size && actual_size == limited_size)
    {
      if (! self->priv->awaiting_read)
        {
          self->priv->awaiting_read = TRUE;

          if (self->priv->read_src_id != 0)
            {
              GSource *src;

              src = g_main_context_find_source_by_id (self->priv->context,
                                                      self->priv->read_src_id);
              g_source_destroy (src);

              self->priv->read_src_id = 0;
            }
        }

      self->priv->read_src_id =
        evd_timeout_add (self->priv->context,
                         _retry_wait,
                         (GSourceFunc) evd_socket_read_wait_timeout,
                         (gpointer) self);
    }
  else
    {
      self->priv->awaiting_read = FALSE;
    }

  return actual_size;
}

static gboolean
evd_socket_read_wait_timeout (gpointer user_data)
{
  EvdSocket *self = EVD_SOCKET (user_data);

  if (self->priv->status == EVD_SOCKET_STATE_CLOSED)
    return FALSE;

  self->priv->read_src_id = 0;

  if ( (self->priv->cond & G_IO_IN) > 0 ||
       self->priv->read_buffer->len > 0)
    {
      evd_socket_manage_read_condition (self);
    }
  else if (self->priv->delayed_close)
    {
      evd_socket_close (self, NULL);
    }

  return FALSE;
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

  if ( (self->priv->cond & G_IO_OUT) > 0 && self->priv->write_src_id == 0)
    evd_socket_manage_write_condition (self);

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
    return -1;

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
      actual_size =
        g_output_stream_write (G_OUTPUT_STREAM (self->priv->socket_output_stream),
                               buf,
                               limited_size,
                               NULL,
                               error);

      if (actual_size > 0)
        {
          if (self->priv->group != NULL)
            evd_stream_report_write (EVD_STREAM (self->priv->group),
                                     actual_size);

          evd_stream_report_write (EVD_STREAM (self),
                                   actual_size);
        }
    }

  if (actual_size >= 0 && actual_size < size)
    {
      if (self->priv->write_src_id == 0)
        {
          self->priv->write_src_id =
            evd_timeout_add (self->priv->context,
                             _retry_wait,
                             (GSourceFunc) evd_socket_write_wait_timeout,
                             (gpointer) self);
        }
    }

  return actual_size;
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
        *error = g_error_new (evd_socket_err_domain,
                              EVD_SOCKET_ERROR_BUFFER_OVERFLOW,
                              "Read buffer is full");

      return FALSE;
    }
  else
    {
      g_string_append_len (self->priv->read_buffer, buf, size);

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
        *error = g_error_new (evd_socket_err_domain,
                              EVD_SOCKET_ERROR_BUFFER_OVERFLOW,
                              "Write buffer is full");

      return FALSE;
    }
  else
    {
      g_string_append_len (self->priv->write_buffer, buf, size);

      return TRUE;
    }
}

static gssize
evd_socket_write_filtered (EvdSocket    *self,
                           const gchar  *buf,
                           gsize         size,
                           GError      **error)
{
  gsize orig_size = size;

  g_return_val_if_fail (EVD_IS_SOCKET (self), -1);
  g_return_val_if_fail (buf != NULL, -1);
  g_return_val_if_fail (size > 0, -1);

  if (! evd_socket_is_connected (self, error))
    return -1;

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

static gboolean
evd_socket_cleanup (EvdSocket *self, GError **error)
{
  EvdSocketClass *class = EVD_SOCKET_GET_CLASS (self);

  if (class->cleanup != NULL)
    return class->cleanup (self, error);
  else
    return evd_socket_cleanup_protected (self, error);
}

static void
evd_socket_on_resolve (EvdResolver         *resolver,
                       EvdResolverRequest  *request,
                       gpointer             user_data)
{
  EvdSocket *self = EVD_SOCKET (user_data);
  GList *addresses;
  GError *error = NULL;

  if (self->priv->status != EVD_SOCKET_STATE_RESOLVING)
    return;

  if ( (addresses = evd_resolver_request_get_result (request, &error)) != NULL)
    {
      GSocketAddress *socket_address;
      GList *node = addresses;
      gboolean match = FALSE;

      /* TODO: by now only the first matching address will be used */
      while (node != NULL)
        {
          socket_address = G_SOCKET_ADDRESS (node->data);
          if (evd_socket_check_address (self, socket_address, NULL))
            {
              match = TRUE;
              break;
            }
          else
            node = node->next;
        }

      if (match)
        {
          gint sub_status;

          self->priv->status = EVD_SOCKET_STATE_CLOSED;

          sub_status = self->priv->sub_status;
          self->priv->sub_status = EVD_SOCKET_STATE_CLOSED;

          switch (sub_status)
            {
            case EVD_SOCKET_STATE_LISTENING:
              {
                if (! evd_socket_listen_addr (self, socket_address, &error) &&
                    error != NULL)
                  evd_socket_throw_error (self, error);
                break;
              }
            case EVD_SOCKET_STATE_BOUND:
              {
                if (! evd_socket_bind_addr (self,
                                            socket_address,
                                            self->priv->bind_allow_reuse,
                                            &error) && error != NULL)
                  evd_socket_throw_error (self, error);
                break;
              }
            case EVD_SOCKET_STATE_CONNECTED:
              {
                if (! evd_socket_connect_addr (self,
                                               socket_address,
                                               &error) && error != NULL)
                  evd_socket_throw_error (self, error);
                break;
              }
            default:
              {
              }
            }
        }
      else
        {
          error = g_error_new (evd_socket_err_domain,
                               EVD_SOCKET_ERROR_RESOLVE,
                               "None of the resolved addresses match socket family");
          evd_socket_throw_error (self, error);
          evd_socket_close (self, NULL);
        }

      evd_resolver_free_addresses (addresses);
    }
  else
    {
      error->code = EVD_SOCKET_ERROR_RESOLVE;
      evd_socket_throw_error (self, error);
    }
}

static void
evd_socket_resolve_address (EvdSocket      *self,
                            const gchar    *address,
                            EvdSocketState  action)
{
  EvdResolver *resolver;

  self->priv->sub_status = action;

  if (self->priv->resolve_request == NULL)
    {
      resolver = evd_resolver_get_default ();
      self->priv->resolve_request = evd_resolver_resolve (resolver,
                                                          address,
                                                          evd_socket_on_resolve,
                                                          self);
      g_object_unref (resolver);
    }
  else
    {
      g_object_set (self->priv->resolve_request,
                    "address", address,
                    NULL);
      evd_resolver_request_resolve (self->priv->resolve_request);
    }
}

static void
evd_socket_manage_read_condition (EvdSocket *self)
{
  if (self->priv->status == EVD_SOCKET_STATE_TLS_HANDSHAKING)
    evd_socket_tls_handshake (self);
  else
    if (self->priv->read_src_id == 0)
      evd_socket_invoke_on_read_internal (self);
}

static void
evd_socket_manage_write_condition (EvdSocket *self)
{
  if (self->priv->status == EVD_SOCKET_STATE_TLS_HANDSHAKING)
    evd_socket_tls_handshake (self);
  else
    if (self->priv->write_src_id == 0)
      evd_socket_invoke_on_write_internal (self);
}

static void
evd_socket_handle_condition_internal (EvdSocket *self)
{
  EvdSocketClass *class;
  GIOCondition cond;

  g_mutex_lock (self->priv->mutex);

  self->priv->event_handler_src_id = 0;
  cond = self->priv->new_cond;
  self->priv->new_cond = 0;

  g_mutex_unlock (self->priv->mutex);

  class = EVD_SOCKET_GET_CLASS (self);
  if (class->handle_condition != NULL)
    class->handle_condition (self, cond);
  else
    evd_socket_handle_condition (self, cond);
}

static gboolean
evd_socket_handle_condition_cb (gpointer data)
{
  EvdSocket *self = EVD_SOCKET (data);

  if (EVD_IS_SOCKET (self) && self->priv->watched)
    evd_socket_handle_condition_internal (self);

  return FALSE;
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
  g_error_free (error);
}

static gboolean
evd_socket_confirm_read_condition (EvdSocket *self)
{
  gchar buf[1] = { 0, };

  if (evd_socket_read_len (self, buf, 1, NULL) == 1)
    {
      evd_socket_unread_len (self, buf, 1, NULL);
      return TRUE;
    }

  return FALSE;
}

void
evd_socket_notify_condition (EvdSocket    *self,
                             GIOCondition  cond)
{
  /* ATTENTION! this runs in socket manager's thread */

  g_mutex_lock (self->priv->mutex);

  self->priv->new_cond |= cond;

  if (self->priv->event_handler_src_id == 0)
    {
      GSource *src;

      src = g_idle_source_new ();
      g_source_set_priority (src, self->priv->actual_priority);
      g_source_set_callback (src,
                             evd_socket_handle_condition_cb,
                             (gpointer) self,
                             NULL);
      self->priv->event_handler_src_id =
        g_source_attach (src, self->priv->context);

      g_source_unref (src);
    }

  g_mutex_unlock (self->priv->mutex);
}

static void
evd_socket_copy_properties (EvdStream *_self, EvdStream *_target)
{
  EvdSocket *self = EVD_SOCKET (_self);
  EvdSocket *target = EVD_SOCKET (_target);

  evd_socket_set_priority (target, self->priv->priority);

  target->priv->auto_write = self->priv->auto_write;

  if (self->priv->group != NULL)
    evd_socket_group_add (self->priv->group, target);

  EVD_STREAM_CLASS (evd_socket_parent_class)->
    copy_properties (_self, _target);
}

void
evd_socket_handle_condition (EvdSocket *self, GIOCondition condition)
{
  GError *error = NULL;

  g_object_ref (self);

  if (self->priv->status == EVD_SOCKET_STATE_LISTENING)
    {
      EvdSocket *client;

      while ( (self->priv->status == EVD_SOCKET_STATE_LISTENING) &&
              ((client = evd_socket_accept (self, &error)) != NULL) )
        {
          evd_socket_copy_properties (EVD_STREAM (self), EVD_STREAM (client));

          /* fire 'new-connection' signal */
          g_signal_emit (self,
                         evd_socket_signals[SIGNAL_NEW_CONNECTION],
                         0,
                         client, NULL);

          if (TLS_AUTOSTART (client))
            {
              /* copy TLS session properties from listener to accepted socket */
              evd_tls_session_copy_properties (TLS_SESSION (self),
                                               TLS_SESSION (client));

              if (! evd_socket_starttls (client, EVD_TLS_MODE_SERVER, &error))
                {
                  evd_socket_throw_error (client, error);
                  evd_socket_close (client, NULL);
                }
            }
          else
            {
              g_object_unref (client);
            }
        }

      if (error != NULL)
        {
          if (error->code != G_IO_ERROR_WOULD_BLOCK)
            {
              /* error accepting connection, emit 'error' signal */
              error->code = EVD_SOCKET_ERROR_ACCEPT;
              evd_socket_throw_error (self, error);

              self->priv->new_cond |= condition;
              evd_timeout_add (self->priv->context,
                               0,
                               evd_socket_handle_condition_cb,
                               self);
            }

          if (error != NULL)
            g_error_free (error);
        }
    }
  else
    {
      if (condition & G_IO_ERR)
        {
          if (self->priv->status == EVD_SOCKET_STATE_CONNECTING)
            {
              /* @TODO: assume connection was refused */
              error = g_error_new (evd_socket_err_domain,
                                   EVD_SOCKET_ERROR_UNKNOWN,
                                   "Connection refused");
            }
          else
            {
              error = g_error_new (evd_socket_err_domain,
                                   EVD_SOCKET_ERROR_UNKNOWN,
                                   "Unknown socket error");
            }

          evd_socket_throw_error (self, error);
          evd_socket_close (self, NULL);
        }
      else if (self->priv->status != EVD_SOCKET_STATE_CLOSED)
        {
          /* write condition */
          if (condition & G_IO_OUT)
            {
              self->priv->watched_cond &= (~G_IO_OUT);
              if (evd_socket_watch (self, self->priv->watched_cond, &error))
                {
                  if (self->priv->status == EVD_SOCKET_STATE_CONNECTING)
                    {
                      /* socket has just connected! */

                      /* restore priority */
                      self->priv->actual_priority = self->priv->priority;

                      evd_socket_setup_streams (self);

                      self->priv->cond |= G_IO_OUT;
                      evd_socket_set_status (self, EVD_SOCKET_STATE_CONNECTED);
                      self->priv->cond &= ~G_IO_OUT;

                      if (TLS_AUTOSTART (self) && (! self->priv->tls_enabled))
                        if (! evd_socket_starttls (self, EVD_TLS_MODE_CLIENT, &error))
                          {
                            evd_socket_throw_error (self, error);
                            evd_socket_close (self, NULL);
                          }
                    }

                  if ( (self->priv->cond & G_IO_OUT) == 0)
                    {
                      self->priv->cond |= G_IO_OUT;

                      evd_socket_manage_write_condition (self);
                    }
                }
              else
                {
                  evd_socket_throw_error (self, error);
                  evd_socket_close (self, NULL);
                }
            }

          /* read condition */
          if (condition & G_IO_IN)
            {
              self->priv->watched_cond &= ~G_IO_IN;
              if (! evd_socket_watch (self,
                                      self->priv->watched_cond,
                                      &error))
                {
                  evd_socket_throw_error (self, error);
                  evd_socket_close (self, NULL);
                }
              else if ( (self->priv->cond & G_IO_IN) == 0)
                {
                  if ( TLS_ENABLED (self) ||
                       (condition & G_IO_HUP) == 0 ||
                       evd_socket_confirm_read_condition (self) )
                    {
                      self->priv->cond |= G_IO_IN;

                      evd_socket_manage_read_condition (self);
                    }
                }
            }
        }

    }

  if (condition & G_IO_HUP)
    {
      if (self->priv->awaiting_read ||
          (TLS_ENABLED (self) && TLS_READ_PENDING (self)) ||
          self->priv->cond & G_IO_IN)
        {
          self->priv->delayed_close = TRUE;

          if (self->priv->read_src_id == 0)
            {
              self->priv->read_src_id =
                evd_timeout_add (self->priv->context,
                                 0,
                                 (GSourceFunc) evd_socket_read_wait_timeout,
                                 (gpointer) self);
            }
        }
      else
        {
          evd_socket_close (self, &error);
        }
    }

  g_object_unref (self);
}

void
evd_socket_set_group (EvdSocket *self, EvdSocketGroup *group)
{
  if (self->priv->group == group)
    return;

  if (self->priv->group != NULL)
    {
      EvdSocketGroup *current_group;

      current_group = self->priv->group;
      self->priv->group = NULL;

      evd_stream_set_on_read (EVD_STREAM (self), NULL);
      evd_stream_set_on_write (EVD_STREAM (self), NULL);

      g_object_unref (current_group);
    }

  self->priv->group = group;

  if (group != NULL)
    {
      g_object_ref (self->priv->group);

      if (self->priv->read_buffer->len > 0)
        evd_socket_invoke_on_read_internal (self);

      if (evd_socket_can_write (self))
        evd_socket_invoke_on_write_internal (self);
    }
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

  if (self->priv->resolve_request != NULL &&
      evd_resolver_request_is_active (self->priv->resolve_request))
    evd_resolver_cancel (self->priv->resolve_request);

  self->priv->family = G_SOCKET_FAMILY_INVALID;

  self->priv->status = EVD_SOCKET_STATE_CLOSED;

  self->priv->watched = FALSE;
  self->priv->watched_cond = 0;
  self->priv->cond = 0;

  if (self->priv->tls_enabled &&
      (! evd_tls_session_close (TLS_SESSION (self), error)) )
    result = FALSE;

  self->priv->tls_enabled = FALSE;
  self->priv->delayed_close = FALSE;

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
    }

  g_mutex_lock (self->priv->mutex);
  if (self->priv->event_handler_src_id != 0)
    {
      g_source_remove (self->priv->event_handler_src_id);
      self->priv->event_handler_src_id = 0;
      self->priv->new_cond = 0;
    }
  g_mutex_unlock (self->priv->mutex);

  /* cleanup streams */
  if (self->priv->socket_input_stream != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->priv->socket_input_stream,
                                            evd_socket_input_stream_drained,
                                            self);
      g_object_unref (self->priv->socket_input_stream);
      self->priv->socket_input_stream = NULL;
    }

  if (self->priv->socket_output_stream != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->priv->socket_output_stream,
                                            evd_socket_output_stream_filled,
                                            self);
      g_object_unref (self->priv->socket_output_stream);
      self->priv->socket_output_stream = NULL;
    }

  if (self->priv->tls_input_stream != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->priv->tls_input_stream,
                                            evd_socket_input_stream_drained,
                                            self);
      g_object_unref (self->priv->tls_input_stream);
      self->priv->tls_input_stream = NULL;
    }

  if (self->priv->tls_output_stream != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->priv->tls_output_stream,
                                            evd_socket_output_stream_filled,
                                            self);
      g_object_unref (self->priv->tls_output_stream);
      self->priv->tls_output_stream = NULL;
    }

  return result;
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

      self->priv->watched_cond = G_IO_IN | G_IO_OUT;
      if (evd_socket_watch (client, self->priv->watched_cond, error))
        {
          evd_socket_setup_streams (client);

          evd_socket_set_status (client, EVD_SOCKET_STATE_CONNECTED);

          return client;
        }
    }

  return NULL;
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
  g_return_val_if_fail (EVD_IS_SOCKET (self), NULL);

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
  g_return_val_if_fail (EVD_IS_SOCKET (self), NULL);

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
evd_socket_bind_addr (EvdSocket       *self,
                      GSocketAddress  *address,
                      gboolean         allow_reuse,
                      GError         **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);
  g_return_val_if_fail (G_IS_SOCKET_ADDRESS (address), FALSE);

  if (self->priv->status != EVD_SOCKET_STATE_CLOSED)
    {
      if (error != NULL)
        *error = g_error_new (evd_socket_err_domain,
                              EVD_SOCKET_ERROR_SOCKET_ACTIVE,
                              "Socket is currently active, should be closed first before requesting to bind");

      return FALSE;
    }

  if (! evd_socket_check_address (self, address, error))
    return FALSE;

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
  else
    {
      evd_socket_cleanup (self, NULL);
    }

  return FALSE;
}

gboolean
evd_socket_bind (EvdSocket    *self,
                 const gchar  *address,
                 gboolean      allow_reuse,
                 GError      **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);
  g_return_val_if_fail (address != NULL, FALSE);

  if (self->priv->status != EVD_SOCKET_STATE_CLOSED)
    {
      if (error != NULL)
        *error = g_error_new (evd_socket_err_domain,
                              EVD_SOCKET_ERROR_SOCKET_ACTIVE,
                              "Socket is currently active, should be closed first before requesting to bind");

      return FALSE;
    }

  /* we need to cache the allow_reuse flag as this op will complete async */
  self->priv->bind_allow_reuse = allow_reuse;

  evd_socket_set_status (self, EVD_SOCKET_STATE_RESOLVING);

  evd_socket_resolve_address (self, address, EVD_SOCKET_STATE_BOUND);

  return TRUE;
}

gboolean
evd_socket_listen_addr (EvdSocket *self, GSocketAddress *address, GError **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  if (self->priv->status != EVD_SOCKET_STATE_CLOSED &&
      (self->priv->status != EVD_SOCKET_STATE_BOUND || address != NULL) )
    {
      if (error != NULL)
        *error = g_error_new (evd_socket_err_domain,
                              EVD_SOCKET_ERROR_SOCKET_ACTIVE,
                              "Socket is currently active, should be closed first before requesting to listen");

      return FALSE;
    }

  if (address != NULL)
    if (! evd_socket_bind_addr (self, address, TRUE, error))
      return FALSE;

  if (self->priv->status != EVD_SOCKET_STATE_BOUND)
    {
      /* this is to consider that socket could have been closed
         during 'state-changed' signal handler after call to bind */
      return FALSE;
    }

  g_socket_set_listen_backlog (self->priv->socket, 10000); /* TODO: change by a max-conn prop */
  if (g_socket_listen (self->priv->socket, error))
    {
      self->priv->watched_cond = G_IO_IN;
      if (evd_socket_watch (self, self->priv->watched_cond, error))
        {
          self->priv->cond = 0;
          self->priv->actual_priority = G_PRIORITY_HIGH + 1;
          evd_socket_set_status (self, EVD_SOCKET_STATE_LISTENING);

          return TRUE;
        }
      else
        {
          evd_socket_cleanup (self, NULL);
        }
    }

  return FALSE;
}

gboolean
evd_socket_listen (EvdSocket *self, const gchar *address, GError **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  if (address == NULL)
    return evd_socket_listen_addr (self, NULL, error);
  else
    if (self->priv->status != EVD_SOCKET_STATE_CLOSED)
      {
        if (error != NULL)
          *error = g_error_new (evd_socket_err_domain,
                                EVD_SOCKET_ERROR_SOCKET_ACTIVE,
                                "Socket is currently active, should be closed first before requesting to listen");

        return FALSE;
      }

  evd_socket_set_status (self, EVD_SOCKET_STATE_RESOLVING);

  evd_socket_resolve_address (self, address, EVD_SOCKET_STATE_LISTENING);

  return TRUE;
}

gboolean
evd_socket_connect_addr (EvdSocket        *self,
                         GSocketAddress   *address,
                         GError          **error)
{
  GError *_error = NULL;

  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);
  g_return_val_if_fail (G_IS_SOCKET_ADDRESS (address), FALSE);

  if (self->priv->status != EVD_SOCKET_STATE_CLOSED &&
      (self->priv->status != EVD_SOCKET_STATE_BOUND ||
       self->priv->protocol != G_SOCKET_PROTOCOL_UDP) )
    {
      if (error != NULL)
        *error = g_error_new (evd_socket_err_domain,
                              EVD_SOCKET_ERROR_SOCKET_ACTIVE,
                              "Socket is currently active, should be closed first before requesting to connect");

      return FALSE;
    }

  if (! evd_socket_check_address (self, address, error))
    return FALSE;

  if (! evd_socket_check (self, error))
    return FALSE;

  if (! g_socket_connect (self->priv->socket,
                          address,
                          NULL,
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

  if (! evd_socket_watch (self, G_IO_IN | G_IO_OUT, error))
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
evd_socket_connect_to (EvdSocket    *self,
                       const gchar  *address,
                       GError      **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);
  g_return_val_if_fail (address != NULL, FALSE);

  if (self->priv->status != EVD_SOCKET_STATE_CLOSED &&
      (self->priv->status != EVD_SOCKET_STATE_BOUND ||
       self->priv->protocol != G_SOCKET_PROTOCOL_UDP) )
    {
      if (error != NULL)
        *error = g_error_new (evd_socket_err_domain,
                              EVD_SOCKET_ERROR_SOCKET_ACTIVE,
                              "Socket is currently active, should be closed first before requesting to connect");

      return FALSE;
    }

  evd_socket_set_status (self, EVD_SOCKET_STATE_RESOLVING);

  evd_socket_resolve_address (self, address, EVD_SOCKET_STATE_CONNECTED);

  return TRUE;
}

gssize
evd_socket_read_len (EvdSocket  *self,
                     gchar      *buffer,
                     gsize       size,
                     GError    **error)
{
  gssize read_from_buf = 0;
  gssize read_from_socket = 0;
  gchar *buf;

  g_return_val_if_fail (EVD_IS_SOCKET (self), 0);

  if (size == 0)
    return 0;

  g_return_val_if_fail (buffer != NULL, 0);

  if (self->priv->read_buffer->len > 0)
    {
      read_from_buf = MIN (self->priv->read_buffer->len, size);
      g_memmove (buffer, self->priv->read_buffer->str, read_from_buf);
      size -= read_from_buf;

      buf = (gchar *) ( ((guintptr) buffer) + read_from_buf);
    }
  else
    {
      buf = buffer;
    }

  if (size > 0)
    {
      if (TLS_ENABLED (self))
        {
          read_from_socket = g_input_stream_read (TLS_INPUT_STREAM (self),
                                                  buf,
                                                  size,
                                                  NULL,
                                                  error);

          /* TODO: improve this ASAP, too hackish! */
          if (read_from_socket > 0 && read_from_socket < size &&
              self->priv->read_src_id == 0)
            {
              self->priv->read_src_id =
                evd_timeout_add (self->priv->context,
                                 0,
                                 evd_socket_read_wait_timeout,
                                 self);
            }
        }
      else
        read_from_socket = evd_socket_read_filtered (self,
                                                     buf,
                                                     size,
                                                     error);
    }

  if (read_from_socket == 0)
    {
      if (self->priv->delayed_close && ! self->priv->awaiting_read)
        evd_socket_close (self, NULL);
    }
  else if (read_from_socket < 0)
    {
      /* hack to gracefully recover from peer
         abruptly closing TLS connection */
      if (error != NULL && *error != NULL &&
          (*error)->code == EVD_TLS_ERROR_UNEXPECTED_PACKET_LEN)
        {
          g_error_free (*error);
          *error = NULL;

          read_from_socket = 0;

          evd_socket_close (self, NULL);
        }

      read_from_buf = 0;
    }
  else
    {
      if (read_from_buf > 0)
        g_string_erase (self->priv->read_buffer, 0, read_from_buf);
    }

  return read_from_buf + read_from_socket;
}

gchar *
evd_socket_read (EvdSocket  *self,
                 gsize      *size,
                 GError    **error)
{
  gchar buf[MAX_BLOCK_SIZE + 1] = { 0, };
  gssize actual_size;
  gchar *data = NULL;

  g_return_val_if_fail (EVD_IS_SOCKET (self), NULL);
  g_return_val_if_fail (size != NULL, NULL);

  if (*size == 0)
    return NULL;

  if ( (actual_size = evd_socket_read_len (self,
                                           buf,
                                           *size,
                                           error)) >= 0)
    {
      *size = actual_size;

      if (actual_size > 0)
        {
          data = g_new (gchar, actual_size + 1);
          g_memmove (data, buf, actual_size);
          data[actual_size] = '\0';
        }
    }
  else
    {
      (*size) = 0;
    }

  return data;
}

gssize
evd_socket_write_len (EvdSocket    *self,
                      const gchar  *buffer,
                      gsize         size,
                      GError      **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), 0);

  if (size == 0)
    return 0;

  g_return_val_if_fail (buffer != NULL, 0);

  if (TLS_ENABLED (self))
    return g_output_stream_write (TLS_OUTPUT_STREAM (self),
                                  buffer,
                                  size,
                                  NULL,
                                  error);
  else
    return evd_socket_write_filtered (self,
                                      buffer,
                                      size,
                                      error);
}

gssize
evd_socket_write (EvdSocket    *self,
                  const gchar  *buffer,
                  GError      **error)
{
  gsize size;

  if (buffer == NULL)
    return 0;

  size = strlen (buffer);

  return evd_socket_write_len (self, buffer, size, error);
}

gssize
evd_socket_unread_len (EvdSocket    *self,
                       const gchar  *buffer,
                       gsize         size,
                       GError      **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), -1);

  if (size == 0)
    return 0;

  g_return_val_if_fail (buffer != NULL, 0);

  if (! evd_socket_read_buffer_add_data (self,
                                         buffer,
                                         size,
                                         error))
    {
      return -1;
    }
  else
    {
      /* TODO: Should we invoke on-read handler here? */

      return size;
    }
}

gssize
evd_socket_unread (EvdSocket    *self,
                   const gchar  *buffer,
                   GError      **error)
{
  return evd_socket_unread_len (self, buffer, strlen (buffer), error);
}

gboolean
evd_socket_has_write_data_pending (EvdSocket *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  return (self->priv->write_buffer->len > 0);
}

gsize
evd_socket_get_max_readable (EvdSocket *self)
{
  gsize size = MAX_BLOCK_SIZE;
  gsize limited_size;

  g_return_val_if_fail (EVD_IS_SOCKET (self), 0);

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

  g_return_val_if_fail (EVD_IS_SOCKET (self), 0);

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
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  return
    (self->priv->status == EVD_SOCKET_STATE_CONNECTED) &&
    (self->priv->read_src_id == 0) &&
    ( (self->priv->cond & G_IO_IN) > 0 ||
      self->priv->read_buffer->len > 0);
}

gboolean
evd_socket_can_write (EvdSocket *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  return
    (self->priv->status == EVD_SOCKET_STATE_CONNECTED ||
     self->priv->status == EVD_SOCKET_STATE_BOUND) &&
    (self->priv->write_src_id == 0) &&
    ( (self->priv->cond & G_IO_OUT) > 0 ||
      self->priv->write_buffer->len > 0);
}

GSocketAddress *
evd_socket_get_remote_address (EvdSocket  *self,
                               GError    **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), NULL);

  if (self->priv->socket == NULL)
    return NULL;
  else
    return g_socket_get_remote_address (self->priv->socket, error);
}

GSocketAddress *
evd_socket_get_local_address (EvdSocket  *self,
                              GError    **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), NULL);

  if (self->priv->socket == NULL)
    return NULL;
  else
    return g_socket_get_local_address (self->priv->socket, error);
}

gboolean
evd_socket_starttls (EvdSocket *self, EvdTlsMode mode, GError **error)
{
  EvdTlsSession *session;

  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);
  g_return_val_if_fail (mode == EVD_TLS_MODE_CLIENT ||
                        mode == EVD_TLS_MODE_SERVER,
                        FALSE);

  if (self->priv->tls_enabled)
    {
      if (error != NULL)
        *error = g_error_new (evd_socket_err_domain,
                              EVD_SOCKET_ERROR_TLS_ACTIVE,
                              "SSL/TLS was already started");

      return FALSE;
    }

  self->priv->tls_enabled = TRUE;

  session = TLS_SESSION (self);

  g_object_set (session, "mode", mode, NULL);

  self->priv->tls_input_stream =
    evd_tls_input_stream_new (session,
                              G_INPUT_STREAM (self->priv->socket_input_stream));

  g_signal_connect (TLS_INPUT_STREAM (self),
                    "drained",
                    G_CALLBACK (evd_socket_input_stream_drained),
                    self);

  self->priv->tls_output_stream =
    evd_tls_output_stream_new (session,
                               G_OUTPUT_STREAM (self->priv->socket_output_stream));

  g_signal_connect (TLS_OUTPUT_STREAM (self),
                    "filled",
                    G_CALLBACK (evd_socket_output_stream_filled),
                    self);

  evd_socket_set_status (self, EVD_SOCKET_STATE_TLS_HANDSHAKING);

  return TRUE;
}

gboolean
evd_socket_shutdown (EvdSocket  *self,
                     gboolean    shutdown_read,
                     gboolean    shutdown_write,
                     GError    **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  if (! evd_socket_is_connected (self, error))
    return FALSE;

  if (shutdown_write && TLS_ENABLED (self))
    if (! evd_tls_session_shutdown_write (TLS_SESSION (self), error))
      return FALSE;

  return g_socket_shutdown (self->priv->socket,
                            shutdown_read,
                            shutdown_write,
                            error);
}

gboolean
evd_socket_get_tls_active (EvdSocket *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  return self->priv->tls_enabled;
}
