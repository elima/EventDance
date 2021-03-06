/*
 * evd-transport.c
 *
 * EventDance, Peer-to-peer IPC library <http://eventdance.org>
 *
 * Copyright (C) 2009-2012, Igalia S.L.
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

#include "evd-transport.h"

#include "evd-utils.h"
#include "evd-marshal.h"
#include "evd-peer-manager.h"

#define PEER_MSG_KEY     "org.eventdance.lib.transport.PEER_MESSAGE"
#define PEER_CLOSING_KEY "org.eventdance.lib.Transport.PEER_CLOSING"

/* signals */
enum
{
  SIGNAL_RECEIVE,
  SIGNAL_NEW_PEER,
  SIGNAL_PEER_CLOSED,
  SIGNAL_VALIDATE_PEER,
  SIGNAL_LAST
};

typedef struct
{
  const gchar *buffer;
  gchar *text_buffer;
  gsize size;
} EvdTransportPeerMessage;

static guint evd_transport_signals[SIGNAL_LAST] = { 0 };

static void     evd_transport_receive_internal         (EvdTransport *self,
                                                        EvdPeer      *peer,
                                                        const gchar  *buffer,
                                                        gsize         size);
static void     evd_transport_notify_receive           (EvdTransport *self,
                                                        EvdPeer      *peer);

static void     evd_transport_notify_new_peer          (EvdTransport *self,
                                                        EvdPeer      *peer);
static EvdPeer *evd_transport_create_new_peer_internal (EvdTransport *self);

static void     evd_transport_notify_peer_closed       (EvdTransport *self,
                                                        EvdPeer      *peer,
                                                        gboolean      gracefully);
static guint    evd_transport_notify_validate_peer     (EvdTransport *self,
                                                        EvdPeer      *peer);
static gboolean evd_transport_validate_peer_signal_acc (GSignalInvocationHint *hint,
                                                        GValue                *return_accu,
                                                        const GValue          *handler_return,
                                                        gpointer               data);

static void
evd_transport_base_init (gpointer g_class)
{
  static gboolean is_initialized = FALSE;

  EvdTransportInterface *iface = (EvdTransportInterface *) g_class;

  iface->receive = evd_transport_receive_internal;
  iface->notify_receive = evd_transport_notify_receive;
  iface->create_new_peer = evd_transport_create_new_peer_internal;
  iface->notify_new_peer = evd_transport_notify_new_peer;
  iface->notify_peer_closed = evd_transport_notify_peer_closed;
  iface->notify_validate_peer = evd_transport_notify_validate_peer;
  iface->open = NULL;

  if (! is_initialized)
    {
      evd_transport_signals[SIGNAL_RECEIVE] =
        g_signal_new ("receive",
                      G_TYPE_FROM_CLASS (g_class),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (EvdTransportInterface, signal_receive),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1,
                      EVD_TYPE_PEER);

      evd_transport_signals[SIGNAL_NEW_PEER] =
        g_signal_new ("new-peer",
                      G_TYPE_FROM_CLASS (g_class),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (EvdTransportInterface, signal_new_peer),
                      NULL, NULL,
                      g_cclosure_marshal_VOID__OBJECT,
                      G_TYPE_NONE, 1,
                      EVD_TYPE_PEER);

      evd_transport_signals[SIGNAL_PEER_CLOSED] =
        g_signal_new ("peer-closed",
                      G_TYPE_FROM_CLASS (g_class),
                      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (EvdTransportInterface, signal_peer_closed),
                      NULL, NULL,
                      evd_marshal_VOID__OBJECT_BOOLEAN,
                      G_TYPE_NONE, 2,
                      EVD_TYPE_PEER,
                      G_TYPE_BOOLEAN);

      evd_transport_signals[SIGNAL_VALIDATE_PEER] =
        g_signal_new ("validate-peer",
                      G_TYPE_FROM_CLASS (g_class),
                      G_SIGNAL_ACTION,
                      G_STRUCT_OFFSET (EvdTransportInterface, signal_validate_peer),
                      evd_transport_validate_peer_signal_acc, NULL,
                      evd_marshal_UINT__OBJECT,
                      G_TYPE_UINT, 1,
                      EVD_TYPE_PEER);

      is_initialized = TRUE;
    }

  iface->peer_manager = evd_peer_manager_get_default ();
}

static void
evd_transport_base_finalize (gpointer g_class)
{
  EvdTransportInterface *iface = (EvdTransportInterface *) g_class;

  g_object_unref (iface->peer_manager);
}

static gboolean
evd_transport_validate_peer_signal_acc (GSignalInvocationHint *hint,
                                        GValue                *return_accu,
                                        const GValue          *handler_return,
                                        gpointer               data)
{
  guint signal_result;
  guint global_result;

  global_result = g_value_get_uint (return_accu);
  signal_result = g_value_get_uint (handler_return);

  if (signal_result == EVD_VALIDATE_REJECT)
    global_result = EVD_VALIDATE_REJECT;
  else if (signal_result == EVD_VALIDATE_PENDING)
    global_result = EVD_VALIDATE_PENDING;
  else if (global_result != EVD_VALIDATE_PENDING)
    global_result = EVD_VALIDATE_ACCEPT;

  g_value_set_uint (return_accu, global_result);

  return (global_result != EVD_VALIDATE_REJECT);
}

GType
evd_transport_get_type (void)
{
  static GType iface_type = 0;

  if (iface_type == 0)
    {
      static const GTypeInfo info = {
        sizeof (EvdTransportInterface),
        evd_transport_base_init,
        evd_transport_base_finalize,
        NULL,
      };

      iface_type = g_type_register_static (G_TYPE_INTERFACE, "EvdTransport",
                                           &info, 0);
    }

  return iface_type;
}

static void
evd_transport_notify_receive (EvdTransport *self, EvdPeer *peer)
{
  g_signal_emit (evd_peer_get_transport (peer),
                 evd_transport_signals[SIGNAL_RECEIVE],
                 0,
                 peer,
                 NULL);
}

static void
evd_transport_receive_internal (EvdTransport *self,
                                EvdPeer      *peer,
                                const gchar  *buffer,
                                gsize         size)
{
  EvdTransportPeerMessage *msg;

  g_object_ref (peer);

  msg = g_object_get_data (G_OBJECT (peer), PEER_MSG_KEY);
  if (msg == NULL)
    {
      msg = g_new (EvdTransportPeerMessage, 1);
      g_object_set_data_full (G_OBJECT (peer),
                              PEER_MSG_KEY,
                              msg,
                              g_free);
    }

  msg->buffer = buffer;
  msg->size = size;
  msg->text_buffer = NULL;

  evd_transport_notify_receive (self, peer);

  if (msg->text_buffer != NULL)
    {
      g_slice_free1 (msg->size + 1, msg->text_buffer);
      msg->text_buffer = NULL;
    }
  msg->buffer = NULL;
  msg->size = 0;

  g_object_unref (peer);
}

static void
evd_transport_notify_new_peer (EvdTransport *self, EvdPeer *peer)
{
  g_signal_emit (evd_peer_get_transport (peer),
                 evd_transport_signals[SIGNAL_NEW_PEER],
                 0,
                 peer,
                 NULL);
}

static EvdPeer *
evd_transport_create_new_peer_internal (EvdTransport *self)
{
  EvdPeer *peer;
  guint validate_result;

  peer = g_object_new (EVD_TYPE_PEER,
                       "transport", self,
                       NULL);

  validate_result = evd_transport_notify_validate_peer (self, peer);
  if (validate_result == EVD_VALIDATE_REJECT)
    {
      g_object_unref (peer);
      return NULL;
    }
  else if (validate_result == EVD_VALIDATE_ACCEPT)
    {
      evd_transport_accept_peer (self, peer);
    }

  return peer;
}

static void
evd_transport_notify_peer_closed (EvdTransport *self,
                                  EvdPeer      *peer,
                                  gboolean      gracefully)
{
  g_signal_emit (evd_peer_get_transport (peer),
                 evd_transport_signals[SIGNAL_PEER_CLOSED],
                 0,
                 peer,
                 gracefully,
                 NULL);
}

static guint
evd_transport_notify_validate_peer (EvdTransport *self, EvdPeer *peer)
{
  guint result;

  g_signal_emit (self,
                 evd_transport_signals[SIGNAL_VALIDATE_PEER],
                 0,
                 peer,
                 &result);

  return result;
}

static gboolean
evd_transport_accept_peer_internal (EvdTransport *self, EvdPeer *peer)
{
  EvdPeerManager *peer_manager;

  peer_manager = EVD_TRANSPORT_GET_INTERFACE (self)->peer_manager;

  if (evd_peer_manager_lookup_peer (peer_manager,
                                    evd_peer_get_id (peer)) == NULL)
    {
      evd_peer_manager_add_peer (peer_manager, peer);
      /* By design, only EvdPeerManager should hold a reference to
         a peer, so after adding a peer we should drop our reference */
      g_object_unref (peer);

      evd_transport_notify_new_peer (self, peer);
    }

  return TRUE;
}

static gboolean
send_frame (EvdTransport    *self,
            EvdPeer         *peer,
            const gchar     *buffer,
            gsize            size,
            EvdMessageType   type,
            GError         **error)
{
  g_return_val_if_fail (EVD_IS_TRANSPORT (self), FALSE);
  g_return_val_if_fail (EVD_IS_PEER (peer), FALSE);

  if (! EVD_TRANSPORT_GET_INTERFACE (self)->send (self,
                                                  peer,
                                                  buffer,
                                                  size,
                                                  type,
                                                  NULL)
      && ! evd_peer_push_message (peer,
                                  buffer,
                                  size,
                                  type,
                                  error))
    {
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}

/* public methods */

gboolean
evd_transport_send (EvdTransport  *self,
                    EvdPeer       *peer,
                    const gchar   *buffer,
                    gsize          size,
                    GError       **error)
{
  return send_frame (self, peer, buffer, size, EVD_MESSAGE_TYPE_BINARY, error);
}

gboolean
evd_transport_send_text (EvdTransport  *self,
                         EvdPeer       *peer,
                         const gchar   *text,
                         GError       **error)
{
  gsize size;

  size = strlen (text);

  return send_frame (self, peer, text, size, EVD_MESSAGE_TYPE_TEXT, error);
}

/**
 * evd_transport_receive:
 * @size: (out):
 *
 * Returns: (transfer none):
 **/
const gchar *
evd_transport_receive (EvdTransport *self,
                       EvdPeer       *peer,
                       gsize         *size)
{
  EvdTransportPeerMessage *msg;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), NULL);

  msg = g_object_get_data (G_OBJECT (peer), PEER_MSG_KEY);
  if (msg == NULL)
    return NULL;

  if (size != NULL)
    *size = msg->size;

  return msg->buffer;
}

/**
 * evd_transport_receive_text:
 *
 * Returns: (transfer none):
 **/
const gchar *
evd_transport_receive_text (EvdTransport *self,
                            EvdPeer       *peer)
{
  EvdTransportPeerMessage *msg;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), NULL);

  msg = g_object_get_data (G_OBJECT (peer), PEER_MSG_KEY);
  if (msg == NULL)
    return NULL;

  if (msg->text_buffer == NULL)
    {
      msg->text_buffer = g_slice_alloc (msg->size + 1);
      msg->text_buffer[msg->size] = '\0';
      memcpy (msg->text_buffer, msg->buffer, msg->size);
    }

  return msg->text_buffer;
}

gboolean
evd_transport_peer_is_connected (EvdTransport *self,
                                 EvdPeer       *peer)
{
  g_return_val_if_fail (EVD_IS_TRANSPORT (self), FALSE);
  g_return_val_if_fail (EVD_IS_PEER (peer), FALSE);

  if (EVD_TRANSPORT_GET_INTERFACE (self)->peer_is_connected (self, peer))
    {
      evd_peer_touch (peer);
      return TRUE;
    }
  else
    {
      return FALSE;
    }
}

void
evd_transport_close_peer (EvdTransport  *self,
                          EvdPeer       *peer,
                          gboolean       gracefully,
                          GError       **error)
{
  EvdTransportInterface *iface;
  EvdPeerManager *peer_manager;
  gpointer bag;

  g_return_if_fail (EVD_IS_TRANSPORT (self));
  g_return_if_fail (EVD_IS_PEER (peer));

  bag = g_object_get_data (G_OBJECT (peer), PEER_CLOSING_KEY);
  if (bag != NULL)
    return;

  g_object_ref (self);
  g_object_set_data (G_OBJECT (peer), PEER_CLOSING_KEY, self);

  g_object_ref (peer);

  peer_manager = EVD_TRANSPORT_GET_INTERFACE (self)->peer_manager;
  evd_peer_manager_close_peer (peer_manager, peer, gracefully);

  evd_peer_close (peer, gracefully);

  iface = EVD_TRANSPORT_GET_INTERFACE (self);

  if (iface->peer_closed != NULL)
    iface->peer_closed (self, peer, gracefully);

  evd_transport_notify_peer_closed (self, peer, gracefully);

  g_object_set_data (G_OBJECT (peer), PEER_CLOSING_KEY, NULL);
  g_object_unref (self);

  g_object_unref (peer);
}

/**
 * evd_transport_create_new_peer:
 *
 * Returns: (transfer full):
 **/
EvdPeer *
evd_transport_create_new_peer (EvdTransport *self)
{
  EvdTransportInterface *iface;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), NULL);

  iface = EVD_TRANSPORT_GET_INTERFACE (self);

  g_assert (iface->create_new_peer != NULL);

  return iface->create_new_peer (self);
}

/**
 * evd_transport_lookup_peer:
 *
 * Returns: (transfer none):
 **/
EvdPeer *
evd_transport_lookup_peer (EvdTransport *self, const gchar *peer_id)
{
  EvdPeerManager *peer_manager;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), NULL);

  if (peer_id == NULL)
    return NULL;

  peer_manager = EVD_TRANSPORT_GET_INTERFACE (self)->peer_manager;

  return evd_peer_manager_lookup_peer (peer_manager, peer_id);
}

gboolean
evd_transport_accept_peer (EvdTransport *self, EvdPeer *peer)
{
  EvdTransportInterface *iface;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), FALSE);
  g_return_val_if_fail (EVD_IS_PEER (peer), FALSE);

  iface = EVD_TRANSPORT_GET_INTERFACE (self);
  if (iface->accept_peer != NULL)
    return iface->accept_peer (self, peer);
  else
    return evd_transport_accept_peer_internal (self, peer);
}

gboolean
evd_transport_reject_peer (EvdTransport *self, EvdPeer *peer)
{
  EvdTransportInterface *iface;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), FALSE);
  g_return_val_if_fail (EVD_IS_PEER (peer), FALSE);

  iface = EVD_TRANSPORT_GET_INTERFACE (self);
  if (iface->reject_peer != NULL)
    {
      return iface->reject_peer (self, peer);
    }
  else
    {
      g_object_unref (peer);
      return TRUE;
    }
}

/**
 * evd_transport_get_peer_manager:
 *
 * Returns: (transfer none):
 **/
EvdPeerManager *
evd_transport_get_peer_manager (EvdTransport *self)
{
  g_return_val_if_fail (EVD_IS_TRANSPORT (self), NULL);

  return EVD_TRANSPORT_GET_INTERFACE (self)->peer_manager;
}

/**
 * evd_transport_set_peer_manager:
 *
 **/
void
evd_transport_set_peer_manager (EvdTransport   *self,
                                EvdPeerManager *peer_manager)
{
  EvdTransportInterface *iface;

  g_return_if_fail (EVD_IS_TRANSPORT (self));
  g_return_if_fail (EVD_IS_PEER_MANAGER (peer_manager));

  iface = EVD_TRANSPORT_GET_INTERFACE (self);

  if (iface->peer_manager != peer_manager)
    {
      g_object_unref (iface->peer_manager);
      iface->peer_manager = peer_manager;
      g_object_ref (iface->peer_manager);
    }
}

/**
 * evd_transport_open:
 * @cancellable: (allow-none):
 * @callback: (scope async) (allow-none):
 * @user_data: (allow-none):
 *
 **/
void
evd_transport_open (EvdTransport        *self,
                    const gchar         *address,
                    GCancellable        *cancellable,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
  GSimpleAsyncResult *res;

  g_return_if_fail (EVD_IS_TRANSPORT (self));
  g_return_if_fail (address != NULL && address[0] != '\0');

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_transport_open);

  if (EVD_TRANSPORT_GET_INTERFACE (self)->open != NULL)
    {
      EVD_TRANSPORT_GET_INTERFACE (self)->open (self,
                                                address,
                                                res,
                                                cancellable);
    }
  else
    {
      g_simple_async_result_set_error (res,
                                       G_IO_ERROR,
                                       G_IO_ERROR_NOT_SUPPORTED,
                                       "Method open() not implemented in transport");
      g_simple_async_result_complete_in_idle (res);
      g_object_unref (res);
    }
}

gboolean
evd_transport_open_finish (EvdTransport  *self,
                           GAsyncResult  *result,
                           GError       **error)
{
  g_return_val_if_fail (EVD_IS_TRANSPORT (self), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                        G_OBJECT (self),
                                                        evd_transport_open),
                        FALSE);

  return ! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                                  error);
}
