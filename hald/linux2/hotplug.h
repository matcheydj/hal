/***************************************************************************
 * CVSID: $Id$
 *
 * hotplug.h : Handling of hotplug events
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

#ifndef HOTPLUG_H
#define HOTPLUG_H

#include <glib.h>

#include "util.h"

/** Data structure representing a hotplug event; also used for
 *  coldplugging.
 */
typedef struct
{
	gboolean is_add;                        /**< Whether the event is add or remove */
	char subsystem[HAL_PATH_MAX];           /**< Subsystem e.g. usb, pci (only for hotplug msg) */
	char sysfs_path[HAL_PATH_MAX];          /**< Path into sysfs e.g. /sys/block/sda */

	char wait_for_sysfs_path[HAL_PATH_MAX];	/**< Wait for completion of events that a) comes before this one; AND
						 *   b) has a sysfs path that is contained in or equals this */

	char device_file [HAL_PATH_MAX];        /**< Path to special device (may be NULL) */

	int net_ifindex;                        /**< For network class devices only; the value of the ifindex file */
} HotplugEvent;

void hotplug_event_enqueue (HotplugEvent *event);

void hotplug_event_process_queue (void);

void hotplug_event_end (void *end_token);

#endif /* HOTPLUG_H */