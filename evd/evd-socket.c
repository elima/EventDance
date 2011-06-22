/*
 * evd-socket.c
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

/**
 * SECTION:evd-socket
 * @short_description: EventDance's base socket class.
 *
 * #EvdSocket sockets are Berkeley-style sockets optmized for performance and scalability
 * under high-concurrency scenarios.
 *
 **/

#include "evd-socket.h"

#include "evd-utils.h"
#include "evd-marshal.h"
#include "evd-error.h"
#include "evd-poll.h"
#include "evd-resolver.h"
#include "evd-connection.h"

G_DEFINE_TYPE (EvdSocket, evd_socket, G_TYPE_OBJECT)

#define EVD_SOCKET_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                     EVD_TYPE_SOCKET, \
                                     EvdSocketPrivate))

#define SOCKET_ACTIVE(socket)       (socket->priv->status == EVD_SOCKET_STATE_CONNECTED || \
                                     (socket->priv->status == EVD_SOCKET_STATE_BOUND && \
                                      socket->priv->protocol == G_SOCKET_PROTOCOL_UDP))

/* private data */
struct _EvdSocketPrivate
{
  GSocket *socket;
  GSocketFamily family;
  GSocketType type;
  GSocketProtocol protocol;

  EvdSocketState status;
  EvdSocketState sub_status;

  GIOCondition cond;

  gint actual_priority;
  gint priority;

  gboolean bind_allow_reuse;

  EvdSocketNotifyConditionCallback notify_cond_cb;
  gpointer notify_cond_user_data;

  GType io_stream_type;
  GIOStream *io_stream;

  gboolean has_pending;
  GSimpleAsyncResult *async_result;

  EvdPoll *poll;
  EvdPollSession *poll_session;
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
  PROP_PRIORITY,
  PROP_STATUS,
  PROP_IO_STREAM_TYPE
};

static void       evd_socket_class_init                 (EvdSocketClass *class);
static void       evd_socket_init                       (EvdSocket *self);

static void       evd_socket_finalize                   (GObject *obj);
static void       evd_socket_dispose                    (GObject *obj);

static void       evd_socket_set_property               (GObject      *obj,
                                                         guint         prop_id,
                                                         const GValue *value,
                                                         GParamSpec   *pspec);
static void       evd_socket_get_property               (GObject    *obj,
                                                         guint       prop_id,
                                                         GValue     *value,
                                                         GParamSpec *pspec);

static gboolean   evd_socket_cleanup                    (EvdSocket  *self,
                                                         GError    **error);
static gboolean   evd_socket_cleanup_internal           (EvdSocket  *self,
                                                         GError    **error);

static void       evd_socket_set_status                 (EvdSocket      *self,
                                                         EvdSocketState  status);

static void       evd_socket_copy_properties            (EvdSocket *self,
                                                         EvdSocket *target);

static void       evd_socket_deliver_async_result_error (EvdSocket           *self,
                                                         GSimpleAsyncResult  *res,
                                                         GError              *error,
                                                         GAsyncReadyCallback  callback,
                                                         gpointer             user_data,
                                                         gboolean             in_idle);

static gboolean   evd_socket_check_availability         (EvdSocket  *self,
                                                         GError    **error);

static void       evd_socket_throw_error                (EvdSocket *self,
                                                         GError    *error);

static void       evd_socket_handle_condition           (EvdSocket    *self,
                                                         GIOCondition  condition);

static EvdSocket *evd_socket_accept                     (EvdSocket  *self,
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

  class->handle_condition = NULL;
  class->cleanup = evd_socket_cleanup_internal;

  /* install signals */
  evd_socket_signals[SIGNAL_ERROR] =
    g_signal_new ("error",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdSocketClass, error),
                  NULL, NULL,
                  evd_marshal_VOID__UINT_INT_STRING,
                  G_TYPE_NONE, 3,
                  G_TYPE_UINT,
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
                  G_TYPE_IO_STREAM);

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

  g_object_class_install_property (obj_class, PROP_IO_STREAM_TYPE,
                                   g_param_spec_gtype ("io-stream-type",
                                                       "The IO stream GType",
                                                       "The GType of the IO stream returned by socket when connected",
                                                       EVD_TYPE_CONNECTION,
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

  priv->socket   = NULL;
  priv->family   = G_SOCKET_FAMILY_INVALID;
  priv->type     = G_SOCKET_TYPE_INVALID;
  priv->protocol = G_SOCKET_PROTOCOL_UNKNOWN;

  priv->status     = EVD_SOCKET_STATE_CLOSED;
  priv->sub_status = EVD_SOCKET_STATE_CLOSED;

  priv->cond = 0;

  priv->priority        = G_PRIORITY_DEFAULT;
  priv->actual_priority = G_PRIORITY_DEFAULT;

  priv->notify_cond_cb = NULL;
  priv->notify_cond_user_data = NULL;

  priv->io_stream_type = EVD_TYPE_CONNECTION;

  priv->has_pending = FALSE;
  priv->async_result = NULL;

  priv->poll = evd_poll_get_default ();
  priv->poll_session = NULL;
}

static void
evd_socket_dispose (GObject *obj)
{
  EvdSocket *self = EVD_SOCKET (obj);

  self->priv->status = EVD_SOCKET_STATE_CLOSED;

  evd_socket_cleanup (self, NULL);

  G_OBJECT_CLASS (evd_socket_parent_class)->dispose (obj);
}

static void
evd_socket_finalize (GObject *obj)
{
  EvdSocket *self = EVD_SOCKET (obj);

  g_object_unref (self->priv->poll);

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

    case PROP_PRIORITY:
      evd_socket_set_priority (self, g_value_get_uint (value));
      break;

    case PROP_IO_STREAM_TYPE:
      self->priv->io_stream_type = g_value_get_gtype (value);
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

    case PROP_PRIORITY:
      g_value_set_int (value, self->priv->priority);
      break;

    case PROP_STATUS:
      g_value_set_uint (value, self->priv->status);
      break;

    case PROP_IO_STREAM_TYPE:
      g_value_set_gtype (value, self->priv->io_stream_type);
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

  g_object_set (socket,
                "blocking", FALSE,
                "keepalive", TRUE,
                NULL);
}

static gboolean
evd_socket_setup (EvdSocket *self, GError **error)
{
  if (self->priv->socket == NULL)
    {
      GSocket *socket;

      if ( (socket = g_socket_new (self->priv->family,
                                   self->priv->type,
                                   self->priv->protocol,
                                   error)) == NULL)
        {
          return FALSE;
        }
      else
        {
          evd_socket_set_socket (self, socket);
        }
    }

  return TRUE;
}

static GIOCondition
evd_socket_on_condition (EvdPoll      *poll,
                         GIOCondition  cond,
                         gpointer      user_data)
{
  EvdSocket *self = EVD_SOCKET (user_data);

  evd_socket_handle_condition (self, cond);

  return cond;
}

gboolean
static evd_socket_watch (EvdSocket *self, GIOCondition cond, GError **error)
{
  self->priv->cond = 0;

  if (self->priv->poll_session == NULL)
    {
      self->priv->poll_session =
        evd_poll_add (self->priv->poll,
                      g_socket_get_fd (self->priv->socket),
                      cond,
                      self->priv->actual_priority,
                      evd_socket_on_condition,
                      self,
                      error);

      return (self->priv->poll_session != NULL);
    }
  else
    {
      return evd_poll_mod (self->priv->poll,
                           self->priv->poll_session,
                           cond,
                           self->priv->actual_priority,
                           error);
    }
}

static gboolean
evd_socket_unwatch (EvdSocket *self, GError **error)
{
  if (self->priv->poll_session == NULL
      || evd_poll_del (self->priv->poll,
                          self->priv->poll_session,
                          error))
    {
      return TRUE;
    }
  else
    {
      return FALSE;
    }
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
        *error = g_error_new (G_IO_ERROR,
                              G_IO_ERROR_INVALID_ARGUMENT,
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
      g_set_error_literal (error,
                           EVD_ERROR,
                           EVD_ERROR_NOT_CONNECTED,
                           "Socket is not connected");

      return FALSE;
    }

  return TRUE;
}

static gboolean
evd_socket_cleanup (EvdSocket *self, GError **error)
{
  EvdSocketClass *class = EVD_SOCKET_GET_CLASS (self);

  if (class->cleanup != NULL)
    return class->cleanup (self, error);
  else
    return evd_socket_cleanup (self, error);
}

static void
evd_socket_deliver_async_result_error (EvdSocket           *self,
                                       GSimpleAsyncResult  *res,
                                       GError              *error,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data,
                                       gboolean             in_idle)
{
  if (res == NULL)
    {
      res = g_simple_async_result_new_from_error (G_OBJECT (self),
                                                  callback,
                                                  user_data,
                                                  error);
    }
  else
    {
      g_simple_async_result_set_from_error (res, error);
    }

  if (in_idle)
    g_simple_async_result_complete_in_idle (res);
  else
    g_simple_async_result_complete (res);

  g_object_unref (res);
}

static gboolean
evd_socket_bind_addr_internal (EvdSocket       *self,
                               GSocketAddress  *address,
                               gboolean         allow_reuse,
                               GError         **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);
  g_return_val_if_fail (G_IS_SOCKET_ADDRESS (address), FALSE);

  if (! evd_socket_check_address (self, address, error))
    return FALSE;

  if (! evd_socket_setup (self, error))
    return FALSE;

  if (! g_socket_bind (self->priv->socket,
                       address,
                       allow_reuse,
                       error))
    {
      evd_socket_cleanup (self, NULL);
      return FALSE;
    }
  else
    {
      evd_socket_set_status (self, EVD_SOCKET_STATE_BOUND);
    }

  return TRUE;
}

static gboolean
evd_socket_listen_addr_internal (EvdSocket *self, GSocketAddress *address, GError **error)
{
  if (address != NULL && ! evd_socket_bind_addr_internal (self, address, TRUE, error))
    return FALSE;

  if (self->priv->status != EVD_SOCKET_STATE_BOUND)
    {
      /* this is to consider that socket could have been closed
         during 'state-changed' signal handler after call to bind */

      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_CLOSED,
                           "Socket has been closed");

      return FALSE;
    }

  g_socket_set_listen_backlog (self->priv->socket, 10000); /* TODO: change by a max-conn prop */
  if (g_socket_listen (self->priv->socket, error))
    {
      if (evd_socket_watch (self, G_IO_IN, error))
        {
          self->priv->cond = 0;
          self->priv->actual_priority = G_PRIORITY_HIGH + 1;
          evd_socket_set_status (self, EVD_SOCKET_STATE_LISTENING);
        }
      else
        {
          evd_socket_cleanup (self, NULL);

          return FALSE;
        }
    }

  return TRUE;
}

static gboolean
evd_socket_connect_addr_internal (EvdSocket        *self,
                                  GSocketAddress   *address,
                                  GError          **error)
{
  GError *_error = NULL;

  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);
  g_return_val_if_fail (G_IS_SOCKET_ADDRESS (address), FALSE);

  if (! evd_socket_check_address (self, address, error))
    return FALSE;

  if (! evd_socket_setup (self, error))
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
      return FALSE;
    }
  else
    {
      self->priv->actual_priority = G_PRIORITY_HIGH + 2;
      evd_socket_set_status (self, EVD_SOCKET_STATE_CONNECTING);

      return TRUE;
    }
}

static void
evd_socket_on_address_resolved (GObject      *obj,
                                GAsyncResult *res,
                                gpointer      user_data)
{
  EvdSocket *self = EVD_SOCKET (user_data);
  EvdResolver *resolver = EVD_RESOLVER (obj);
  GList *addresses;
  GError *error = NULL;

  if (self->priv->status != EVD_SOCKET_STATE_RESOLVING)
    goto free_mem;

  if ( (addresses = evd_resolver_resolve_finish (EVD_RESOLVER (obj),
                                                 res,
                                                 &error)) != NULL)
    {
      GSocketAddress *socket_address;
      GList *node = addresses;
      gboolean match = FALSE;

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

          sub_status = self->priv->sub_status;
          self->priv->sub_status = EVD_SOCKET_STATE_CLOSED;

          switch (sub_status)
            {
            case EVD_SOCKET_STATE_LISTENING:
              {
                if (evd_socket_listen_addr_internal (self, socket_address, &error))
                  {
                    if (self->priv->async_result != NULL)
                      {
                        self->priv->has_pending = FALSE;

                        g_simple_async_result_complete_in_idle (self->priv->async_result);
                        g_object_unref (self->priv->async_result);
                        self->priv->async_result = NULL;
                      }
                  }
                break;
              }
            case EVD_SOCKET_STATE_BOUND:
              {
                if (evd_socket_bind_addr_internal (self,
                                                   socket_address,
                                                   self->priv->bind_allow_reuse,
                                                   &error))
                  {
                    if (self->priv->async_result != NULL)
                      {
                        self->priv->has_pending = FALSE;

                        g_simple_async_result_complete_in_idle (self->priv->async_result);
                        g_object_unref (self->priv->async_result);
                        self->priv->async_result = NULL;
                      }
                  }
                break;
              }
            case EVD_SOCKET_STATE_CONNECTING:
              {
                evd_socket_connect_addr_internal (self, socket_address, &error);
                break;
              }
            default:
              {
              }
            }
        }
      else
        {
          error = g_error_new (G_IO_ERROR,
                               G_IO_ERROR_INVALID_ARGUMENT,
                               "None of the resolved addresses match socket family");
        }

      evd_resolver_free_addresses (addresses);
    }

  if (error != NULL)
    {
      if (self->priv->async_result != NULL)
        {
          evd_socket_deliver_async_result_error (self,
                                                 self->priv->async_result,
                                                 error,
                                                 NULL,
                                                 NULL,
                                                 TRUE);
          self->priv->async_result = NULL;
        }

      evd_socket_throw_error (self, error);
      evd_socket_close (self, NULL);
    }

 free_mem:
  g_object_unref (self);
  g_object_unref (resolver);
}

static void
evd_socket_resolve_address (EvdSocket       *self,
                            const gchar     *address,
                            EvdSocketState   action,
                            GCancellable    *cancellable)
{
  EvdResolver *resolver;
  self->priv->sub_status = action;

  resolver = evd_resolver_get_default ();

  g_object_ref (self);
  evd_resolver_resolve_async (resolver,
                              address,
                              cancellable,
                              evd_socket_on_address_resolved,
                              self);
}

static void
evd_socket_copy_properties (EvdSocket *self, EvdSocket *target)
{
  evd_socket_set_priority (target, self->priv->priority);

  target->priv->io_stream_type = self->priv->io_stream_type;
}

static gboolean
evd_socket_check_availability (EvdSocket  *self,
                               GError    **error)
{
  if (self->priv->has_pending)
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_PENDING,
                           "Socket has outstanding operation");
      return FALSE;
    }

  if (SOCKET_ACTIVE (self))
    {
      g_set_error_literal (error,
                           G_IO_ERROR,
                           G_IO_ERROR_BUSY,
                           "Socket is currently active, should be closed first before requesting new operation");
      return FALSE;
    }

  return TRUE;
}

static void
evd_socket_set_status (EvdSocket *self, EvdSocketState status)
{
  EvdSocketState old_status;

  if (status == self->priv->status)
    return;

  old_status = self->priv->status;
  self->priv->status = status;

  g_signal_emit (self,
                 evd_socket_signals[SIGNAL_STATE_CHANGED],
                 0,
                 status,
                 old_status,
                 NULL);
}

static void
evd_socket_throw_error (EvdSocket *self, GError *error)
{
  g_object_ref (self);
  g_signal_emit (self,
                 evd_socket_signals[SIGNAL_ERROR],
                 0,
                 error->domain,
                 error->code,
                 error->message,
                 NULL);
  g_object_unref (self);

  g_error_free (error);
}

static void
evd_socket_handle_condition (EvdSocket *self, GIOCondition condition)
{
  GError *error = NULL;

  if (condition == self->priv->cond)
    return;

  self->priv->cond = condition;

  g_object_ref (self);

  if (self->priv->status == EVD_SOCKET_STATE_LISTENING)
    {
      EvdSocket *client;
      GIOStream *conn;

      self->priv->cond &= ~G_IO_IN;

      while ( (self->priv->status == EVD_SOCKET_STATE_LISTENING) &&
              ((client = evd_socket_accept (self, &error)) != NULL) )
        {
          evd_socket_copy_properties (self, client);

          conn = g_object_new (self->priv->io_stream_type,
                               "socket", client,
                               NULL);

          /* fire 'new-connection' signal */
          g_signal_emit (self,
                         evd_socket_signals[SIGNAL_NEW_CONNECTION],
                         0,
                         G_IO_STREAM (conn),
                         NULL);

          g_object_unref (conn);
          g_object_unref (client);
        }

      if (error != NULL)
        {
          if (error->code != G_IO_ERROR_WOULD_BLOCK)
            {
              /* error accepting connection, emit 'error' signal */
              error->code = EVD_ERROR_SOCKET_ACCEPT;
              evd_socket_throw_error (self, error);

              /* @TODO: even on error, we should continue
                 accepting new connection until EAGAIN */
            }
          else
            {
              g_error_free (error);
            }
        }
    }
  else
    {
      if (condition & G_IO_ERR)
        {
          if (self->priv->status == EVD_SOCKET_STATE_CONNECTING)
            {
              /* assume connection was refused */
              error = g_error_new (G_IO_ERROR,
                                   G_IO_ERROR_CONNECTION_REFUSED,
                                   "Connection refused");
              evd_socket_deliver_async_result_error (self,
                                                     self->priv->async_result,
                                                     error,
                                                     NULL,
                                                     NULL,
                                                     TRUE);
              self->priv->async_result = NULL;
            }
          else
            {
              error = g_error_new (EVD_ERROR,
                                   EVD_ERROR_UNKNOWN,
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
              if (self->priv->status == EVD_SOCKET_STATE_CONNECTING)
                {
                  /* socket has just connected! */
                  self->priv->has_pending = FALSE;

                  /* restore priority */
                  self->priv->actual_priority = self->priv->priority;

                  evd_socket_set_status (self, EVD_SOCKET_STATE_CONNECTED);

                  if (self->priv->async_result != NULL)
                    {
                      GSimpleAsyncResult *res;

                      res = self->priv->async_result;
                      self->priv->async_result = NULL;

                      g_simple_async_result_complete_in_idle (res);
                      g_object_unref (res);
                    }
                }
            }
        }
    }

  if (SOCKET_ACTIVE (self) && self->priv->notify_cond_cb != NULL)
    self->priv->notify_cond_cb (self,
                                condition,
                                self->priv->notify_cond_user_data);

  g_object_unref (self);
}

static gboolean
evd_socket_cleanup_internal (EvdSocket *self, GError **error)
{
  gboolean result = TRUE;

  self->priv->family = G_SOCKET_FAMILY_INVALID;

  self->priv->cond = 0;

  if (self->priv->socket != NULL)
    {
      if (! evd_socket_unwatch (self, error) ||
          (! g_socket_is_closed (self->priv->socket) &&
           ! g_socket_close (self->priv->socket,
                             error != NULL && *error == NULL ? error : NULL)))
        {
          result = FALSE;
        }

      g_object_unref (self->priv->socket);
      self->priv->socket = NULL;
    }

  if (self->priv->poll_session != NULL)
    self->priv->poll_session = NULL;

  self->priv->has_pending = FALSE;

  return result;
}

static EvdSocket *
evd_socket_accept (EvdSocket *self, GError **error)
{
  EvdSocket *client = NULL;
  GSocket *client_socket;

  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  if ( (client_socket = g_socket_accept (self->priv->socket, NULL, error)) != NULL)
    {
      client = EVD_SOCKET (g_object_new (G_OBJECT_TYPE (self), NULL, NULL));
      evd_socket_set_socket (client, client_socket);

      if (evd_socket_watch (client, G_IO_IN | G_IO_OUT, error))
        {
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
  gboolean result = TRUE;

  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  if (self->priv->async_result != NULL)
    {
      GSimpleAsyncResult *res;

      res = self->priv->async_result;
      self->priv->async_result = NULL;

      g_simple_async_result_set_error (res,
                                       G_IO_ERROR,
                                       G_IO_ERROR_CLOSED,
                                       "Socket has been closed");
      g_simple_async_result_complete (res);
      g_object_unref (res);
    }

  if (self->priv->status != EVD_SOCKET_STATE_CLOSED &&
      self->priv->status != EVD_SOCKET_STATE_CLOSING)
    {
      if (! evd_socket_cleanup (self, error))
        result = FALSE;

      g_object_ref (self);
      evd_socket_set_status (self, EVD_SOCKET_STATE_CLOSED);
      g_signal_emit (self, evd_socket_signals[SIGNAL_CLOSE], 0, NULL);
      g_object_unref (self);
    }

  return result;
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
evd_socket_shutdown (EvdSocket  *self,
                     gboolean    shutdown_read,
                     gboolean    shutdown_write,
                     GError    **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  if (! evd_socket_is_connected (self, error))
    return FALSE;

  return g_socket_shutdown (self->priv->socket,
                            shutdown_read,
                            shutdown_write,
                            error);
}

gboolean
evd_socket_watch_condition (EvdSocket *self, GIOCondition cond, GError **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  if (SOCKET_ACTIVE (self))
    {
      self->priv->cond = ~cond;

      return TRUE;
    }
  else
    {
      g_set_error_literal (error,
                           EVD_ERROR,
                           EVD_ERROR_NOT_CONNECTED,
                           "Socket is not active");

      return FALSE;
    }
}

GIOCondition
evd_socket_get_condition (EvdSocket *self)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), 0);

  return self->priv->cond;
}

/**
 * evd_socket_set_notify_condition_callback:
 * @callback: (allow-none):
 * @user_data: (allow-none):
 *
 **/
void
evd_socket_set_notify_condition_callback (EvdSocket                        *self,
                                          EvdSocketNotifyConditionCallback  callback,
                                          gpointer                          user_data)
{
  g_return_if_fail (EVD_IS_SOCKET (self));

  self->priv->notify_cond_cb = callback;
  self->priv->notify_cond_user_data = user_data;
}

void
evd_socket_connect_to (EvdSocket           *self,
                       const gchar         *address,
                       GCancellable        *cancellable,
                       GAsyncReadyCallback  callback,
                       gpointer             user_data)
{
  GError *error = NULL;

  g_return_if_fail (EVD_IS_SOCKET (self));
  g_return_if_fail (address != NULL);

  self->priv->async_result =
    g_simple_async_result_new (G_OBJECT (self),
                               callback,
                               user_data,
                               evd_socket_connect_addr);

  if (! evd_socket_check_availability (self, &error))
    {
      g_simple_async_result_set_from_error (self->priv->async_result, error);
      g_error_free (error);

      g_simple_async_result_complete_in_idle (self->priv->async_result);
      g_object_unref (self->priv->async_result);
      self->priv->async_result = NULL;

      return;
    }

  self->priv->has_pending = TRUE;

  evd_socket_set_status (self, EVD_SOCKET_STATE_RESOLVING);

  evd_socket_resolve_address (self,
                              address,
                              EVD_SOCKET_STATE_CONNECTING,
                              cancellable);
}

void
evd_socket_connect_addr (EvdSocket           *self,
                         GSocketAddress      *address,
                         GCancellable        *cancellable,
                         GAsyncReadyCallback  callback,
                         gpointer             user_data)
{
  GError *error = NULL;

  g_return_if_fail (EVD_IS_SOCKET (self));

  if (! evd_socket_check_availability (self, &error) ||
      ! evd_socket_connect_addr_internal (self, address, &error))
    {
      evd_socket_deliver_async_result_error (self,
                                             NULL,
                                             error,
                                             callback,
                                             user_data,
                                             TRUE);
      return;
    }
  else
    {
      self->priv->has_pending = TRUE;
      self->priv->async_result = g_simple_async_result_new (G_OBJECT (self),
                                                            callback,
                                                            user_data,
                                                            evd_socket_connect_addr);
    }

  return;
}

/**
 * evd_socket_connect_finish:
 * @result: (type Gio.AsyncResult):
 *
 * Returns: (transfer full) (type Gio.IOStream):
 **/
GIOStream *
evd_socket_connect_finish (EvdSocket     *self,
                           GAsyncResult  *result,
                           GError       **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), NULL);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                        G_OBJECT (self),
                                                        evd_socket_connect_addr),
                        NULL);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
    return g_object_new (self->priv->io_stream_type, "socket", self, NULL);
  else
    return NULL;
}

gboolean
evd_socket_listen_addr (EvdSocket *self, GSocketAddress *address, GError **error)
{
  gboolean result;

  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);

  if (! evd_socket_check_availability (self, error))
    return FALSE;
  else
    self->priv->has_pending = TRUE;

  result = evd_socket_listen_addr_internal (self, address, error);
  self->priv->has_pending = FALSE;

  return result;
}

void
evd_socket_listen (EvdSocket           *self,
                   const gchar         *address,
                   GCancellable        *cancellable,
                   GAsyncReadyCallback  callback,
                   gpointer             user_data)
{
  GError *error = NULL;

  g_return_if_fail (EVD_IS_SOCKET (self));

  self->priv->async_result = g_simple_async_result_new (G_OBJECT (self),
                                                        callback,
                                                        user_data,
                                                        evd_socket_listen);

  if (! evd_socket_check_availability (self, &error))
    {
      g_simple_async_result_set_from_error (self->priv->async_result, error);
      g_error_free (error);

      g_simple_async_result_complete_in_idle (self->priv->async_result);
      g_object_unref (self->priv->async_result);
      self->priv->async_result = NULL;

      return;
    }

  self->priv->has_pending = TRUE;

  evd_socket_set_status (self, EVD_SOCKET_STATE_RESOLVING);

  evd_socket_resolve_address (self,
                              address,
                              EVD_SOCKET_STATE_LISTENING,
                              cancellable);
}

gboolean
evd_socket_listen_finish (EvdSocket     *self,
                          GAsyncResult  *result,
                          GError       **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                        G_OBJECT (self),
                                                        evd_socket_listen),
                        FALSE);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
    return TRUE;
  else
    return FALSE;
}

gboolean
evd_socket_bind_addr (EvdSocket       *self,
                      GSocketAddress  *address,
                      gboolean         allow_reuse,
                      GError         **error)
{
  gboolean result;

  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);
  g_return_val_if_fail (G_IS_SOCKET_ADDRESS (address), FALSE);

  if (! evd_socket_check_availability (self, error))
    return FALSE;
  else
    self->priv->has_pending = TRUE;

  result = evd_socket_bind_addr_internal (self, address, allow_reuse, error);
  self->priv->has_pending = FALSE;

  return result;
}

void
evd_socket_bind (EvdSocket           *self,
                 const gchar         *address,
                 GCancellable        *cancellable,
                 GAsyncReadyCallback  callback,
                 gpointer             user_data)
{
  GError *error = NULL;

  g_return_if_fail (EVD_IS_SOCKET (self));

  self->priv->async_result = g_simple_async_result_new (G_OBJECT (self),
                                                        callback,
                                                        user_data,
                                                        evd_socket_bind);

  if (! evd_socket_check_availability (self, &error))
    {
      g_simple_async_result_set_from_error (self->priv->async_result, error);
      g_error_free (error);

      g_simple_async_result_complete_in_idle (self->priv->async_result);
      g_object_unref (self->priv->async_result);
      self->priv->async_result = NULL;

      return;
    }

  self->priv->has_pending = TRUE;

  evd_socket_set_status (self, EVD_SOCKET_STATE_RESOLVING);

  evd_socket_resolve_address (self,
                              address,
                              EVD_SOCKET_STATE_BOUND,
                              cancellable);
}

gboolean
evd_socket_bind_finish (EvdSocket     *self,
                        GAsyncResult  *result,
                        GError       **error)
{
  g_return_val_if_fail (EVD_IS_SOCKET (self), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                        G_OBJECT (self),
                                                        evd_socket_bind),
                        FALSE);

  if (! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                               error))
    return TRUE;
  else
    return FALSE;
}
