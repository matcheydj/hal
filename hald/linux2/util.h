/***************************************************************************
 * CVSID: $Id$
 *
 * util.h - Various utilities
 *
 * Copyright (C) 2004 David Zeuthen, <david@fubar.dk>
 *
 * Licensed under the Academic Free License version 2.0
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 **************************************************************************/

#ifndef UTIL_H
#define UTIL_H

#include "../device.h"

gboolean hal_util_remove_trailing_slash (gchar *path);

gboolean hal_util_get_fs_mnt_path (const gchar *fs_type, gchar *mnt_path, gsize len);

const gchar *hal_util_get_last_element (const gchar *s);

gchar *hal_util_get_parent_sysfs_path (const gchar *path);

gboolean hal_util_get_device_file (const gchar *sysfs_path, gchar *dev_file, gsize dev_file_length);

HalDevice *hal_util_find_closest_ancestor (const gchar *sysfs_path);

gchar *hal_util_get_normalized_path (const gchar *path1, const gchar *path2);

gboolean hal_util_get_int_from_file (const gchar *directory, const gchar *file, gint *result, gint base);

gboolean hal_util_set_int_from_file (HalDevice *d, const gchar *key, const gchar *directory, const gchar *file, gint base);

gboolean hal_util_set_string_from_file (HalDevice *d, const gchar *key, const gchar *directory, const gchar *file);

gboolean hal_util_get_bcd2_from_file (const gchar *directory, const gchar *file, gint *result);

gboolean hal_util_set_bcd2_from_file (HalDevice *d, const gchar *key, const gchar *directory, const gchar *file);

void hal_util_compute_udi (HalDeviceStore *store, gchar *dst, gsize dstsize, const gchar *format, ...);

typedef void (*HelperTerminatedCB)(HalDevice *d, gboolean timed_out, gint return_code, gpointer data1, gpointer data2);

gboolean helper_invoke (const gchar *path, HalDevice *d, gpointer data1, gpointer data2, HelperTerminatedCB cb, guint timeout);

#define HAL_HELPER_TIMEOUT 10000

#define HAL_PATH_MAX 256

extern char hal_sysfs_path [HAL_PATH_MAX];
extern char hal_proc_path [HAL_PATH_MAX];

#endif /* UTIL_H */