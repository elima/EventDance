/*
 * evd-dbus-bridge.c
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

#include <json-glib/json-glib.h>

#include "evd-dbus-bridge.h"

#include "evd-utils.h"
#include "evd-dbus-agent.h"

G_DEFINE_TYPE (EvdDBusBridge, evd_dbus_bridge, EVD_TYPE_IPC_MECHANISM)

#define EVD_DBUS_BRIDGE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                          EVD_TYPE_DBUS_BRIDGE, \
                                          EvdDBusBridgePrivate))

enum EvdDBusBridgeCmd
{
  EVD_DBUS_BRIDGE_CMD_NONE,

  EVD_DBUS_BRIDGE_CMD_ERROR,
  EVD_DBUS_BRIDGE_CMD_REPLY,
  EVD_DBUS_BRIDGE_CMD_NEW_CONNECTION,
  EVD_DBUS_BRIDGE_CMD_CLOSE_CONNECTION,
  EVD_DBUS_BRIDGE_CMD_OWN_NAME,
  EVD_DBUS_BRIDGE_CMD_UNOWN_NAME,
  EVD_DBUS_BRIDGE_CMD_NAME_ACQUIRED,
  EVD_DBUS_BRIDGE_CMD_NAME_LOST,
  EVD_DBUS_BRIDGE_CMD_REGISTER_OBJECT,
  EVD_DBUS_BRIDGE_CMD_UNREGISTER_OBJECT,
  EVD_DBUS_BRIDGE_CMD_NEW_PROXY,
  EVD_DBUS_BRIDGE_CMD_CLOSE_PROXY,
  EVD_DBUS_BRIDGE_CMD_CALL_METHOD,
  EVD_DBUS_BRIDGE_CMD_CALL_METHOD_RETURN,
  EVD_DBUS_BRIDGE_CMD_EMIT_SIGNAL,

  EVD_DBUS_BRIDGE_CMD_PAD0,
  EVD_DBUS_BRIDGE_CMD_PAD1,
  EVD_DBUS_BRIDGE_CMD_PAD2,
  EVD_DBUS_BRIDGE_CMD_PAD3,
  EVD_DBUS_BRIDGE_CMD_PAD4,

  EVD_DBUS_BRIDGE_CMD_LAST
};

enum EvdDBusBridgeErr
{
  EVD_DBUS_BRIDGE_ERR_FAILED,
  EVD_DBUS_BRIDGE_ERR_INVALID_MSG,
  EVD_DBUS_BRIDGE_ERR_UNKNOW_COMMAND,
  EVD_DBUS_BRIDGE_ERR_INVALID_SUBJECT,
  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
  EVD_DBUS_BRIDGE_ERR_CONNECTION_FAILED,
  EVD_DBUS_BRIDGE_ERR_ALREADY_REGISTERED,
  EVD_DBUS_BRIDGE_ERR_PROXY_FAILED,
  EVD_DBUS_BRIDGE_ERR_UNKNOWN_METHOD,

  EVD_DBUS_BRIDGE_ERR_PAD0,
  EVD_DBUS_BRIDGE_ERR_PAD1,
  EVD_DBUS_BRIDGE_ERR_PAD2,
  EVD_DBUS_BRIDGE_ERR_PAD3,
  EVD_DBUS_BRIDGE_ERR_PAD4,

  EVD_DBUS_BRIDGE_ERR_LAST
};

/* private data */
struct _EvdDBusBridgePrivate
{
  EvdDBusAgentVTable agent_vtable;

#ifdef ENABLE_TESTS
  EvdDBusBridgeSendMsgCb send_msg_callback;
  gpointer send_msg_user_data;
#endif
};

typedef struct
{
  EvdDBusBridge *bridge;
  GObject *obj;
  guint8 cmd;
  guint64 serial;
  guint32 conn_id;
  gint32 subject;
  gchar *args;
  gint err_code;
} MsgClosure;

static void     evd_dbus_bridge_class_init             (EvdDBusBridgeClass *class);
static void     evd_dbus_bridge_init                   (EvdDBusBridge *self);

static void     evd_dbus_bridge_finalize               (GObject *obj);
static void     evd_dbus_bridge_dispose                (GObject *obj);

static void     evd_dbus_bridge_send                   (EvdDBusBridge *self,
                                                        GObject       *obj,
                                                        guint8         cmd,
                                                        guint64        serial,
                                                        guint32        conn_id,
                                                        guint32        subject,
                                                        const gchar   *args);

static void     evd_dbus_bridge_on_proxy_signal        (GObject     *obj,
                                                        guint        conn_id,
                                                        guint        proxy_id,
                                                        const gchar *signal_name,
                                                        GVariant    *parameters,
                                                        gpointer     user_data);
static void     evd_dbus_bridge_on_proxy_props_changed (GObject     *obj,
                                                        guint        conn_id,
                                                        guint        proxy_uuid,
                                                        GVariant    *changed_properties,
                                                        GStrv       *invalidated_properties,
                                                        gpointer     user_data);

static void     evd_dbus_bridge_on_name_acquired       (GObject *object,
                                                        guint    conn_id,
                                                        guint    owner_id,
                                                        gpointer user_data);
static void     evd_dbus_bridge_on_name_lost           (GObject *object,
                                                        guint    conn_id,
                                                        guint    owner_id,
                                                        gpointer user_data);

static void     evd_dbus_bridge_on_reg_obj_call_method (GObject     *object,
                                                        guint        conn_id,
                                                        const gchar *sender,
                                                        const gchar *method_name,
                                                        guint        registration_id,
                                                        GVariant    *parameters,
                                                        guint64      serial,
                                                        gpointer     user_data);

static void     transport_on_new_peer                  (EvdIpcMechanism *ipc_mechanism,
                                                        EvdTransport    *transport,
                                                        EvdPeer         *peer);
static void     transport_on_receive                   (EvdIpcMechanism *self,
                                                        EvdTransport    *transport,
                                                        EvdPeer         *peer,
                                                        const guchar    *data,
                                                        gsize            size);

#ifndef ENABLE_TESTS
void            evd_dbus_bridge_process_msg            (EvdDBusBridge *self,
                                                        GObject       *object,
                                                        const gchar   *msg,
                                                        gsize          length);
#endif /* ENABLE_TESTS */

static void
evd_dbus_bridge_class_init (EvdDBusBridgeClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdIpcMechanismClass *ipc_mechanism_class = EVD_IPC_MECHANISM_CLASS (class);

  obj_class->dispose = evd_dbus_bridge_dispose;
  obj_class->finalize = evd_dbus_bridge_finalize;

  ipc_mechanism_class->transport_receive = transport_on_receive;
  ipc_mechanism_class->transport_new_peer = transport_on_new_peer;

  g_type_class_add_private (obj_class, sizeof (EvdDBusBridgePrivate));
}

static void
evd_dbus_bridge_init (EvdDBusBridge *self)
{
  EvdDBusBridgePrivate *priv;

  priv = EVD_DBUS_BRIDGE_GET_PRIVATE (self);
  self->priv = priv;

  priv->agent_vtable.proxy_signal = evd_dbus_bridge_on_proxy_signal;
  priv->agent_vtable.proxy_properties_changed = evd_dbus_bridge_on_proxy_props_changed;
  priv->agent_vtable.method_call = evd_dbus_bridge_on_reg_obj_call_method;
  priv->agent_vtable.name_acquired = evd_dbus_bridge_on_name_acquired;
  priv->agent_vtable.name_lost = evd_dbus_bridge_on_name_lost;
}

static void
evd_dbus_bridge_dispose (GObject *obj)
{
  G_OBJECT_CLASS (evd_dbus_bridge_parent_class)->dispose (obj);
}

static void
evd_dbus_bridge_finalize (GObject *obj)
{
  G_OBJECT_CLASS (evd_dbus_bridge_parent_class)->finalize (obj);
}

static gchar *
escape_json_for_args (const gchar *json)
{
  gchar *escaped_json1;
  gchar *escaped_json2;

  escaped_json1 = g_strescape (json, "\b\f\n\r\t\'");
  escaped_json2 = g_strescape (escaped_json1, "\b\f\n\r\t\'");
  g_free (escaped_json1);

  return escaped_json2;
}

static MsgClosure *
evd_dbus_bridge_new_msg_closure (EvdDBusBridge *self,
                                 GObject       *obj,
                                 guint8         cmd,
                                 guint64        serial,
                                 guint32        conn_id,
                                 gint32         subject,
                                 const gchar   *args,
                                 gint           err_code)
{
  MsgClosure *closure;

  closure = g_slice_new (MsgClosure);

  closure->bridge = self;
  closure->obj = obj;
  closure->cmd = cmd;
  closure->serial = serial;
  closure->conn_id = conn_id;
  closure->subject = subject;
  closure->args = g_strdup (args);
  closure->err_code = err_code;

  return closure;
}

static void
evd_dbus_bridge_free_msg_closure (MsgClosure *closure)
{
  g_free (closure->args);
  g_slice_free (MsgClosure, closure);
}

static void
evd_dbus_bridge_on_proxy_signal (GObject     *obj,
                                 guint        conn_id,
                                 guint        proxy_id,
                                 const gchar *signal_name,
                                 GVariant    *parameters,
                                 gpointer     user_data)
{
  EvdDBusBridge *self = EVD_DBUS_BRIDGE (user_data);
  gchar *json;
  gchar *escaped_json;
  gchar *args;
  const gchar *signature;

  json = json_gvariant_serialize_data (parameters, NULL);
  escaped_json = escape_json_for_args (json);
  signature = g_variant_get_type_string (parameters);

  args = g_strdup_printf ("\\\"%s\\\",\\\"%s\\\",\\\"%s\\\"",
                          signal_name,
                          escaped_json,
                          signature);

  evd_dbus_bridge_send (self,
                        obj,
                        EVD_DBUS_BRIDGE_CMD_EMIT_SIGNAL,
                        0,
                        conn_id,
                        proxy_id,
                        args);

  g_free (args);
  g_free (escaped_json);
  g_free (json);
}

static void
evd_dbus_bridge_on_proxy_props_changed (GObject     *obj,
                                        guint        conn_id,
                                        guint        proxy_id,
                                        GVariant    *changed_properties,
                                        GStrv       *invalidated_properties,
                                        gpointer     user_data)
{
  /* @TODO */
}

static void
evd_dbus_bridge_on_name_acquired (GObject *object,
                                  guint    conn_id,
                                  guint    owner_id,
                                  gpointer user_data)
{
  EvdDBusBridge *self = EVD_DBUS_BRIDGE (user_data);

  evd_dbus_bridge_send (self,
                        object,
                        EVD_DBUS_BRIDGE_CMD_NAME_ACQUIRED,
                        0,
                        conn_id,
                        owner_id,
                        "");
}

static void
evd_dbus_bridge_on_name_lost (GObject *object,
                              guint    conn_id,
                              guint    owner_id,
                              gpointer user_data)
{
  EvdDBusBridge *self = EVD_DBUS_BRIDGE (user_data);

  evd_dbus_bridge_send (self,
                        object,
                        EVD_DBUS_BRIDGE_CMD_NAME_LOST,
                        0,
                        conn_id,
                        owner_id,
                        "");
}

static void
evd_dbus_bridge_on_reg_obj_call_method (GObject     *obj,
                                        guint        conn_id,
                                        const gchar *sender,
                                        const gchar *method_name,
                                        guint        registration_id,
                                        GVariant    *parameters,
                                        guint64      serial,
                                        gpointer     user_data)
{
  EvdDBusBridge *self = EVD_DBUS_BRIDGE (user_data);
  gchar *json;
  gchar *escaped_json;
  gchar *args;
  const gchar *signature;

  json = json_gvariant_serialize_data (parameters, NULL);
  escaped_json = escape_json_for_args (json);
  signature = g_variant_get_type_string (parameters);

  args = g_strdup_printf ("\\\"%s\\\",\\\"%s\\\",\\\"%s\\\",0,0",
                          method_name,
                          escaped_json,
                          signature);

  evd_dbus_bridge_send (self,
                        obj,
                        EVD_DBUS_BRIDGE_CMD_CALL_METHOD,
                        serial,
                        conn_id,
                        registration_id,
                        args);

  g_free (args);
  g_free (escaped_json);
  g_free (json);
}

static void
evd_dbus_bridge_send (EvdDBusBridge *self,
                      GObject       *obj,
                      guint8         cmd,
                      guint64        serial,
                      guint32        conn_id,
                      guint32        subject,
                      const gchar   *args)
{
  gchar *json;

  json = g_strdup_printf ("[%u,%" G_GUINT64_FORMAT ",%u,%u,\"[%s]\"]",
                          cmd,
                          serial,
                          conn_id,
                          subject,
                          args);

  if (EVD_IS_PEER (obj))
    {
      GError *error = NULL;

      if (! evd_peer_send_text (EVD_PEER (obj), json, &error))
        {
          g_warning ("error sending DBus msg to peer: %s", error->message);
          g_error_free (error);
        }
    }

#ifdef ENABLE_TESTS
  if (self->priv->send_msg_callback != NULL)
    {
      self->priv->send_msg_callback (self,
                                     obj,
                                     json,
                                     self->priv->send_msg_user_data);
    }
#endif

  g_free (json);
}

static gboolean
evd_dbus_bridge_on_idle_send (gpointer user_data)
{
  MsgClosure *closure = (MsgClosure *) user_data;

  evd_dbus_bridge_send (closure->bridge,
                        closure->obj,
                        closure->cmd,
                        closure->serial,
                        closure->conn_id,
                        closure->subject,
                        closure->args);

  evd_dbus_bridge_free_msg_closure (closure);

  return FALSE;
}

static void
evd_dbus_bridge_send_in_idle (EvdDBusBridge *self,
                              GObject       *obj,
                              guint8         cmd,
                              guint64        serial,
                              guint32        conn_id,
                              guint32        subject,
                              const gchar   *args)
{
  MsgClosure *closure;

  closure = evd_dbus_bridge_new_msg_closure (self,
                                             obj,
                                             cmd,
                                             serial,
                                             conn_id,
                                             subject,
                                             args,
                                             0);

  evd_timeout_add (NULL,
                   0,
                   G_PRIORITY_DEFAULT,
                   evd_dbus_bridge_on_idle_send, closure);
}

static void
evd_dbus_bridge_send_error (EvdDBusBridge *self,
                            GObject       *obj,
                            guint64        serial,
                            guint32        conn_id,
                            guint32        subject,
                            gint           err_code,
                            const gchar   *err_msg)
{
  gchar *args;

  if (err_msg != NULL)
    args = g_strdup_printf ("%d,\\\"%s\\\"", err_code, err_msg);
  else
    args = g_strdup_printf ("%d", err_code);

  evd_dbus_bridge_send (self,
                        obj,
                        EVD_DBUS_BRIDGE_CMD_ERROR,
                        serial,
                        conn_id,
                        subject,
                        args);
  g_free (args);
}

static gboolean
evd_dbus_bridge_on_send_error_idle (gpointer user_data)
{
  MsgClosure *closure = (MsgClosure *) user_data;

  evd_dbus_bridge_send_error (closure->bridge,
                              closure->obj,
                              closure->serial,
                              closure->conn_id,
                              closure->subject,
                              closure->err_code,
                              closure->args);

  evd_dbus_bridge_free_msg_closure (closure);

  return FALSE;
}

static void
evd_dbus_bridge_send_error_in_idle (EvdDBusBridge *self,
                                    GObject       *obj,
                                    guint64        serial,
                                    guint32        conn_id,
                                    guint32        subject,
                                    gint           err_code,
                                    const gchar   *err_msg)
{
  MsgClosure *closure;

  closure = evd_dbus_bridge_new_msg_closure (self,
                                             obj,
                                             EVD_DBUS_BRIDGE_CMD_ERROR,
                                             serial,
                                             conn_id,
                                             subject,
                                             err_msg,
                                             err_code);

  evd_timeout_add (NULL,
                   0,
                   G_PRIORITY_DEFAULT,
                   evd_dbus_bridge_on_send_error_idle,
                   closure);
}

static void
evd_dbus_bridge_on_new_connection (GObject      *obj,
                                   GAsyncResult *res,
                                   gpointer      user_data)
{
  EvdDBusBridge *self;
  MsgClosure *closure = (MsgClosure *) user_data;
  guint conn_id;
  GError *error = NULL;

  self = closure->bridge;

  if ( (conn_id = evd_dbus_agent_new_connection_finish (obj,
                                                        res,
                                                        &error)) == 0)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  closure->serial,
                                  0,
                                  closure->subject,
                                  EVD_DBUS_BRIDGE_ERR_CONNECTION_FAILED,
                                  error->message);
      g_error_free (error);
    }
  else
    {
      gchar *args;

      args = g_strdup_printf ("%u", (guint32) conn_id);
      evd_dbus_bridge_send (closure->bridge,
                            closure->obj,
                            EVD_DBUS_BRIDGE_CMD_REPLY,
                            closure->serial,
                            0,
                            closure->subject,
                            args);
      g_free (args);
    }

  evd_dbus_bridge_free_msg_closure (closure);
}

static void
evd_dbus_bridge_new_connection (EvdDBusBridge *self,
                                GObject       *obj,
                                guint64        serial,
                                guint32        conn_id,
                                const gchar   *args)
{
  gchar *addr;
  gboolean reuse;
  MsgClosure *closure;
  GVariant *variant_args;

  variant_args = json_gvariant_deserialize_data (args, -1, "(sb)", NULL);
  if (variant_args == NULL)
    {
      evd_dbus_bridge_send_error_in_idle (self,
                                          obj,
                                          serial,
                                          conn_id,
                                          0,
                                          EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                          NULL);
      return;
    }

  g_variant_get (variant_args, "(sb)", &addr, &reuse);

  closure = evd_dbus_bridge_new_msg_closure (self,
                                             obj,
                                             EVD_DBUS_BRIDGE_CMD_NEW_CONNECTION,
                                             serial,
                                             conn_id,
                                             0,
                                             NULL,
                                             0);
  evd_dbus_agent_new_connection (obj,
                                 addr,
                                 reuse,
                                 NULL,
                                 evd_dbus_bridge_on_new_connection,
                                 closure);

  g_free (addr);
  g_variant_unref (variant_args);
}

static void
evd_dbus_bridge_close_connection (EvdDBusBridge *self,
                                  GObject       *obj,
                                  guint64        serial,
                                  guint32        conn_id,
                                  guint32        subject)
{
  GError *error = NULL;

  /* @TODO: validate that 'subject' is 0 */

  if (evd_dbus_agent_close_connection (obj, conn_id, &error))
    {
      evd_dbus_bridge_send_in_idle (self,
                                    obj,
                                    EVD_DBUS_BRIDGE_CMD_REPLY,
                                    serial,
                                    conn_id,
                                    subject,
                                    "");
    }
  else
    {
      evd_dbus_bridge_send_error_in_idle (self,
                                          obj,
                                          serial,
                                          conn_id,
                                          subject,
                                          EVD_DBUS_BRIDGE_ERR_INVALID_SUBJECT,
                                          error->message);
      g_error_free (error);
    }
}

static void
evd_dbus_bridge_own_name (EvdDBusBridge *self,
                          GObject       *obj,
                          guint64        serial,
                          guint32        conn_id,
                          guint32        subject,
                          const gchar   *args)
{
  GVariant *variant_args;
  gchar *name;
  guint32 flags;
  GDBusConnection *dbus_conn;
  GError *error = NULL;
  guint owning_id;
  gchar *st_args;

  variant_args = json_gvariant_deserialize_data (args, -1, "(su)", NULL);
  if (variant_args == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      return;
    }

  g_variant_get (variant_args, "(su)", &name, &flags);

  dbus_conn = evd_dbus_agent_get_connection (obj, conn_id, &error);
  if (dbus_conn == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_SUBJECT,
                                  NULL);

      g_error_free (error);

      goto free;
    }

  owning_id =
    evd_dbus_agent_own_name (obj,
                             conn_id,
                             name,
                             flags,
                             &error);

  st_args = g_strdup_printf ("%u", owning_id);
  evd_dbus_bridge_send (self,
                        obj,
                        EVD_DBUS_BRIDGE_CMD_REPLY,
                        serial,
                        conn_id,
                        subject,
                        st_args);
  g_free (st_args);

 free:
  g_free (name);
  g_variant_unref (variant_args);
}

static void
evd_dbus_bridge_unown_name (EvdDBusBridge *self,
                            GObject       *obj,
                            guint64        serial,
                            guint32        conn_id,
                            guint32        subject,
                            const gchar   *args)
{
  GError *error = NULL;

  if (evd_dbus_agent_unown_name (obj, subject, &error))
    {
      evd_dbus_bridge_send (self,
                            obj,
                            EVD_DBUS_BRIDGE_CMD_REPLY,
                            serial,
                            conn_id,
                            subject,
                            "");
    }
  else
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  subject,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
    }
}

static void
evd_dbus_bridge_register_object (EvdDBusBridge *self,
                                 GObject       *obj,
                                 guint64        serial,
                                 guint32        conn_id,
                                 guint32        subject,
                                 const gchar   *args)
{
  GVariant *variant_args;
  gchar *object_path;
  gchar *iface_data;
  gchar *node_data;
  guint reg_id = 0;
  GDBusNodeInfo *node_info = NULL;
  GError *error = NULL;

  variant_args = json_gvariant_deserialize_data (args, -1, "(ss)", NULL);
  if (variant_args == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      return;
    }

  g_variant_get (variant_args, "(ss)", &object_path, &iface_data);

  /* create interface info */
  node_data = g_strdup_printf ("<node>%s</node>", iface_data);
  node_info = g_dbus_node_info_new_for_xml (node_data, &error);
  if (node_info != NULL && node_info->interfaces != NULL)
    {
      GDBusInterfaceInfo *iface_info;

      iface_info = node_info->interfaces[0];
      reg_id = evd_dbus_agent_register_object (obj,
                                               conn_id,
                                               object_path,
                                               iface_info,
                                               &error);

      if (reg_id > 0)
        {
          gchar *args;

          args = g_strdup_printf ("%u", reg_id);
          evd_dbus_bridge_send (self,
                                obj,
                                EVD_DBUS_BRIDGE_CMD_REPLY,
                                serial,
                                conn_id,
                                subject,
                                args);
          g_free (args);
        }
      else
        {
          evd_dbus_bridge_send_error (self,
                                      obj,
                                      serial,
                                      conn_id,
                                      subject,
                                      EVD_DBUS_BRIDGE_ERR_ALREADY_REGISTERED,
                                      NULL);
          g_error_free (error);
        }
    }
  else
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  subject,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
    }

  if (node_info != NULL)
    g_dbus_node_info_unref (node_info);
  g_free (node_data);
  g_free (iface_data);
  g_free (object_path);
  g_variant_unref (variant_args);
}

static void
evd_dbus_bridge_unregister_object (EvdDBusBridge *self,
                                   GObject       *obj,
                                   guint64        serial,
                                   guint32        conn_id,
                                   guint32        subject,
                                   const gchar   *args)
{
  GError *error = NULL;

  if (evd_dbus_agent_unregister_object (obj, subject, &error))
    {
      evd_dbus_bridge_send (self,
                            obj,
                            EVD_DBUS_BRIDGE_CMD_REPLY,
                            serial,
                            conn_id,
                            subject,
                            "");
    }
  else
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  subject,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_SUBJECT,
                                  NULL);
      g_error_free (error);
    }
}

static void
evd_dbus_bridge_on_new_proxy (GObject      *obj,
                              GAsyncResult *res,
                              gpointer      user_data)
{
  MsgClosure *closure = (MsgClosure *) user_data;
  guint proxy_id;
  GError *error = NULL;

  if ( (proxy_id = evd_dbus_agent_new_proxy_finish (obj, res, &error)) > 0)
    {
      gchar *args;

      args = g_strdup_printf ("%u", proxy_id);
      evd_dbus_bridge_send (closure->bridge,
                            closure->obj,
                            EVD_DBUS_BRIDGE_CMD_REPLY,
                            closure->serial,
                            closure->conn_id,
                            closure->subject,
                            args);
      g_free (args);
    }
  else
    {
      evd_dbus_bridge_send_error (closure->bridge,
                                  closure->obj,
                                  closure->serial,
                                  closure->conn_id,
                                  closure->subject,
                                  EVD_DBUS_BRIDGE_ERR_PROXY_FAILED,
                                  error->message);
      g_error_free (error);
    }

  evd_dbus_bridge_free_msg_closure (closure);
}

static void
evd_dbus_bridge_new_proxy (EvdDBusBridge *self,
                           GObject       *obj,
                           guint64        serial,
                           guint32        conn_id,
                           guint32        subject,
                           const gchar   *args)
{
  GVariant *variant_args;
  guint flags;
  gchar *name;
  gchar *obj_path;
  gchar *iface_name;
  MsgClosure *closure;

  variant_args = json_gvariant_deserialize_data (args, -1, "(sssu)", NULL);
  if (variant_args == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      return;
    }

  g_variant_get (variant_args, "(sssu)",
                 &name,
                 &obj_path,
                 &iface_name,
                 &flags);

  closure = evd_dbus_bridge_new_msg_closure (self,
                                             obj,
                                             EVD_DBUS_BRIDGE_CMD_NEW_PROXY,
                                             serial,
                                             conn_id,
                                             subject,
                                             NULL,
                                             0);

  evd_dbus_agent_new_proxy (obj,
                            conn_id,
                            flags,
                            name,
                            obj_path,
                            iface_name,
                            NULL,
                            evd_dbus_bridge_on_new_proxy,
                            closure);

  g_free (iface_name);
  g_free (obj_path);
  g_free (name);
  g_variant_unref (variant_args);
}

static void
evd_dbus_bridge_close_proxy (EvdDBusBridge *self,
                             GObject       *obj,
                             guint64        serial,
                             guint32        conn_id,
                             guint32        subject,
                             const gchar   *args)
{
  GError *error = NULL;

  if (evd_dbus_agent_close_proxy (obj, subject, &error))
    {
      evd_dbus_bridge_send (self,
                            obj,
                            EVD_DBUS_BRIDGE_CMD_REPLY,
                            serial,
                            conn_id,
                            subject,
                            "");
    }
  else
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  subject,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_SUBJECT,
                                  NULL);
      g_error_free (error);
    }
}

static void
evd_dbus_proxy_on_call_method_return (GObject      *obj,
                                      GAsyncResult *res,
                                      gpointer      user_data)
{
  MsgClosure *closure = (MsgClosure *) user_data;
  GVariant *ret_variant;
  GError *error = NULL;

  ret_variant = g_dbus_proxy_call_finish (G_DBUS_PROXY (obj), res, &error);
  if (ret_variant != NULL)
    {
      gchar *json;
      gchar *escaped_json;
      gchar *args;

      json = json_gvariant_serialize_data (ret_variant, NULL);
      escaped_json = escape_json_for_args (json);
      args = g_strdup_printf ("\\\"%s\\\"", escaped_json);

      evd_dbus_bridge_send (closure->bridge,
                            closure->obj,
                            EVD_DBUS_BRIDGE_CMD_CALL_METHOD_RETURN,
                            closure->serial,
                            closure->conn_id,
                            closure->subject,
                            args);

      g_free (args);
      g_free (escaped_json);
      g_free (json);
      g_variant_unref (ret_variant);
    }
  else
    {
      gint err_code;
      gchar *err_msg = NULL;

      /* @TODO: organize this in a method to convert from
         DBus error to bridge error */
      if (error->code == G_DBUS_ERROR_INVALID_ARGS)
        err_code = EVD_DBUS_BRIDGE_ERR_INVALID_ARGS;
      else if (error->code == G_DBUS_ERROR_UNKNOWN_METHOD)
        err_code = EVD_DBUS_BRIDGE_ERR_UNKNOWN_METHOD;
      else
        {
          err_code = EVD_DBUS_BRIDGE_ERR_FAILED;
          err_msg = error->message;
        }

      evd_dbus_bridge_send_error (closure->bridge,
                                  closure->obj,
                                  closure->serial,
                                  closure->conn_id,
                                  closure->subject,
                                  err_code,
                                  err_msg);
      g_error_free (error);
    }

  evd_dbus_bridge_free_msg_closure (closure);
}

static void
evd_dbus_bridge_call_method (EvdDBusBridge *self,
                             GObject       *obj,
                             guint64        serial,
                             guint32        subject,
                             guint32        conn_id,
                             const gchar   *args)
{
  GVariant *variant_args;
  gchar *method_name;
  gchar *method_args;
  guint call_flags;
  gint timeout;
  gchar *signature;
  GDBusProxy *proxy;
  MsgClosure *closure;
  GVariant *params;

  variant_args = json_gvariant_deserialize_data (args, -1, "(ssgui)", NULL);
  if (variant_args == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  subject,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      return;
    }

  g_variant_get (variant_args, "(ssgui)",
                 &method_name,
                 &method_args,
                 &signature,
                 &call_flags,
                 &timeout);

  params = json_gvariant_deserialize_data (method_args, -1, signature, NULL);
  if (params == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  subject,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      goto out;
    }

  proxy = evd_dbus_agent_get_proxy (obj, subject, NULL);
  if (proxy == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  subject,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_SUBJECT,
                                  NULL);
      goto out;
    }

  closure = evd_dbus_bridge_new_msg_closure (self,
                                             obj,
                                             EVD_DBUS_BRIDGE_CMD_CALL_METHOD,
                                             serial,
                                             conn_id,
                                             subject,
                                             args,
                                             0);

  g_dbus_proxy_call (proxy,
                     method_name,
                     params,
                     call_flags,
                     timeout,
                     NULL,
                     evd_dbus_proxy_on_call_method_return,
                     closure);

 out:
  g_free (signature);
  g_free (method_args);
  g_free (method_name);
  g_variant_unref (variant_args);
}

static gchar *
evd_dbus_bridge_get_method_signature_from_reg_object (GObject *obj,
                                                      guint    reg_id,
                                                      guint64  serial)
{
  gchar *signature;
  GString *sig_str;
  GDBusMethodInvocation *invocation;
  const GDBusMethodInfo *method_info;

  invocation = evd_dbus_agent_get_method_invocation (obj,
                                                     reg_id,
                                                     serial,
                                                     NULL);
  if (invocation == NULL)
    return NULL;

  method_info = g_dbus_method_invocation_get_method_info (invocation);

  sig_str = g_string_new ("(");

  if (method_info->out_args != NULL)
    {
      gint i = 0;

      while (method_info->out_args[i] != NULL)
        {
          g_string_append (sig_str, method_info->out_args[i]->signature);
          i++;
        }
    }

  g_string_append (sig_str, ")");

  signature = sig_str->str;
  g_string_free (sig_str, FALSE);

  return signature;
}

static void
evd_dbus_bridge_call_method_return (EvdDBusBridge *self,
                                    GObject       *obj,
                                    guint64        serial,
                                    guint32        conn_id,
                                    guint32        subject,
                                    const gchar   *args)
{
  GVariant *variant_args = NULL;
  gchar *return_args = NULL;
  gchar *signature = NULL;
  GVariant *return_variant;
  gboolean invalid_args = FALSE;

  signature =
    evd_dbus_bridge_get_method_signature_from_reg_object (obj, subject, serial);
  if (signature == NULL)
    {
      invalid_args = TRUE;
      goto out;
    }

  variant_args = json_gvariant_deserialize_data (args, -1, "(s)", NULL);
  if (variant_args == NULL)
    {
      invalid_args = TRUE;
      goto out;
    }

  g_variant_get (variant_args, "(s)", &return_args);

  return_variant = json_gvariant_deserialize_data (return_args,
                                                   -1,
                                                   signature,
                                                   NULL);
  if (return_variant == NULL)
    {
      invalid_args = TRUE;
      goto out;
    }

  if (! evd_dbus_agent_method_call_return (obj,
                                           subject,
                                           serial,
                                           return_variant,
                                           NULL))
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  subject,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_SUBJECT,
                                  NULL);
    }

 out:
  if (invalid_args)
    evd_dbus_bridge_send_error (self,
                                obj,
                                serial,
                                conn_id,
                                subject,
                                EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                NULL);

  g_free (signature);
  g_free (return_args);
  if (variant_args != NULL)
    g_variant_unref (variant_args);
}

static void
evd_dbus_bridge_emit_signal (EvdDBusBridge *self,
                             GObject       *obj,
                             guint64        serial,
                             guint32        conn_id,
                             guint32        subject,
                             const gchar   *args)
{
  GVariant *variant_args;
  gchar *signal_name;
  gchar *signal_args;
  gchar *signature;
  GVariant *signal_args_variant;
  GError *error = NULL;

  variant_args = json_gvariant_deserialize_data (args, -1, "(sss)", NULL);
  if (variant_args == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      return;
    }

  g_variant_get (variant_args, "(sss)",
                 &signal_name,
                 &signal_args,
                 &signature);

  signal_args_variant = json_gvariant_deserialize_data (signal_args,
                                                        -1,
                                                        signature,
                                                        NULL);
  if (signal_args_variant == NULL)
    {
      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  0,
                                  EVD_DBUS_BRIDGE_ERR_INVALID_ARGS,
                                  NULL);
      goto out;
    }
  else
    {
      g_variant_ref_sink (signal_args_variant);
    }

  if (! evd_dbus_agent_emit_signal (obj,
                                    subject,
                                    signal_name,
                                    signal_args_variant,
                                    &error))
    {
      gint err_code;
      gchar *err_msg = NULL;

      if (error->code == G_DBUS_ERROR_INVALID_ARGS)
        err_code = EVD_DBUS_BRIDGE_ERR_INVALID_ARGS;
      else
        {
          err_code = EVD_DBUS_BRIDGE_ERR_FAILED;
          err_msg = error->message;
        }

      evd_dbus_bridge_send_error (self,
                                  obj,
                                  serial,
                                  conn_id,
                                  subject,
                                  err_code,
                                  err_msg);
      g_error_free (error);
    }

 out:
  if (signal_args_variant != NULL)
    g_variant_unref (signal_args_variant);
  g_free (signature);
  g_free (signal_args);
  g_free (signal_name);
  g_variant_unref (variant_args);
}

static void
transport_on_new_peer (EvdIpcMechanism *ipc_mechanism,
                       EvdTransport    *transport,
                       EvdPeer         *peer)
{
  EvdDBusBridge *self = EVD_DBUS_BRIDGE (ipc_mechanism);

  evd_dbus_agent_set_object_vtable (G_OBJECT (peer),
                                    &self->priv->agent_vtable,
                                    self);
}

static void
transport_on_receive (EvdIpcMechanism *ipc_mechanism,
                      EvdTransport    *transport,
                      EvdPeer         *peer,
                      const guchar    *data,
                      gsize            size)
{
  evd_dbus_bridge_process_msg (EVD_DBUS_BRIDGE (ipc_mechanism),
                               G_OBJECT (peer),
                               (const gchar *) data,
                               size);
}

/* public methods */

EvdDBusBridge *
evd_dbus_bridge_new (void)
{
  EvdDBusBridge *self;

  self = g_object_new (EVD_TYPE_DBUS_BRIDGE, NULL);

  return self;
}

void
evd_dbus_bridge_process_msg (EvdDBusBridge *self,
                             GObject       *object,
                             const gchar   *msg,
                             gsize          length)
{
  GVariant *variant_msg;
  guint8 cmd;
  guint64 serial;
  guint32 conn_id;
  guint32 subject;
  gchar *args;

  variant_msg = json_gvariant_deserialize_data (msg, length, "(ytuus)", NULL);
  if (variant_msg == NULL)
    {
      evd_dbus_bridge_send_error_in_idle (self,
                                          object,
                                          0,
                                          0,
                                          0,
                                          EVD_DBUS_BRIDGE_ERR_INVALID_MSG,
                                          NULL);
      return;
    }

  g_variant_get (variant_msg,
                 "(ytuus)",
                 &cmd,
                 &serial,
                 &conn_id,
                 &subject,
                 &args);

  switch (cmd)
    {
    case EVD_DBUS_BRIDGE_CMD_NEW_CONNECTION:
      evd_dbus_bridge_new_connection (self, object, serial, conn_id, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_CLOSE_CONNECTION:
      evd_dbus_bridge_close_connection (self, object, serial, conn_id, subject);
      break;

    case EVD_DBUS_BRIDGE_CMD_OWN_NAME:
      evd_dbus_bridge_own_name (self, object, serial, conn_id, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_UNOWN_NAME:
      evd_dbus_bridge_unown_name (self, object, serial, conn_id, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_REGISTER_OBJECT:
      evd_dbus_bridge_register_object (self, object, serial, conn_id, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_UNREGISTER_OBJECT:
      evd_dbus_bridge_unregister_object (self, object, serial, conn_id, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_NEW_PROXY:
      evd_dbus_bridge_new_proxy (self, object, serial, conn_id, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_CLOSE_PROXY:
      evd_dbus_bridge_close_proxy (self, object, serial, conn_id, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_CALL_METHOD:
      evd_dbus_bridge_call_method (self, object, serial, conn_id, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_CALL_METHOD_RETURN:
      evd_dbus_bridge_call_method_return (self, object, serial, conn_id, subject, args);
      break;

    case EVD_DBUS_BRIDGE_CMD_EMIT_SIGNAL:
      evd_dbus_bridge_emit_signal (self, object, serial, conn_id, subject, args);
      break;

    default:
      evd_dbus_bridge_send_error_in_idle (self,
                                          object,
                                          serial,
                                          conn_id,
                                          0,
                                          EVD_DBUS_BRIDGE_ERR_UNKNOW_COMMAND,
                                          NULL);
      break;
    }

  g_free (args);
  g_variant_unref (variant_msg);
}

#ifdef ENABLE_TESTS

void
evd_dbus_bridge_track_object (EvdDBusBridge *self, GObject *object)
{
  evd_dbus_agent_set_object_vtable (object,
                                    &self->priv->agent_vtable,
                                    self);
}

void
evd_dbus_bridge_set_send_msg_callback (EvdDBusBridge          *self,
                                       EvdDBusBridgeSendMsgCb  callback,
                                       gpointer                user_data)
{
  g_return_if_fail (EVD_IS_DBUS_BRIDGE (self));

  self->priv->send_msg_callback = callback;
  self->priv->send_msg_user_data = user_data;
}

#endif /* ENABLE_TESTS */
