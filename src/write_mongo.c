/**
 * collectd - src/write_mongo.c
 * Copyright (C) 2010  Florian Forster, Akkarit Sangpetch
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian Forster <ff at octo.it>
 *   Akkarit Sangpetch <asangpet@andrew.cmu.edu>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "configfile.h"

#include <pthread.h>
#include "libmongo/bson.h"
#include "libmongo/mongo.h"

struct wm_node_s
{
  char name[DATA_MAX_NAME_LEN];

  char *host;
  int port;
  int timeout;

  int connected;

  mongo_connection conn[1];
  mongo_connection_options opts[1];
  pthread_mutex_t lock;
};
typedef struct wm_node_s wm_node_t;

/*
 * Functions
 */
static int wm_write (const data_set_t *ds, /* {{{ */
    const value_list_t *vl,
    user_data_t *ud)
{
  wm_node_t *node = ud->data;
  char collection_name[512];
  int status;
  int i;
  bson record[1];
  bson_buffer record_buf[1];

  ssnprintf(collection_name, sizeof (collection_name), "collectd.%s", vl->plugin);

  bson_buffer_init(record_buf);
  bson_append_time_t(record_buf,"ts",vl->time);
  bson_append_string(record_buf,"h",vl->host);
  bson_append_string(record_buf,"i",vl->plugin_instance);
  bson_append_string(record_buf,"t",vl->plugin_instance);
  bson_append_string(record_buf,"ti",vl->plugin_instance);

  if (ds->ds_num == 1) {
    if (ds->ds[0].type == DS_TYPE_COUNTER)
      bson_append_long(record_buf, "v", vl->values[0].counter);
    else if (ds->ds[0].type == DS_TYPE_GAUGE)
      bson_append_double(record_buf, "v", vl->values[0].gauge);
    else if (ds->ds[0].type == DS_TYPE_DERIVE)
      bson_append_long(record_buf, "v", vl->values[0].derive);
    else if (ds->ds[0].type == DS_TYPE_ABSOLUTE)
      bson_append_long(record_buf, "v", vl->values[0].absolute);
    else
      assert (23 == 42);
  } else {
    bson_append_start_object(record_buf,"v");
    for (i = 0; i < ds->ds_num; i++)
    {
      if (ds->ds[i].type == DS_TYPE_COUNTER)
        bson_append_long(record_buf, ds->ds[i].name, vl->values[i].counter);
      else if (ds->ds[i].type == DS_TYPE_GAUGE)
        bson_append_double(record_buf, ds->ds[i].name, vl->values[i].gauge);
      else if (ds->ds[i].type == DS_TYPE_DERIVE)
        bson_append_long(record_buf, ds->ds[i].name, vl->values[i].derive);
      else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
        bson_append_long(record_buf, ds->ds[i].name, vl->values[i].absolute);
      else
        assert (23 == 42);
    }
    bson_append_finish_object(record_buf);
  }
  bson_from_buffer(record,record_buf);

  pthread_mutex_lock (&node->lock);

  if (node->connected == 0)
  {
    strcpy(node->opts->host, node->host);
    node->opts->port = node->port;

    status = mongo_connect(node->conn,node->opts);
    if (status!=mongo_conn_success) {
      ERROR ("write_mongo plugin: Connecting to host \"%s\" (port %i) failed.",
          (node->host != NULL) ? node->host : "localhost",
          (node->port != 0) ? node->port : 6379);
      pthread_mutex_unlock (&node->lock);
      return (-1);
    } else {
      node->connected = 1;
    }
  }

  /* Assert if the connection has been established */
  assert (node->connected == 1);

  mongo_insert(node->conn,collection_name,record);

  pthread_mutex_unlock (&node->lock);

  return (0);
} /* }}} int wm_write */

static void wm_config_free (void *ptr) /* {{{ */
{
  wm_node_t *node = ptr;

  if (node == NULL)
    return;

  if (node->connected != 0)
  {
    mongo_destroy(node->conn);
    node->connected = 0;
  }

  sfree (node->host);
  sfree (node);
} /* }}} void wm_config_free */

static int wm_config_node (oconfig_item_t *ci) /* {{{ */
{
  wm_node_t *node;
  int status;
  int i;

  node = malloc (sizeof (*node));
  if (node == NULL)
    return (ENOMEM);
  memset (node, 0, sizeof (*node));
  node->host = NULL;
  node->port = 0;
  node->timeout = 1000;
  node->connected = 0;
  pthread_mutex_init (&node->lock, /* attr = */ NULL);

  status = cf_util_get_string_buffer (ci, node->name, sizeof (node->name));

  if (status != 0)
  {
    sfree (node);
    return (status);
  }

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Host", child->key) == 0)
      status = cf_util_get_string (child, &node->host);
    else if (strcasecmp ("Port", child->key) == 0)
    {
      status = cf_util_get_port_number (child);
      if (status > 0)
      {
        node->port = status;
        status = 0;
      }
    }
    else if (strcasecmp ("Timeout", child->key) == 0)
      status = cf_util_get_int (child, &node->timeout);
    else
      WARNING ("write_mongo plugin: Ignoring unknown config option \"%s\".",
          child->key);

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if (status == 0)
  {
    char cb_name[DATA_MAX_NAME_LEN];
    user_data_t ud;

    ssnprintf (cb_name, sizeof (cb_name), "write_mongo/%s", node->name);

    ud.data = node;
    ud.free_func = wm_config_free;

    status = plugin_register_write (cb_name, wm_write, &ud);
    INFO ("write_mongo plugin: registered write plugin %s %d",cb_name,status);
  }

  if (status != 0)
    wm_config_free (node);

  return (status);
} /* }}} int wm_config_node */

static int wm_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Node", child->key) == 0)
      wm_config_node (child);
    else
      WARNING ("write_mongo plugin: Ignoring unknown "
          "configuration option \"%s\" at top level.", child->key);
  }

  return (0);
} /* }}} int wm_config */

void module_register (void)
{
  plugin_register_complex_config ("write_mongo", wm_config);
}

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */
