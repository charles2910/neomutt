/**
 * @file
 * Berkeley DB backend for the key/value Store
 *
 * @authors
 * Copyright (C) 2004 Thomas Glanzmann <sithglan@stud.uni-erlangen.de>
 * Copyright (C) 2004 Tobias Werth <sitowert@stud.uni-erlangen.de>
 * Copyright (C) 2004 Brian Fundakowski Feldman <green@FreeBSD.org>
 * Copyright (C) 2016 Pietro Cerutti <gahr@gahr.ch>
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
 * @page store_bdb Berkeley DB (BDB)
 *
 * Berkeley DB backend for the key/value Store.
 * https://en.wikipedia.org/wiki/Berkeley_DB
 */

#include "config.h"
#include <db.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include "mutt/lib.h"
#include "lib.h"

/**
 * struct StoreDbCtx - Berkeley DB context
 */
struct StoreDbCtx
{
  DB_ENV *env;
  DB *db;
  int fd;
  struct Buffer lockfile;
};

/**
 * bdb_sdata_free - Free Bdb Store Data
 * @param ptr Bdb Store Data to free
 */
static void bdb_sdata_free(struct StoreDbCtx **ptr)
{
  if (!ptr || !*ptr)
    return;

  struct StoreDbCtx *sdata = *ptr;
  buf_dealloc(&sdata->lockfile);

  FREE(ptr);
}

/**
 * bdb_sdata_new - Create new Bdb Store Data
 * @retval ptr New Bdb Store Data
 */
static struct StoreDbCtx *bdb_sdata_new(void)
{
  struct StoreDbCtx *sdata = mutt_mem_calloc(1, sizeof(struct StoreDbCtx));

  sdata->lockfile = buf_make(128);

  return sdata;
}

/**
 * dbt_init - Initialise a BDB thing
 * @param dbt  Thing to initialise
 * @param data ID string to associate
 * @param len  Length of ID string
 */
static void dbt_init(DBT *dbt, void *data, size_t len)
{
  dbt->data = data;
  dbt->size = len;
  dbt->ulen = len;
  dbt->dlen = 0;
  dbt->doff = 0;
  dbt->flags = DB_DBT_USERMEM;
}

/**
 * dbt_empty_init - Initialise an empty BDB thing
 * @param dbt  Thing to initialise
 */
static void dbt_empty_init(DBT *dbt)
{
  dbt->data = NULL;
  dbt->size = 0;
  dbt->ulen = 0;
  dbt->dlen = 0;
  dbt->doff = 0;
  dbt->flags = 0;
}

/**
 * store_bdb_open - Implements StoreOps::open() - @ingroup store_open
 */
static StoreHandle *store_bdb_open(const char *path)
{
  if (!path)
    return NULL;

  struct StoreDbCtx *ctx = bdb_sdata_new();

  const int pagesize = 512;

  buf_printf(&ctx->lockfile, "%s-lock-hack", path);

  ctx->fd = open(buf_string(&ctx->lockfile), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
  if (ctx->fd < 0)
  {
    FREE(&ctx);
    return NULL;
  }

  if (mutt_file_lock(ctx->fd, true, true))
    goto fail_close;

  int rc = db_env_create(&ctx->env, 0);
  if (rc)
    goto fail_unlock;

  rc = (*ctx->env->open)(ctx->env, NULL, DB_INIT_MPOOL | DB_CREATE | DB_PRIVATE, 0600);
  if (rc)
    goto fail_env;

  ctx->db = NULL;
  rc = db_create(&ctx->db, ctx->env, 0);
  if (rc)
    goto fail_env;

  uint32_t createflags = DB_CREATE;
  struct stat st = { 0 };

  if ((stat(path, &st) != 0) && (errno == ENOENT))
  {
    createflags |= DB_EXCL;
    ctx->db->set_pagesize(ctx->db, pagesize);
  }

  rc = (*ctx->db->open)(ctx->db, NULL, path, NULL, DB_BTREE, createflags, 0600);
  if (rc)
    goto fail_db;

  // Return an opaque pointer
  return (StoreHandle *) ctx;

fail_db:
  ctx->db->close(ctx->db, 0);
fail_env:
  ctx->env->close(ctx->env, 0);
fail_unlock:
  mutt_file_unlock(ctx->fd);
fail_close:
  close(ctx->fd);
  unlink(buf_string(&ctx->lockfile));
  bdb_sdata_free(&ctx);

  return NULL;
}

/**
 * store_bdb_fetch - Implements StoreOps::fetch() - @ingroup store_fetch
 */
static StoreHandle *store_bdb_fetch(StoreHandle *store, const char *key,
                                    size_t klen, size_t *vlen)
{
  if (!store)
    return NULL;

  // Decloak an opaque pointer
  struct StoreDbCtx *ctx = store;

  DBT dkey = { 0 };
  DBT data = { 0 };

  dbt_init(&dkey, (void *) key, klen);
  dbt_empty_init(&data);
  data.flags = DB_DBT_MALLOC;

  ctx->db->get(ctx->db, NULL, &dkey, &data, 0);

  *vlen = data.size;
  return data.data;
}

/**
 * store_bdb_free - Implements StoreOps::free() - @ingroup store_free
 */
static void store_bdb_free(StoreHandle *store, void **ptr)
{
  FREE(ptr);
}

/**
 * store_bdb_store - Implements StoreOps::store() - @ingroup store_store
 */
static int store_bdb_store(StoreHandle *store, const char *key, size_t klen,
                           void *value, size_t vlen)
{
  if (!store)
    return -1;

  // Decloak an opaque pointer
  struct StoreDbCtx *ctx = store;

  DBT dkey = { 0 };
  DBT databuf = { 0 };

  dbt_init(&dkey, (void *) key, klen);
  dbt_empty_init(&databuf);
  databuf.flags = DB_DBT_USERMEM;
  databuf.data = value;
  databuf.size = vlen;
  databuf.ulen = vlen;

  return ctx->db->put(ctx->db, NULL, &dkey, &databuf, 0);
}

/**
 * store_bdb_delete_record - Implements StoreOps::delete_record() - @ingroup store_delete_record
 */
static int store_bdb_delete_record(StoreHandle *store, const char *key, size_t klen)
{
  if (!store)
    return -1;

  // Decloak an opaque pointer
  struct StoreDbCtx *ctx = store;

  DBT dkey = { 0 };
  dbt_init(&dkey, (void *) key, klen);
  return ctx->db->del(ctx->db, NULL, &dkey, 0);
}

/**
 * store_bdb_close - Implements StoreOps::close() - @ingroup store_close
 */
static void store_bdb_close(StoreHandle **ptr)
{
  if (!ptr || !*ptr)
    return;

  // Decloak an opaque pointer
  struct StoreDbCtx *db = *ptr;

  db->db->close(db->db, 0);
  db->env->close(db->env, 0);
  mutt_file_unlock(db->fd);
  close(db->fd);
  unlink(buf_string(&db->lockfile));

  bdb_sdata_free((struct StoreDbCtx **) ptr);
}

/**
 * store_bdb_version - Implements StoreOps::version() - @ingroup store_version
 */
static const char *store_bdb_version(void)
{
  return DB_VERSION_STRING;
}

STORE_BACKEND_OPS(bdb)
