/***************************************************************************
 * CVSID: $Id$
 *
 * PCMCIA socket class handler
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <assert.h>
#include <unistd.h>
#include <stdarg.h>
#include <limits.h>

#include "../logger.h"
#include "../device_store.h"
#include "../hald.h"

#include "class_device.h"
#include "common.h"

/**
 * @defgroup HalDaemonLinuxPCMCIASocket PCMCIA Socket class devices
 * @ingroup HalDaemonLinux
 * @brief PCMCIA Socket class devices
 * @{
 */


/** This method is called just before the device is either merged
 *  onto the sysdevice or added to the GDL (cf. merge_or_add). 
 *  This is useful for extracting more information about the device
 *  through e.g. ioctl's using the device file property and also
 *  for setting info.category|capability.
 *
 *  @param  self          Pointer to class members
 *  @param  d             The HalDevice object of the instance of
 *                        this device class
 *  @param  sysfs_path    The path in sysfs (including mount point) of
 *                        the class device in sysfs
 *  @param  class_device  Libsysfs object representing class device
 *                        instance
 */
static void 
pcmcia_socket_class_pre_process (ClassDeviceHandler *self,
				HalDevice *d,
				const char *sysfs_path,
				struct sysfs_class_device *class_device)
{
	int num;

	sscanf (class_device->name, "pcmcia_socket%d", &num);
	hal_device_property_set_int (d, "pcmcia_socket.number", num);

	hal_device_add_capability (d, "pcmcia_socket");
	hal_device_property_set_string (d, "info.category", "pcmcia_socket");

}

/** Method specialisations for input device class */
ClassDeviceHandler pcmcia_socket_class_handler = {
	class_device_init,                  /**< init function */
	class_device_shutdown,              /**< shutdown function */
	class_device_tick,                  /**< timer function */
	class_device_accept,                /**< accept function */
	class_device_visit,                 /**< visitor function */
	class_device_removed,               /**< class device is removed */
	class_device_udev_event,            /**< handle udev event */
	class_device_get_device_file_target,/**< where to store devfile name */
	pcmcia_socket_class_pre_process,    /**< add more properties */
	class_device_post_merge,            /**< post merge function */
	class_device_got_udi,               /**< got UDI */
	NULL,                               /**< No UDI computation */
	class_device_in_gdl,                /**< in GDL */
	"pcmcia_socket",                    /**< sysfs class name */
	"pcmcia_socket",                    /**< hal class name */
	FALSE,                              /**< require device file */
	TRUE                                /**< merge onto sysdevice */
};

/** @} */