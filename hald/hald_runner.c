/***************************************************************************
 * CVSID: $Id$
 *
 * hald_runner.c - Interface to the hal runner helper daemon
 *
 * Copyright (C) 2006 Sjoerd Simons, <sjoerd@luon.net>
 *
 * Licensed under the Academic Free License version 2.1
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 **************************************************************************/

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib-lowlevel.h>

#include "hald.h"
#include "util.h"
#include "logger.h"
#include "hald_dbus.h"
#include "hald_runner.h"

typedef struct {
  HalDevice *d;
  HalRunTerminatedCB cb;
  gpointer data1;
  gpointer data2;
} HelperData;

#define DBUS_SERVER_ADDRESS "unix:tmpdir=/tmp/hald-runner" 

static DBusConnection *runner_connection = NULL;

static void 
handle_connection(DBusServer *server,
                  DBusConnection *new_connection,
                  void *data) {

  if (runner_connection == NULL) {
    runner_connection = new_connection;
    dbus_connection_ref (new_connection);
    dbus_connection_setup_with_g_main (new_connection, NULL);
    /* dbus_server_unref(server); */
  }
}

static void
runner_died(GPid pid, gint status, gpointer data) {
  g_spawn_close_pid (pid);
  DIE (("Runner died"));
}

gboolean
hald_runner_start_runner(gchar *runner_location) {
  DBusServer *server = NULL;
  DBusError err;
  GError *error = NULL;
  GPid pid;
  char *argv[] = { NULL, NULL};
  char *env[] =  { NULL, NULL};

  dbus_error_init(&err);
  server = dbus_server_listen(DBUS_SERVER_ADDRESS, &err);
  if (server == NULL) {
    HAL_ERROR (("Cannot create D-BUS server for the runner"));
    goto error;
  }

  dbus_server_setup_with_g_main(server, NULL);
  dbus_server_set_new_connection_function(server, handle_connection, 
                                          NULL, NULL);
  if (runner_location == NULL) {
    runner_location = g_strdup_printf("%s/hald-runner", PACKAGE_LIBEXEC_DIR);
  } else {
    runner_location = g_strdup(runner_location);
  }
  HAL_INFO (("Using runner '%s'\n", runner_location));
  argv[0] = runner_location;
  env[0] = g_strdup_printf("HALD_RUNNER_DBUS_ADDRESS=%s",
             dbus_server_get_address(server));

  if (!g_spawn_async(NULL, argv, env, G_SPAWN_DO_NOT_REAP_CHILD, 
        NULL, NULL, &pid, &error)) {
    HAL_ERROR (("Could not spawn runner : '%s'", error->message));
    g_error_free (error);
    goto error;
  }
  g_free(argv[0]);
  g_free(env[0]);

  g_child_watch_add(pid, runner_died, NULL);
  while (runner_connection == NULL) {
    /* Wait for the runner */
    g_main_context_iteration(NULL, TRUE);
  }
  return TRUE;

error:
  if (server != NULL)
    dbus_server_unref(server);
  return FALSE;
}

static gboolean
add_property_to_msg (HalDevice *device, HalProperty *property, 
                                     gpointer user_data)
{
  char *prop_upper, *value;
  char *c;
  gchar *env;
  DBusMessageIter *iter = (DBusMessageIter *)user_data;
  
  prop_upper = g_ascii_strup (hal_property_get_key (property), -1);
 
  /* periods aren't valid in the environment, so replace them with
   * underscores. */
  for (c = prop_upper; *c; c++) {
    if (*c == '.')
      *c = '_';
  }

  value = hal_property_to_string (property);
  env = g_strdup_printf ("HAL_PROP_%s=%s", prop_upper, value);
  dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &env);

  g_free(env);
  g_free (value);
  g_free (prop_upper);

  return TRUE;
}

static void
add_env(DBusMessageIter *iter, const gchar *key, const gchar *value) {
  gchar *env;
  env = g_strdup_printf ("%s=%s", key, value);
  dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &env);
  g_free(env);
}

static void
add_basic_env(DBusMessageIter *iter, const gchar *udi) {
  if (hald_is_verbose) {
    add_env(iter, "HALD_VERBOSE", "1");
  }
  if (hald_is_initialising) {
    add_env(iter, "HALD_STARTUP", "1");
  }
  add_env(iter, "UDI", udi);
  add_env(iter, "HALD_DIRECT_ADDR", hald_dbus_local_server_addr());
}

static void
add_extra_env(DBusMessageIter *iter, gchar **env) {
  int i;
  if (env != NULL) for (i = 0; env[i] != NULL; i++) {
    dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &env[i]);
  }
}

static gboolean
add_command(DBusMessageIter *iter, const gchar *command_line) {
  gint argc;
  gint x;
  char **argv;
  GError *err = NULL;
  DBusMessageIter array_iter;

  if (!g_shell_parse_argv(command_line, &argc, &argv, &err)) {
    HAL_ERROR (("Error parsing commandline '%s': %s", 
                 command_line, err->message));
    g_error_free (err);
    return FALSE;
  }
  if (!dbus_message_iter_open_container(iter, 
                                   DBUS_TYPE_ARRAY,
                                   DBUS_TYPE_STRING_AS_STRING,
                                   &array_iter))
    DIE (("No memory"));
  for (x = 0 ; argv[x] != NULL; x++) {
    dbus_message_iter_append_basic(&array_iter, DBUS_TYPE_STRING, &argv[x]);
  }
  dbus_message_iter_close_container(iter, &array_iter);

  g_strfreev(argv);
  return TRUE;
}

static gboolean
add_first_part(DBusMessageIter *iter, HalDevice *device,
                   const gchar *command_line, char **extra_env) {
  DBusMessageIter array_iter;
  const char *udi;

  udi = hal_device_get_udi(device);
  dbus_message_iter_append_basic(iter, DBUS_TYPE_STRING, &udi);

  dbus_message_iter_open_container(iter, 
                                   DBUS_TYPE_ARRAY,
                                   DBUS_TYPE_STRING_AS_STRING,
                                   &array_iter);
  hal_device_property_foreach (device, add_property_to_msg, &array_iter);
  add_basic_env(&array_iter, udi);
  add_extra_env(&array_iter, extra_env);
  dbus_message_iter_close_container(iter, &array_iter);

  if (!add_command(iter, command_line)) {
    return FALSE;
  }
  return TRUE;
}

/* Start a helper, returns true on a successfull start */
gboolean
hald_runner_start(HalDevice *device,
                      const gchar *command_line, char **extra_env) {
  DBusMessage *msg, *reply;
  DBusError err;
  DBusMessageIter iter;

  dbus_error_init(&err);
  msg = dbus_message_new_method_call("org.freedesktop.HalRunner",
                                     "/org/freedesktop/HalRunner",
                                     "org.freedesktop.HalRunner",
                                     "Start");
  if (msg == NULL) 
    DIE(("No memory"));
  dbus_message_iter_init_append(msg, &iter);

  if (!add_first_part(&iter, device, command_line, extra_env)) 
    goto error;
 
  /* Wait for the reply, should be almost instantanious */
  reply = 
    dbus_connection_send_with_reply_and_block(runner_connection, 
                                              msg, -1, &err);
  if (reply) {
    gboolean ret = 
      (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN);
    dbus_message_unref(reply);
    dbus_message_unref(msg);
    return ret;
  }

error:
  dbus_message_unref(msg);
  return FALSE;
}

static void
call_notify(DBusPendingCall *pending, void *user_data) {
  HelperData *hb = (HelperData *)user_data;
  dbus_uint32_t exitt = HALD_RUN_SUCCESS;
  dbus_int32_t return_code = 0;
  DBusMessage *m;
  GArray *error = NULL;
  DBusMessageIter iter;

  error = g_array_new(TRUE, FALSE, sizeof(char *));

  m = dbus_pending_call_steal_reply(pending);
  if (dbus_message_get_type(m) != DBUS_MESSAGE_TYPE_METHOD_RETURN)
    goto malformed;

  if (!dbus_message_iter_init(m, &iter) ||
       dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32) 
    goto malformed;
  dbus_message_iter_get_basic(&iter, &exitt);

  if (!dbus_message_iter_next(&iter) || 
        dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_INT32) 
    goto malformed;
  dbus_message_iter_get_basic(&iter, &return_code);

  while (dbus_message_iter_next(&iter) &&
    dbus_message_iter_get_arg_type(&iter) == DBUS_TYPE_STRING) {
    const char *value;
    dbus_message_iter_get_basic(&iter, &value);
    g_array_append_vals(error, &value, 1);
  }

  hb->cb(hb->d, exitt, return_code, 
      (gchar **)error->data, hb->data1, hb->data2);

  dbus_message_unref(m);
  g_array_free(error, FALSE);

  return;
malformed:
  /* Send a Fail callback on malformed messages */
  HAL_ERROR (("Malformed or unexpected reply message"));
  hb->cb(hb->d, HALD_RUN_FAILED, return_code, NULL, hb->data1, hb->data2);
  dbus_message_unref(m);
  g_array_free(error, FALSE);
}

/* Run a helper program using the commandline, with input as infomation on
 * stdin */
void
hald_runner_run_method(HalDevice *device,
                           const gchar *command_line, char **extra_env, 
                           gchar *input, gboolean error_on_stderr,
                           guint32 timeout,
                           HalRunTerminatedCB  cb,
                           gpointer data1, gpointer data2) {
  DBusMessage *msg;
  DBusMessageIter iter;
  DBusPendingCall *call;
  HelperData *hd = NULL;
  msg = dbus_message_new_method_call("org.freedesktop.HalRunner",
                             "/org/freedesktop/HalRunner",
                             "org.freedesktop.HalRunner",
                             "Run");
  if (msg == NULL) 
    DIE(("No memory"));
  dbus_message_iter_init_append(msg, &iter);
  
  if (!add_first_part(&iter, device, command_line, extra_env)) 
    goto error;

  dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &input);
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_BOOLEAN, &error_on_stderr);
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_UINT32, &timeout);

  if (!dbus_connection_send_with_reply(runner_connection, 
                                              msg, &call, INT_MAX))
    DIE (("No memory"));

  hd = malloc(sizeof(HelperData));
  hd->d = device;
  hd->cb = cb;
  hd->data1 = data1;
  hd->data2 = data2;

  dbus_pending_call_set_notify(call, call_notify, hd, free);
  dbus_message_unref(msg);
  return;
error:
  dbus_message_unref(msg);
  free(hd);
  cb(device, HALD_RUN_FAILED, 0, NULL, data1, data2);
}

void
hald_runner_run(HalDevice *device,
                    const gchar *command_line, char **extra_env, 
                    guint timeout,
                    HalRunTerminatedCB  cb,
                    gpointer data1, gpointer data2) {
  hald_runner_run_method(device, command_line, extra_env, 
                             "", FALSE, timeout, cb, data1, data2);
}

void
hald_runner_kill_device(HalDevice *device) {
  DBusMessage *msg, *reply;
  DBusError err;
  DBusMessageIter iter;
  const char *udi;
  
  msg = dbus_message_new_method_call("org.freedesktop.HalRunner",
                             "/org/freedesktop/HalRunner",
                             "org.freedesktop.HalRunner",
                             "Kill");
  if (msg == NULL) 
    DIE(("No memory"));
  dbus_message_iter_init_append(msg, &iter);
  udi = hal_device_get_udi(device);
  dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &udi);

  /* Wait for the reply, should be almost instantanious */
  dbus_error_init(&err);
  reply = 
    dbus_connection_send_with_reply_and_block(runner_connection, msg, -1, &err);
  if (reply) {
    dbus_message_unref(reply);
  }

  dbus_message_unref(msg);
}

void
hald_runner_kill_all(HalDevice *device) {
  DBusMessage *msg, *reply;
  DBusError err;
  msg = dbus_message_new_method_call("org.freedesktop.HalRunner",
                             "/org/freedesktop/HalRunner",
                             "org.freedesktop.HalRunner",
                             "KillAll");
  if (msg == NULL) 
    DIE(("No memory"));

  /* Wait for the reply, should be almost instantanious */
  dbus_error_init(&err);
  reply = 
    dbus_connection_send_with_reply_and_block(runner_connection,
                                              msg, -1, &err);
  if (reply) {
    dbus_message_unref(reply);
  }

  dbus_message_unref(msg);
}