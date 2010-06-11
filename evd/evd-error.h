/*
 * evd-error.h
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

#ifndef __EVD_ERROR_H__
#define __EVD_ERROR_H__

#define EVD_ERROR_DOMAIN_STR "org.eventdance.lib.Errors"
#define EVD_ERROR            g_quark_from_string (EVD_ERROR_DOMAIN_STR)

#define EVD_TLS_ERROR_DOMAIN_STR "org.eventdance.lib.TlsErrors"
#define EVD_TLS_ERROR            g_quark_from_string (EVD_TLS_ERROR_DOMAIN_STR)

typedef enum
{
  EVD_ERROR_NONE,
  EVD_ERROR_UNKNOWN,
  EVD_ERROR_EPOLL_ADD,
  EVD_ERROR_EPOLL_DEL,
  EVD_ERROR_EPOLL_MOD,
  EVD_ERROR_NOT_CONNECTING,
  EVD_ERROR_NOT_CONNECTED,
  EVD_ERROR_CLOSED,
  EVD_ERROR_REFUSED,
  EVD_ERROR_SOCKET_ACCEPT,
  EVD_ERROR_ALREADY_ACTIVE,
  EVD_ERROR_INVALID_DATA,
  EVD_ERROR_NOT_INITIALIZED,
  EVD_ERROR_BUFFER_FULL,
  EVD_ERROR_INVALID_ADDRESS,
  EVD_ERROR_RESOLVE_ADDRESS,
  EVD_ERROR_NOT_READABLE,
  EVD_ERROR_NOT_WRITABLE,

  /* padding for future expansiion */
  EVD_ERROR_PADDING0,
  EVD_ERROR_PADDING1,
  EVD_ERROR_PADDING2,
  EVD_ERROR_PADDING3,
  EVD_ERROR_PADDING4,
  EVD_ERROR_PADDING5,
  EVD_ERROR_PADDING6,
  EVD_ERROR_PADDING7,
  EVD_ERROR_PADDING8,
  EVD_ERROR_PADDING9,
} EvdErrorEnum;

#endif /* __EVD_ERROR_H__ */
