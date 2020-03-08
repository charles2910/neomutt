/**
 * @file
 * Connection Library
 *
 * @authors
 * Copyright (C) 2017 Richard Russon <rich@flatcap.org>
 *
 * @copyright
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @page conn CONN: Network connections and their encryption
 *
 * Manage external connections.
 *
 * | File                | Description              |
 * | :------------------ | :----------------------- |
 * | conn/connaccount.c  | @subpage conn_account    |
 * | conn/conn_globals.c | @subpage conn_globals    |
 * | conn/getdomain.c    | @subpage conn_getdomain  |
 * | conn/gnutls.c       | @subpage conn_gnutls     |
 * | conn/openssl.c      | @subpage conn_openssl    |
 * | conn/raw.c          | @subpage conn_raw        |
 * | conn/sasl.c         | @subpage conn_sasl       |
 * | conn/sasl_plain.c   | @subpage conn_sasl_plain |
 * | conn/socket.c       | @subpage conn_socket     |
 * | conn/tunnel.c       | @subpage conn_tunnel     |
 */

#ifndef MUTT_CONN_LIB_H
#define MUTT_CONN_LIB_H

#include "config.h"
#include <stdio.h>
// IWYU pragma: begin_exports
#include "conn_globals.h"
#include "connaccount.h"
#include "connection.h"
#include "sasl_plain.h"
#include "socket.h"
#ifdef USE_SASL
#include "sasl.h"
#endif
// IWYU pragma: end_exports

#ifdef USE_SSL
int mutt_ssl_starttls(struct Connection *conn);
#endif

int getdnsdomainname(char *buf, size_t buflen);

#endif /* MUTT_CONN_LIB_H */
