/*
 * evd-service.c
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

#include "evd-service.h"

#include "evd-error.h"
#include "evd-utils.h"
#include "evd-marshal.h"
#include "evd-socket.h"
#include "evd-tls-session.h"

G_DEFINE_TYPE (EvdService, evd_service, EVD_TYPE_IO_STREAM_GROUP)

#define EVD_SERVICE_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                      EVD_TYPE_SERVICE, \
                                      EvdServicePrivate))

#define VALIDATION_HINT_KEY "org.eventdance.lib.Service.VALIDATION_HINT"

/* private data */
struct _EvdServicePrivate
{
  GHashTable *listeners;

  GType io_stream_type;

  gboolean tls_autostart;
  EvdTlsCredentials *tls_cred;
};

/* signals */
enum
{
  SIGNAL_VALIDATE_CONNECTION,
  SIGNAL_VALIDATE_TLS_CONNECTION,
  SIGNAL_LAST
};

/* properties */
enum
{
  PROP_0,
  PROP_TLS_AUTOSTART,
  PROP_TLS_CREDENTIALS
};

static guint evd_service_signals[SIGNAL_LAST] = { 0 };

static void     evd_service_class_init                 (EvdServiceClass *class);
static void     evd_service_init                       (EvdService *self);

static void     evd_service_finalize                   (GObject *obj);
static void     evd_service_dispose                    (GObject *obj);

static void     evd_service_set_property               (GObject      *obj,
                                                        guint         prop_id,
                                                        const GValue *value,
                                                        GParamSpec   *pspec);
static void     evd_service_get_property               (GObject    *obj,
                                                        guint       prop_id,
                                                        GValue     *value,
                                                        GParamSpec *pspec);

static void     evd_service_listener_destroy           (gpointer listener);
static void     evd_service_listener_on_new_connection (EvdSocket     *listener,
                                                        EvdConnection *conn,
                                                        gpointer       user_data);
static void     evd_service_listener_on_close          (EvdSocket *listener,
                                                        gpointer   user_data);

static void     connection_accepted                    (EvdService    *self,
                                                        EvdConnection *conn);
static void     connection_rejected                    (EvdService    *self,
                                                        EvdConnection *conn);

static void     evd_service_connection_starttls        (EvdService    *self,
                                                        EvdConnection *conn);

static gboolean evd_service_validate_conn_signal_acc   (GSignalInvocationHint *ihint,
                                                        GValue                *return_accu,
                                                        const GValue          *handler_return,
                                                        gpointer              data);

static gboolean evd_service_add                        (EvdIoStreamGroup *self,
                                                        GIOStream        *io_stream);

static void
evd_service_class_init (EvdServiceClass *class)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (class);
  EvdIoStreamGroupClass *conn_group_class = EVD_IO_STREAM_GROUP_CLASS (class);

  class->connection_accepted = connection_accepted;
  class->connection_rejected = connection_rejected;

  obj_class->dispose = evd_service_dispose;
  obj_class->finalize = evd_service_finalize;
  obj_class->get_property = evd_service_get_property;
  obj_class->set_property = evd_service_set_property;

  conn_group_class->add = evd_service_add;

  evd_service_signals[SIGNAL_VALIDATE_CONNECTION] =
    g_signal_new ("validate-connection",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdServiceClass, signal_validate_connection),
                  evd_service_validate_conn_signal_acc,
                  NULL,
                  evd_marshal_UINT__OBJECT,
                  G_TYPE_UINT, 1,
                  EVD_TYPE_CONNECTION);

  evd_service_signals[SIGNAL_VALIDATE_TLS_CONNECTION] =
    g_signal_new ("validate-tls-connection",
                  G_TYPE_FROM_CLASS (obj_class),
                  G_SIGNAL_ACTION,
                  G_STRUCT_OFFSET (EvdServiceClass, signal_validate_tls_connection),
                  evd_service_validate_conn_signal_acc,
                  NULL,
                  evd_marshal_UINT__OBJECT,
                  G_TYPE_UINT, 1,
                  EVD_TYPE_CONNECTION);

  g_object_class_install_property (obj_class, PROP_TLS_AUTOSTART,
                                   g_param_spec_boolean ("tls-autostart",
                                                         "Autostart TLS in connections",
                                                         "Returns TRUE if TLS upgrade should be performed automatically upon incoming connections",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_TLS_CREDENTIALS,
                                   g_param_spec_object ("tls-credentials",
                                                        "The TLS credentials",
                                                        "The TLS credentials that will be passed to the connections of the service",
                                                        EVD_TYPE_TLS_CREDENTIALS,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  /* add private structure */
  g_type_class_add_private (obj_class, sizeof (EvdServicePrivate));
}

static void
evd_service_init (EvdService *self)
{
  EvdServicePrivate *priv;

  priv = EVD_SERVICE_GET_PRIVATE (self);
  self->priv = priv;

  priv->listeners = g_hash_table_new_full (g_direct_hash,
                                           g_direct_equal,
                                           NULL,
                                           evd_service_listener_destroy);

  priv->io_stream_type = EVD_TYPE_CONNECTION;

  priv->tls_autostart = FALSE;
  priv->tls_cred = NULL;
}

static void
evd_service_dispose (GObject *obj)
{
  EvdService *self = EVD_SERVICE (obj);

  if (self->priv->listeners != NULL)
    {
      g_hash_table_destroy (self->priv->listeners);
      self->priv->listeners = NULL;
    }

  G_OBJECT_CLASS (evd_service_parent_class)->dispose (obj);
}

static void
evd_service_finalize (GObject *obj)
{
  EvdService *self = EVD_SERVICE (obj);

  if (self->priv->tls_cred != NULL)
    g_object_unref (self->priv->tls_cred);

  G_OBJECT_CLASS (evd_service_parent_class)->finalize (obj);
}

static void
evd_service_set_property (GObject      *obj,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  EvdService *self;

  self = EVD_SERVICE (obj);

  switch (prop_id)
    {
    case PROP_TLS_AUTOSTART:
      evd_service_set_tls_autostart (self, g_value_get_boolean (value));
      break;

    case PROP_TLS_CREDENTIALS:
      evd_service_set_tls_credentials (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_service_get_property (GObject    *obj,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  EvdService *self;

  self = EVD_SERVICE (obj);

  switch (prop_id)
    {
    case PROP_TLS_AUTOSTART:
      g_value_set_boolean (value, evd_service_get_tls_autostart (self));
      break;

    case PROP_TLS_CREDENTIALS:
      g_value_set_object (value, evd_service_get_tls_credentials (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_service_listener_destroy (gpointer listener)
{
  EvdSocket *socket = EVD_SOCKET (listener);
  EvdService *self;

  self = g_object_get_data (G_OBJECT (socket), "evd-service");

  g_signal_handlers_disconnect_by_func (socket,
                                        evd_service_listener_on_new_connection,
                                        self);

  g_signal_handlers_disconnect_by_func (socket,
                                        evd_service_listener_on_close,
                                        self);

  g_object_unref (socket);
}

static void
evd_service_listener_on_new_connection (EvdSocket     *listener,
                                        EvdConnection *conn,
                                        gpointer       user_data)
{
  EvdService *self = EVD_SERVICE (user_data);

  evd_io_stream_group_add (EVD_IO_STREAM_GROUP (self), G_IO_STREAM (conn));
}

static void
evd_service_listener_on_close (EvdSocket *listener,
                               gpointer   user_data)
{
  EvdService *self = EVD_SERVICE (user_data);

  evd_service_remove_listener (self, listener);
}

void
connection_accepted (EvdService *self, EvdConnection *conn)
{
  /* nothing to do here other than logging */
}

static void
connection_rejected (EvdService *self, EvdConnection *conn)
{
  /* refuse connection */
  g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
}

void
accept_connection_priv (EvdService *self, EvdConnection *conn)
{
  if (self->priv->tls_autostart && ! evd_connection_get_tls_active (conn))
    {
      evd_service_connection_starttls (self, conn);
    }
  else
    {
      EvdServiceClass *class;

      class = EVD_SERVICE_GET_CLASS (self);
      if (class->connection_accepted != NULL)
        class->connection_accepted (self, conn);
    }
}

void
reject_connection_priv (EvdService *self, EvdConnection *conn)
{
  EvdServiceClass *class;

  class = EVD_SERVICE_GET_CLASS (self);
  if (class->connection_rejected != NULL)
    class->connection_rejected (self, conn);
}

static gboolean
evd_service_validate_conn_signal_acc (GSignalInvocationHint *hint,
                                      GValue                *return_accu,
                                      const GValue          *handler_return,
                                      gpointer               data)
{
  guint ret;
  guint acc_ret;

  ret = g_value_get_uint (handler_return);
  acc_ret = g_value_get_uint (return_accu);

  if (ret > acc_ret)
    g_value_set_uint (return_accu, ret);

  return ret != EVD_VALIDATE_REJECT;
}

static void
evd_service_validate_tls_connection (EvdService *self, EvdConnection *conn)
{
  EvdValidateEnum ret = EVD_VALIDATE_ACCEPT;

  g_signal_emit (self,
                 evd_service_signals[SIGNAL_VALIDATE_TLS_CONNECTION],
                 0,
                 conn,
                 &ret);

  if (ret == EVD_VALIDATE_ACCEPT)
    {
      accept_connection_priv (self, conn);
    }
  else if (ret == EVD_VALIDATE_REJECT)
    {
      reject_connection_priv (self, conn);
    }
  else
    {
      gboolean *hint;

      /* validation is pending, add validation hint to connection */
      hint = g_new (gboolean, 1);
      *hint = TRUE;

      g_object_set_data_full (G_OBJECT (conn),
                              VALIDATION_HINT_KEY,
                              hint,
                              g_free);
      g_object_ref (conn);
    }
}

static void
evd_service_validate_connection (EvdService *self, EvdConnection *conn)
{
  EvdValidateEnum ret = EVD_VALIDATE_ACCEPT;

  g_signal_emit (self,
                 evd_service_signals[SIGNAL_VALIDATE_CONNECTION],
                 0,
                 conn,
                 &ret);

  if (ret == EVD_VALIDATE_ACCEPT)
    {
      accept_connection_priv (self, conn);
    }
  else if (ret == EVD_VALIDATE_REJECT)
    {
      reject_connection_priv (self, conn);
    }
  else
    {
      gboolean *hint;

      /* validation is pending, add validation hint to connection */
      hint = g_new (gboolean, 1);
      *hint = TRUE;

      g_object_set_data_full (G_OBJECT (conn),
                              VALIDATION_HINT_KEY,
                              hint,
                              g_free);
      g_object_ref (conn);
    }
}

static void
evd_service_connection_on_tls_started (GObject      *obj,
                                       GAsyncResult *res,
                                       gpointer      user_data)
{
  EvdService *self = EVD_SERVICE (user_data);
  EvdConnection *conn = EVD_CONNECTION (obj);
  GError *error = NULL;

  if (evd_connection_starttls_finish (conn, res, &error))
    {
      evd_service_validate_tls_connection (self, conn);
    }
  else
    {
      g_debug ("TLS upgrade error in EvdService: %s", error->message);
      g_error_free (error);

      g_io_stream_close (G_IO_STREAM (conn), NULL, NULL);
    }
}

static void
evd_service_connection_starttls (EvdService    *self,
                                 EvdConnection *conn)
{
  EvdTlsSession *tls_session;
  EvdTlsCredentials *tls_cred;

  tls_session = evd_connection_get_tls_session (conn);
  tls_cred = evd_service_get_tls_credentials (self);
  evd_tls_session_set_credentials (tls_session, tls_cred);

  evd_connection_starttls (conn,
                           EVD_TLS_MODE_SERVER,
                           NULL,
                           evd_service_connection_on_tls_started,
                           self);
}

static gboolean
evd_service_add (EvdIoStreamGroup *group, GIOStream *io_stream)
{
  EvdService *self = EVD_SERVICE (group);
  EvdConnection *conn;

  /* @TODO: implement connection limit */

  /* @TODO: check if connection type matches service's io_stream_type */

  if (! EVD_IO_STREAM_GROUP_CLASS (evd_service_parent_class)->add (group,
                                                                   io_stream) ||
      ! evd_connection_is_connected (EVD_CONNECTION (io_stream)))
    {
      return FALSE;
    }

  conn = EVD_CONNECTION (io_stream);

  evd_service_validate_connection (self, conn);

  return TRUE;
}

static void
evd_service_socket_on_listen (GObject      *obj,
                              GAsyncResult *result,
                              gpointer      user_data)
{
  GSimpleAsyncResult *res = G_SIMPLE_ASYNC_RESULT (user_data);
  EvdService *self;
  GError *error = NULL;

  self = EVD_SERVICE (g_async_result_get_source_object (G_ASYNC_RESULT (res)));

  if (! evd_socket_listen_finish (EVD_SOCKET (obj),
                                  result,
                                  &error))
    {
      g_simple_async_result_take_error (res, error);
    }
  else
    {
      evd_service_add_listener (self, EVD_SOCKET (obj));
      g_object_unref (obj);
    }

  g_simple_async_result_complete (res);
  g_object_unref (res);

  g_object_unref (self);
}

/* public methods */

EvdService *
evd_service_new (void)
{
  EvdService *self;

  self = g_object_new (EVD_TYPE_SERVICE, NULL);

  return self;
}

void
evd_service_set_tls_autostart (EvdService *self, gboolean autostart)
{
  g_return_if_fail (EVD_IS_SERVICE (self));

  self->priv->tls_autostart = autostart;
}

gboolean
evd_service_get_tls_autostart (EvdService *self)
{
  g_return_val_if_fail (EVD_IS_SERVICE (self), FALSE);

  return self->priv->tls_autostart;
}

void
evd_service_set_tls_credentials (EvdService        *self,
                                 EvdTlsCredentials *credentials)
{
  g_return_if_fail (EVD_IS_SERVICE (self));
  g_return_if_fail (EVD_IS_TLS_CREDENTIALS (credentials));

  if (self->priv->tls_cred != NULL)
    g_object_unref (self->priv->tls_cred);

  self->priv->tls_cred = credentials;
  g_object_ref (self->priv->tls_cred);
}

/**
 * evd_service_get_tls_credentials:
 *
 * Returns: (transfer none): The #EvdTlsCredentials object of this session
 **/
EvdTlsCredentials *
evd_service_get_tls_credentials (EvdService *self)
{
  g_return_val_if_fail (EVD_IS_SERVICE (self), NULL);

  if (self->priv->tls_cred == NULL)
    self->priv->tls_cred = evd_tls_credentials_new ();

  return self->priv->tls_cred;
}

void
evd_service_set_io_stream_type (EvdService *self, GType io_stream_type)
{
  g_return_if_fail (EVD_IS_SERVICE (self));
  g_return_if_fail (g_type_is_a (io_stream_type, EVD_TYPE_CONNECTION));

  self->priv->io_stream_type = io_stream_type;
}

void
evd_service_add_listener (EvdService  *self, EvdSocket *socket)
{
  g_return_if_fail (EVD_IS_SERVICE (self));
  g_return_if_fail (EVD_IS_SOCKET (socket));

  g_object_ref (socket);

  g_object_set (socket, "io-stream-type", self->priv->io_stream_type, NULL);

  g_hash_table_insert (self->priv->listeners,
                       (gpointer) socket,
                       (gpointer) socket);

  g_signal_connect (socket,
                    "new-connection",
                    G_CALLBACK (evd_service_listener_on_new_connection),
                    self);

  g_signal_connect (socket,
                    "close",
                    G_CALLBACK (evd_service_listener_on_close),
                    self);

  g_object_set_data (G_OBJECT (socket), "evd-service", self);
}

gboolean
evd_service_remove_listener (EvdService *self, EvdSocket *socket)
{
  g_return_val_if_fail (EVD_IS_SERVICE (self), FALSE);
  g_return_val_if_fail (EVD_IS_SOCKET (socket), FALSE);

  return g_hash_table_remove (self->priv->listeners, (gconstpointer) socket);
}

/**
 * evd_service_listen:
 * @cancellable: (allow-none):
 * @callback: (allow-none):
 * @user_data: (allow-none):
 **/
void
evd_service_listen (EvdService          *self,
                    const gchar         *address,
                    GCancellable        *cancellable,
                    GAsyncReadyCallback  callback,
                    gpointer             user_data)
{
  EvdSocket *socket;
  GSimpleAsyncResult *res;

  g_return_if_fail (EVD_IS_SERVICE (self));
  g_return_if_fail (address != NULL);

  socket = evd_socket_new ();

  res = g_simple_async_result_new (G_OBJECT (self),
                                   callback,
                                   user_data,
                                   evd_service_listen);

  evd_socket_listen (socket,
                     address,
                     cancellable,
                     evd_service_socket_on_listen,
                     res);
}

gboolean
evd_service_listen_finish (EvdService    *self,
                           GAsyncResult  *result,
                           GError       **error)
{
  g_return_val_if_fail (EVD_IS_SERVICE (self), FALSE);
  g_return_val_if_fail (g_simple_async_result_is_valid (result,
                                                        G_OBJECT (self),
                                                        evd_service_listen),
                        FALSE);

  return
    ! g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
                                             error);
}

void
evd_service_accept_connection (EvdService *self, EvdConnection *conn)
{
  gboolean *hint;

  g_return_if_fail (EVD_IS_SERVICE (self));
  g_return_if_fail (EVD_IS_CONNECTION (self));

  /* check that connection is being validated */
  hint = g_object_get_data (G_OBJECT (conn), VALIDATION_HINT_KEY);
  if (hint == NULL || ! *hint)
    return;

  /* remove validation hint */
  g_object_set_data (G_OBJECT (conn), VALIDATION_HINT_KEY, NULL);

  accept_connection_priv (self, conn);

  g_object_unref (conn);
}

void
evd_service_reject_connection (EvdService *self, EvdConnection *conn)
{
  gboolean *hint;

  g_return_if_fail (EVD_IS_SERVICE (self));
  g_return_if_fail (EVD_IS_CONNECTION (self));

  /* check that connection is being validated */
  hint = g_object_get_data (G_OBJECT (conn), VALIDATION_HINT_KEY);
  if (hint == NULL || ! *hint)
    return;

  /* remove validation hint */
  g_object_set_data (G_OBJECT (conn), VALIDATION_HINT_KEY, NULL);

  reject_connection_priv (self, conn);

  g_object_unref (conn);
}
