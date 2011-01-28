/*
 * evd-tls-session.c
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

#include <errno.h>
#include <string.h>

#include "evd-tls-session.h"

#include "evd-error.h"
#include "evd-tls-common.h"
#include "evd-tls-credentials.h"
#include "evd-tls-certificate.h"

#define EVD_TLS_SESSION_DEFAULT_PRIORITY "NORMAL"

G_DEFINE_TYPE (EvdTlsSession, evd_tls_session, G_TYPE_OBJECT)

#define EVD_TLS_SESSION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), \
                                          EVD_TYPE_TLS_SESSION, \
                                          EvdTlsSessionPrivate))

/* private data */
struct _EvdTlsSessionPrivate
{
  gnutls_session_t session;
  EvdTlsCredentials *cred;

  EvdTlsMode mode;

  EvdTlsSessionPullFunc pull_func;
  EvdTlsSessionPushFunc push_func;
  gpointer              pull_user_data;
  gpointer              push_user_data;

  gchar *priority;

  gulong cred_ready_sig_id;

  gboolean cred_bound;

  gboolean require_peer_cert;

  gboolean write_shutdown;

  gchar *server_name;
};


/* properties */
enum
{
  PROP_0,
  PROP_CREDENTIALS,
  PROP_MODE,
  PROP_PRIORITY,
  PROP_REQUIRE_PEER_CERT
};

static void     evd_tls_session_class_init         (EvdTlsSessionClass *class);
static void     evd_tls_session_init               (EvdTlsSession *self);

static void     evd_tls_session_finalize           (GObject *obj);
static void     evd_tls_session_dispose            (GObject *obj);

static void     evd_tls_session_set_property       (GObject      *obj,
                                                    guint         prop_id,
                                                    const GValue *value,
                                                    GParamSpec   *pspec);
static void     evd_tls_session_get_property       (GObject    *obj,
                                                    guint       prop_id,
                                                    GValue     *value,
                                                    GParamSpec *pspec);

static void
evd_tls_session_class_init (EvdTlsSessionClass *class)
{
  GObjectClass *obj_class;

  obj_class = G_OBJECT_CLASS (class);

  obj_class->dispose = evd_tls_session_dispose;
  obj_class->finalize = evd_tls_session_finalize;
  obj_class->get_property = evd_tls_session_get_property;
  obj_class->set_property = evd_tls_session_set_property;

  g_object_class_install_property (obj_class, PROP_CREDENTIALS,
                                   g_param_spec_object ("credentials",
                                                        "The SSL/TLS session's credentials",
                                                        "The certificate credentials object to use by this SSL/TLS session",
                                                        EVD_TYPE_TLS_CREDENTIALS,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_MODE,
                                   g_param_spec_uint ("mode",
                                                      "SSL/TLS session mode",
                                                      "The SSL/TLS session's mode of operation: client or server",
                                                      EVD_TLS_MODE_SERVER,
                                                      EVD_TLS_MODE_CLIENT,
                                                      EVD_TLS_MODE_SERVER,
                                                      G_PARAM_READWRITE |
                                                      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_PRIORITY,
                                   g_param_spec_string ("priority",
                                                        "Priority string to use in this session",
                                                        "Gets/sets the priorities to use on the ciphers, key exchange methods, macs and compression methods",
                                                        EVD_TLS_SESSION_DEFAULT_PRIORITY,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (obj_class, PROP_REQUIRE_PEER_CERT,
                                   g_param_spec_boolean ("require-peer-cert",
                                                         "Require peer certificate",
                                                         "Controls whether a peer certificate will be requested during handshake",
                                                         FALSE,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (obj_class, sizeof (EvdTlsSessionPrivate));
}

static void
evd_tls_session_init (EvdTlsSession *self)
{
  EvdTlsSessionPrivate *priv;

  priv = EVD_TLS_SESSION_GET_PRIVATE (self);
  self->priv = priv;

  priv->session = NULL;
  priv->cred = NULL;

  priv->mode = EVD_TLS_MODE_SERVER;

  priv->pull_func = NULL;
  priv->push_func = NULL;
  priv->pull_user_data = NULL;
  priv->push_user_data = NULL;

  priv->priority = g_strdup (EVD_TLS_SESSION_DEFAULT_PRIORITY);

  priv->cred_ready_sig_id = 0;

  priv->cred_bound = FALSE;

  priv->require_peer_cert = FALSE;

  priv->write_shutdown = FALSE;

  priv->server_name = NULL;
}

static void
evd_tls_session_dispose (GObject *obj)
{
  EvdTlsSession *self = EVD_TLS_SESSION (obj);

  if (self->priv->cred != NULL)
    {
      g_object_unref (self->priv->cred);
      self->priv->cred = NULL;
    }

  G_OBJECT_CLASS (evd_tls_session_parent_class)->dispose (obj);
}

static void
evd_tls_session_finalize (GObject *obj)
{
  EvdTlsSession *self = EVD_TLS_SESSION (obj);

  if (self->priv->session != NULL)
    gnutls_deinit (self->priv->session);

  if (self->priv->priority != NULL)
    g_free (self->priv->priority);

  if (self->priv->server_name != NULL)
    g_free (self->priv->server_name);

  G_OBJECT_CLASS (evd_tls_session_parent_class)->finalize (obj);
}

static void
evd_tls_session_set_property (GObject      *obj,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  EvdTlsSession *self;

  self = EVD_TLS_SESSION (obj);

  switch (prop_id)
    {
    case PROP_CREDENTIALS:
      evd_tls_session_set_credentials (self, g_value_get_object (value));
      break;

    case PROP_MODE:
      self->priv->mode = g_value_get_uint (value);
      break;

    case PROP_PRIORITY:
      if (self->priv->priority != NULL)
        g_free (self->priv->priority);
      self->priv->priority = g_strdup (g_value_get_string (value));
      break;

    case PROP_REQUIRE_PEER_CERT:
      self->priv->require_peer_cert = g_value_get_boolean (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static void
evd_tls_session_get_property (GObject    *obj,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  EvdTlsSession *self;

  self = EVD_TLS_SESSION (obj);

  switch (prop_id)
    {
    case PROP_CREDENTIALS:
      g_value_set_object (value, evd_tls_session_get_credentials (self));
      break;

    case PROP_MODE:
      g_value_set_uint (value, self->priv->mode);
      break;

    case PROP_PRIORITY:
      g_value_set_string (value, self->priv->priority);
      break;

    case PROP_REQUIRE_PEER_CERT:
      g_value_set_boolean (value, self->priv->require_peer_cert);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
      break;
    }
}

static gssize
evd_tls_session_push (gnutls_transport_ptr_t  ptr,
                      const void             *buf,
                      gsize                   size)
{
  EvdTlsSession *self = EVD_TLS_SESSION (ptr);
  gssize res;
  GError *error = NULL;

  res = self->priv->push_func (self,
                               buf,
                               size,
                               self->priv->push_user_data,
                               &error);

  //  g_debug ("TLS pushed %d out of %d", res, size);

  if (res < 0)
    {
      if (! self->priv->write_shutdown || error->code != G_IO_ERROR_CLOSED)
        {
          /* @TODO: Handle transport error */
          g_debug ("TLS session transport error during push: %s", error->message);
          g_error_free (error);
        }
    }

  return res;
}

static gssize
evd_tls_session_pull (gnutls_transport_ptr_t  ptr,
                      void                   *buf,
                      gsize                   size)
{
  EvdTlsSession *self = EVD_TLS_SESSION (ptr);
  gssize res;
  GError *error = NULL;

  res = self->priv->pull_func (self,
                               buf,
                               size,
                               self->priv->pull_user_data,
                               &error);

  //  g_debug ("TLS pulled %d out of %d", res, size);

  if (res < 0)
    {
      /* @TODO: handle transport error */
      g_debug ("TLS transport error during pull: %s", error->message);
      g_error_free (error);
    }
  else if (res == 0)
    {
      gnutls_transport_set_errno (self->priv->session, EAGAIN);
      res = -1;
    }

  return res;
}

static gint
evd_tls_session_handshake_internal (EvdTlsSession  *self,
                                    GError        **error)
{
  gint err_code;

  err_code = gnutls_handshake (self->priv->session);
  if (err_code == GNUTLS_E_SUCCESS)
    {
      return 1;
    }
  else if (gnutls_error_is_fatal (err_code) == 1)
    {
      evd_tls_build_error (err_code, error);

      return -1;
    }
  else
    {
      return 0;
    }
}

static gboolean
evd_tls_session_bind_credentials (EvdTlsSession      *self,
                                  EvdTlsCredentials  *cred,
                                  GError            **error)
{
  gpointer cred_internal;
  gint err_code;

  /* set credentials */
  cred_internal = evd_tls_credentials_get_credentials (cred);

  if (cred_internal == NULL)
    {
      g_set_error_literal (error,
                           EVD_ERROR,
                           EVD_ERROR_NOT_INITIALIZED,
                           "TLS credentials not initialized");

      return FALSE;
    }
  else if (evd_tls_credentials_get_anonymous (cred))
    {
      if (self->priv->mode == EVD_TLS_MODE_SERVER)
        err_code =
          gnutls_credentials_set (self->priv->session,
                                  GNUTLS_CRD_ANON,
                                  (gnutls_anon_server_credentials_t) cred_internal);
      else
        err_code =
          gnutls_credentials_set (self->priv->session,
                                  GNUTLS_CRD_ANON,
                                  (gnutls_anon_client_credentials_t) cred_internal);
    }
  else
    {
      err_code =
        gnutls_credentials_set (self->priv->session,
                                GNUTLS_CRD_CERTIFICATE,
                                (gnutls_certificate_credentials_t) cred_internal);
    }

  if (err_code != GNUTLS_E_SUCCESS)
    {
      evd_tls_build_error (err_code, error);

      return FALSE;
    }
  else
    {
      self->priv->cred_bound = TRUE;

      return TRUE;
    }
}

static void
evd_tls_session_on_credentials_ready (EvdTlsCredentials *cred,
                                      gpointer           user_data)
{
  EvdTlsSession *self = EVD_TLS_SESSION (user_data);
  GError *error = NULL;

  if (! evd_tls_session_bind_credentials (self, cred, &error))
    {
      /* TODO: handle error */
      g_debug ("error binding credentials: %s", error->message);
      g_error_free (error);
    }
  else if (evd_tls_session_handshake_internal (self, &error) < 0)
    {
      /* TODO: raise error asynchronously, by firing 'error' signal */
      g_debug ("handshake error!: %s", error->message);
      g_error_free (error);
    }
}

static gboolean
evd_tls_session_shutdown (EvdTlsSession           *self,
                          gnutls_close_request_t   how,
                          GError                 **error)
{
  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), FALSE);

  if (self->priv->session != NULL)
    {
      gint err_code;

      err_code = gnutls_bye (self->priv->session, how);
      if (err_code < 0 && gnutls_error_is_fatal (err_code) != 0)
        {
          evd_tls_build_error (err_code, error);
          return FALSE;
        }

      self->priv->write_shutdown = TRUE;
    }

  return TRUE;
}

static gboolean
evd_tls_session_check_initialized (EvdTlsSession *self, GError **error)
{
  if (self->priv->session == NULL)
    {
      g_set_error_literal (error,
                           EVD_ERROR,
                           EVD_ERROR_NOT_INITIALIZED,
                           "SSL/TLS session not yet initialized");

      return FALSE;
    }
  else
    {
      return TRUE;
    }
}

static gboolean
evd_tls_session_set_server_name_internal (EvdTlsSession  *self,
                                          GError        **error)
{
  if (self->priv->session != NULL
      && self->priv->server_name != NULL
      && self->priv->mode == EVD_TLS_MODE_CLIENT)
    {
      gint err_code;

      err_code = gnutls_server_name_set (self->priv->session,
                                         GNUTLS_NAME_DNS,
                                         self->priv->server_name,
                                         strlen (self->priv->server_name));

      if (err_code != GNUTLS_E_SUCCESS)
        {
          evd_tls_build_error (err_code, error);

          return FALSE;
        }
    }

  return TRUE;
}

/* public methods */

EvdTlsSession *
evd_tls_session_new (void)
{
  EvdTlsSession *self;

  self = g_object_new (EVD_TYPE_TLS_SESSION, NULL);

  return self;
}

void
evd_tls_session_set_credentials (EvdTlsSession     *self,
                                 EvdTlsCredentials *credentials)
{
  g_return_if_fail (EVD_IS_TLS_SESSION (self));
  g_return_if_fail (EVD_IS_TLS_CREDENTIALS (credentials));

  if (self->priv->cred != NULL)
    {
      if (self->priv->cred_ready_sig_id != 0)
        {
          g_signal_handler_disconnect (self->priv->cred,
                                       self->priv->cred_ready_sig_id);

          self->priv->cred_ready_sig_id = 0;
        }

      g_object_unref (self->priv->cred);
    }

  self->priv->cred = credentials;
  g_object_ref (self->priv->cred);
}

/**
 * evd_tls_session_get_credentials:
 *
 * Returns: (transfer none): The #EvdTlsCredentials object of this session
 **/
EvdTlsCredentials *
evd_tls_session_get_credentials (EvdTlsSession *self)
{
  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), NULL);

  if (self->priv->cred == NULL)
    {
      self->priv->cred = evd_tls_credentials_new ();
      g_object_ref_sink (self->priv->cred);
    }

  return self->priv->cred;
}

void
evd_tls_session_set_transport_pull_func (EvdTlsSession         *self,
                                         EvdTlsSessionPullFunc  func,
                                         gpointer               user_data)
{
  g_return_if_fail (EVD_IS_TLS_SESSION (self));
  g_return_if_fail (func != NULL);

  self->priv->pull_func = func;
  self->priv->pull_user_data = user_data;
}

void
evd_tls_session_set_transport_push_func (EvdTlsSession         *self,
                                         EvdTlsSessionPushFunc  func,
                                         gpointer               user_data)
{
  g_return_if_fail (EVD_IS_TLS_SESSION (self));
  g_return_if_fail (func != NULL);

  self->priv->push_func = func;
  self->priv->push_user_data = user_data;
}

gint
evd_tls_session_handshake (EvdTlsSession  *self,
                           GError        **error)
{
  EvdTlsCredentials *cred;
  gint err_code;

  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), FALSE);

  if (self->priv->session == NULL)
    {
      err_code = gnutls_init (&self->priv->session, self->priv->mode);
      if (err_code != GNUTLS_E_SUCCESS)
        {
          evd_tls_build_error (err_code, error);
          return -1;
        }
      else
        {
          if (! evd_tls_session_set_server_name_internal (self, error))
            return -1;

          err_code = gnutls_priority_set_direct (self->priv->session,
                                                 self->priv->priority,
                                                 NULL);

          if (err_code != GNUTLS_E_SUCCESS)
            {
              evd_tls_build_error (err_code, error);
              return -1;
            }

          if (self->priv->require_peer_cert &&
              self->priv->mode == EVD_TLS_MODE_SERVER)
            {
              gnutls_certificate_server_set_request (self->priv->session,
                                                     GNUTLS_CERT_REQUEST);
            }

          gnutls_transport_set_ptr2 (self->priv->session, self, self);
          gnutls_transport_set_push_function (self->priv->session,
                                              evd_tls_session_push);
          gnutls_transport_set_pull_function (self->priv->session,
                                              evd_tls_session_pull);
          gnutls_transport_set_lowat (self->priv->session, 0);

          cred = evd_tls_session_get_credentials (self);
          if (! evd_tls_credentials_ready (cred))
            {
              if (self->priv->cred_ready_sig_id == 0)
                self->priv->cred_ready_sig_id =
                  g_signal_connect (cred,
                                    "ready",
                                    G_CALLBACK (evd_tls_session_on_credentials_ready),
                                    self);

              evd_tls_credentials_prepare (cred, self->priv->mode, error);

              return 0;
            }
          else
            {
              if (! evd_tls_session_bind_credentials (self, cred, error))
                return -1;
            }
        }
    }

  if (self->priv->cred_bound)
    return evd_tls_session_handshake_internal (self, error);
  else
    return 0;
}

gssize
evd_tls_session_read (EvdTlsSession  *self,
                      gchar          *buffer,
                      gsize           size,
                      GError        **error)
{
  gssize result;

  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), -1);
  g_return_val_if_fail (size > 0, -1);
  g_return_val_if_fail (buffer != NULL, -1);

  result = gnutls_record_recv (self->priv->session, buffer, size);

  //  g_debug ("record_recv result: %d", result);
  if (result == 0)
    {
      /* @TODO: EOF condition, emit 'close' signal */
    }
  else if (result < 0)
    {
      if (gnutls_error_is_fatal (result) != 0)
        {
          evd_tls_build_error (result, error);
          result = -1;
        }
      else
        {
          result = 0;
        }
    }

  return result;
}

gssize
evd_tls_session_write (EvdTlsSession  *self,
                       const gchar    *buffer,
                       gsize           size,
                       GError        **error)
{
  gssize result;

  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), -1);
  g_return_val_if_fail (size > 0, -1);
  g_return_val_if_fail (buffer != NULL, -1);

  result = gnutls_record_send (self->priv->session, buffer, size);

  //  g_debug ("record_send result: %d", result);

  if (result < 0)
    {
      if (gnutls_error_is_fatal (result) != 0)
        {
          evd_tls_build_error (result, error);
          result = -1;
        }
      else
        {
          result = 0;
        }
    }

  return result;
}

GIOCondition
evd_tls_session_get_direction (EvdTlsSession *self)
{
  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), 0);

  if (self->priv->session == NULL)
    return 0;
  else
    if (gnutls_record_get_direction (self->priv->session) == 0)
      return G_IO_IN;
    else
      return G_IO_OUT;
}

gboolean
evd_tls_session_close (EvdTlsSession *self, GError **error)
{
  return evd_tls_session_shutdown (self, GNUTLS_SHUT_RDWR, error);
}

gboolean
evd_tls_session_shutdown_write (EvdTlsSession *self, GError **error)
{
  return evd_tls_session_shutdown (self, GNUTLS_SHUT_WR, error);
}

void
evd_tls_session_copy_properties (EvdTlsSession *self,
                                 EvdTlsSession *target)
{
  g_return_if_fail (EVD_IS_TLS_SESSION (self));
  g_return_if_fail (self != target);

  g_object_set (target,
                "credentials", evd_tls_session_get_credentials (self),
                "priority", self->priv->priority,
                "require-peer-cert", self->priv->require_peer_cert,
                NULL);
}

/**
 * evd_tls_session_get_peer_certificates:
 * @self:
 * @error:
 *
 * Returns: (transfer full) (element-type Evd.TlsCertificate): The list of certificates
 *          as sent by the peer.
 **/
GList *
evd_tls_session_get_peer_certificates (EvdTlsSession *self, GError **error)
{
  GList *list = NULL;

  const gnutls_datum_t *raw_certs_list;
  guint raw_certs_len;

  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), NULL);

  if (! evd_tls_session_check_initialized (self, error))
    return NULL;

  raw_certs_list = gnutls_certificate_get_peers (self->priv->session, &raw_certs_len);
  if (raw_certs_list != NULL)
    {
      guint i;
      EvdTlsCertificate *cert;

      for (i=0; i<raw_certs_len; i++)
        {
          cert = evd_tls_certificate_new ();

          if (! evd_tls_certificate_import (cert,
                                            (gchar *) raw_certs_list[i].data,
                                            raw_certs_list[i].size,
                                            error))
            {
              evd_tls_free_certificates (list);

              return NULL;
            }
          else
            {
              list = g_list_append (list, (gpointer) cert);
            }
        }
    }

  return list;
}

gint
evd_tls_session_verify_peer (EvdTlsSession  *self,
                             guint           flags,
                             GError        **error)
{
  gint result = EVD_TLS_VERIFY_STATE_OK;
  guint status;
  gint err_code;
  GList *peer_certs;

  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), -1);

  if (! evd_tls_session_check_initialized (self, error))
    return -1;

  /* basic verification */
  err_code = gnutls_certificate_verify_peers2 (self->priv->session, &status);
  if (err_code != GNUTLS_E_SUCCESS)
    {
      if (err_code != GNUTLS_E_NO_CERTIFICATE_FOUND)
        {
          evd_tls_build_error (err_code, error);

          return -1;
        }
      else
        {
          result |= EVD_TLS_VERIFY_STATE_NO_CERT;
        }
    }
  else
    {
      if (status & GNUTLS_CERT_INVALID)
        result |= EVD_TLS_VERIFY_STATE_INVALID;
      if (status & GNUTLS_CERT_REVOKED)
        result |= EVD_TLS_VERIFY_STATE_REVOKED;
      if (status & GNUTLS_CERT_SIGNER_NOT_FOUND)
        result |= EVD_TLS_VERIFY_STATE_SIGNER_NOT_FOUND;
      if (status & GNUTLS_CERT_SIGNER_NOT_CA)
        result |= EVD_TLS_VERIFY_STATE_SIGNER_NOT_CA;
      if (status & GNUTLS_CERT_INSECURE_ALGORITHM)
        result |= EVD_TLS_VERIFY_STATE_INSECURE_ALG;
    }

  /* check each peer certificate individually */
  peer_certs = evd_tls_session_get_peer_certificates (self, error);
  if (peer_certs != NULL)
    {
      GList *node;
      EvdTlsCertificate *cert;
      gint cert_result;

      node = peer_certs;
      do
        {
          cert = EVD_TLS_CERTIFICATE (node->data);

          cert_result = evd_tls_certificate_verify_validity (cert, error);
          if (cert_result < 0)
            break;
          else
            result |= cert_result;

          node = node->next;
        }
      while (node != NULL);

      evd_tls_free_certificates (peer_certs);
    }

  return result;
}

void
evd_tls_session_reset (EvdTlsSession *self)
{
  g_return_if_fail (EVD_IS_TLS_SESSION (self));

  if (self->priv->session != NULL)
    {
      if (self->priv->cred_ready_sig_id != 0)
        {
          g_signal_handler_disconnect (self->priv->cred,
                                       self->priv->cred_ready_sig_id);
          self->priv->cred_ready_sig_id = 0;
        }

      self->priv->cred_bound = FALSE;

      gnutls_deinit (self->priv->session);
      self->priv->session = NULL;
    }

  if (self->priv->server_name != NULL)
    {
      g_free (self->priv->server_name);
      self->priv->server_name = NULL;
    }
}

gboolean
evd_tls_session_set_server_name (EvdTlsSession  *self,
                                 const gchar    *server_name,
                                 GError        **error)
{
  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), FALSE);

  if (self->priv->server_name != NULL)
    {
      g_free (self->priv->server_name);
      self->priv->server_name = NULL;
    }

  if (server_name != NULL)
    self->priv->server_name = g_strdup (server_name);

  return evd_tls_session_set_server_name_internal (self, error);
}

const gchar *
evd_tls_session_get_server_name (EvdTlsSession *self)
{
  g_return_val_if_fail (EVD_IS_TLS_SESSION (self), NULL);

  if (self->priv->mode == EVD_TLS_MODE_SERVER
      && self->priv->server_name == NULL
      && self->priv->session != NULL)
    {
      gint err;
      gsize len = 16;
      gchar buf[16] = {0, };
      guint type;
      guint index = 0;

      do
        {
          err = gnutls_server_name_get (self->priv->session,
                                        buf,
                                        &len,
                                        &type,
                                        index);

          if (err == GNUTLS_E_SUCCESS && type == GNUTLS_NAME_DNS)
            {
              self->priv->server_name = g_new0 (gchar, len);

              if (err == GNUTLS_E_SHORT_MEMORY_BUFFER)
                {
                  err = gnutls_server_name_get (self->priv->session,
                                                self->priv->server_name,
                                                &len,
                                                &type,
                                                index);
                }
              else
                {
                  memmove (self->priv->server_name, buf, len);
                }
            }
          else
            {
              index++;
            }
        }
      while (err != GNUTLS_E_REQUESTED_DATA_NOT_AVAILABLE
             && self->priv->server_name == NULL);
    }

  return self->priv->server_name;
}
