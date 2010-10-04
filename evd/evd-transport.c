/*
 * evd-transport.c
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

#include <string.h>

#include "evd-transport.h"

#define PEER_MSG_KEY "org.eventdance.transport.PeerMessage"

/* signals */
enum
{
  SIGNAL_RECEIVE,
  SIGNAL_NEW_PEER,
  SIGNAL_LAST
};

typedef struct
{
  const gchar *buffer;
  gchar *text_buffer;
  gsize size;
} EvdTransportPeerMessage;

static guint evd_transport_signals[SIGNAL_LAST] = { 0 };

static void  evd_transport_receive_internal (EvdTransport *self,
                                             EvdPeer      *peer,
                                             const gchar  *buffer,
                                             gsize         size);
static void  evd_transport_notify_receive   (EvdTransport *self,
                                             EvdPeer      *peer);

static void
evd_transport_base_init (gpointer g_class)
{
  static gboolean is_initialized = FALSE;

  EvdTransportInterface *iface = (EvdTransportInterface *) g_class;

  iface->receive = evd_transport_receive_internal;
  iface->notify_receive = evd_transport_notify_receive;

  if (!is_initialized)
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

      is_initialized = TRUE;
    }
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
        NULL,
      };

      iface_type = g_type_register_static (G_TYPE_INTERFACE, "EvdTransport",
                                           &info, 0);
    }

  return iface_type;
}

static void
evd_transport_free_peer_msg (gpointer  data,
                             GObject  *where_the_object_was)
{
  EvdTransportPeerMessage *msg;

  msg = g_object_get_data (G_OBJECT (where_the_object_was), PEER_MSG_KEY);

  if (msg != NULL)
    {
      g_slice_free (EvdTransportPeerMessage, msg);

      g_object_set_data (G_OBJECT (where_the_object_was), PEER_MSG_KEY, NULL);
    }
}

static void
evd_transport_notify_receive (EvdTransport *self, EvdPeer *peer)
{
  g_signal_emit (self, evd_transport_signals[SIGNAL_RECEIVE], 0, peer, NULL);
}

static void
evd_transport_receive_internal (EvdTransport *self,
                                EvdPeer      *peer,
                                const gchar  *buffer,
                                gsize         size)
{
  EvdTransportPeerMessage *msg;

  msg = g_object_get_data (G_OBJECT (peer), PEER_MSG_KEY);
  if (msg == NULL)
    {
      msg = g_slice_new (EvdTransportPeerMessage);
      g_object_set_data (G_OBJECT (peer), PEER_MSG_KEY, msg);

      g_object_weak_ref (G_OBJECT (peer), evd_transport_free_peer_msg, NULL);
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
}

/* public methods */

gboolean
evd_transport_send (EvdTransport  *self,
                    EvdPeer       *peer,
                    const gchar   *buffer,
                    gsize          size,
                    GError       **error)
{
  g_return_val_if_fail (EVD_IS_TRANSPORT (self), FALSE);
  g_return_val_if_fail (EVD_IS_PEER (peer), FALSE);

  if (EVD_TRANSPORT_GET_INTERFACE (self)->send (self,
                                                peer,
                                                buffer,
                                                size,
                                                NULL) <= 0
      && ! evd_peer_backlog_push_frame (peer, buffer, size, error))
    {
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}

gboolean
evd_transport_send_text (EvdTransport  *self,
                         EvdPeer       *peer,
                         const gchar   *text,
                         GError       **error)
{
  gsize size;

  g_return_val_if_fail (EVD_IS_TRANSPORT (self), FALSE);
  g_return_val_if_fail (EVD_IS_PEER (peer), FALSE);

  size = strlen (text);

  return evd_transport_send (self, peer, text, size, error);
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

  return EVD_TRANSPORT_GET_INTERFACE (self)->peer_is_connected (self, peer);
}
