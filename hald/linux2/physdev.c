/***************************************************************************
 * CVSID: $Id$
 *
 * physdev.c : Handling of physical kernel devices 
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

#include <stdio.h>
#include <string.h>
#include <mntent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>

#include "../osspec.h"
#include "../logger.h"
#include "../hald.h"
#include "../device_info.h"
#include "../hald_conf.h"

#include "util.h"
#include "coldplug.h"
#include "hotplug_helper.h"

#include "hotplug.h"
#include "physdev.h"

#include "ids.h"

#include "pcmcia_utils.h"

/*--------------------------------------------------------------------------------------------------------------*/

static HalDevice *
pci_add (const gchar *sysfs_path, HalDevice *parent)
{
	HalDevice *d;
	gint device_class;

	d = hal_device_new ();
	hal_device_property_set_string (d, "linux.sysfs_path", sysfs_path);
	hal_device_property_set_string (d, "linux.sysfs_path_device", sysfs_path);
	hal_device_property_set_string (d, "info.bus", "pci");
	if (parent != NULL) {
		hal_device_property_set_string (d, "info.parent", parent->udi);
	} else {
		hal_device_property_set_string (d, "info.parent", "/org/freedesktop/Hal/devices/computer");
	}

	hal_device_property_set_string (d, "pci.linux.sysfs_path", sysfs_path);

	hal_util_set_int_from_file (d, "pci.product_id", sysfs_path, "device", 16);
	hal_util_set_int_from_file (d, "pci.vendor_id", sysfs_path, "vendor", 16);
	hal_util_set_int_from_file (d, "pci.subsys_product_id", sysfs_path, "subsystem_device", 16);
	hal_util_set_int_from_file (d, "pci.subsys_vendor_id", sysfs_path, "subsystem_vendor", 16);

	if (hal_util_get_int_from_file (sysfs_path, "class", &device_class, 16)) {
		hal_device_property_set_int (d, "pci.device_class", ((device_class >> 16) & 0xff));
		hal_device_property_set_int (d, "pci.device_subclass", ((device_class >> 8) & 0xff));
		hal_device_property_set_int (d, "pci.device_protocol", (device_class & 0xff));
	}

	{
		gchar buf[64];
		char *vendor_name;
		char *product_name;
		char *subsys_vendor_name;
		char *subsys_product_name;

		ids_find_pci (hal_device_property_get_int (d, "pci.vendor_id"), 
			      hal_device_property_get_int (d, "pci.product_id"), 
			      hal_device_property_get_int (d, "pci.subsys_vendor_id"), 
			      hal_device_property_get_int (d, "pci.subsys_product_id"), 
			      &vendor_name, &product_name, &subsys_vendor_name, &subsys_product_name);

		if (vendor_name != NULL) {
			hal_device_property_set_string (d, "pci.vendor", vendor_name);
			hal_device_property_set_string (d, "info.vendor", vendor_name);
		} else {
			g_snprintf (buf, sizeof (buf), "Unknown (0x%04x)", 
				    hal_device_property_get_int (d, "pci.vendor_id"));
			hal_device_property_set_string (d, "pci.vendor", buf);
			hal_device_property_set_string (d, "info.vendor", buf);
		}

		if (product_name != NULL) {
			hal_device_property_set_string (d, "pci.product", product_name);
			hal_device_property_set_string (d, "info.product", product_name);
		} else {
			g_snprintf (buf, sizeof (buf), "Unknown (0x%04x)", 
				    hal_device_property_get_int (d, "pci.product_id"));
			hal_device_property_set_string (d, "pci.product", buf);
			hal_device_property_set_string (d, "info.product", buf);
		}

		if (subsys_vendor_name != NULL) {
			hal_device_property_set_string (d, "pci.subsys_vendor", subsys_vendor_name);
		} else {
			g_snprintf (buf, sizeof (buf), "Unknown (0x%04x)", 
				    hal_device_property_get_int (d, "pci.subsys_vendor_id"));
			hal_device_property_set_string (d, "pci.subsys_vendor", buf);
		}

		if (subsys_product_name != NULL) {
			hal_device_property_set_string (d, "pci.subsys_product", subsys_product_name);
		} else {
			g_snprintf (buf, sizeof (buf), "Unknown (0x%04x)", 
				    hal_device_property_get_int (d, "pci.subsys_product_id"));
			hal_device_property_set_string (d, "pci.subsys_product", buf);
		}
	}

	return d;
}

static gboolean
pci_compute_udi (HalDevice *d)
{
	gchar udi[256];

	hal_util_compute_udi (hald_get_gdl (), udi, sizeof (udi),
			      "/org/freedesktop/Hal/devices/pci_%x_%x",
			      hal_device_property_get_int (d, "pci.vendor_id"),
			      hal_device_property_get_int (d, "pci.product_id"));
	hal_device_set_udi (d, udi);
	hal_device_property_set_string (d, "info.udi", udi);

	return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static void 
usbif_set_name (HalDevice *d, int ifclass, int ifsubclass, int ifprotocol)
{
	const char *name;

	switch (ifclass) {
	default:
	case 0x00:
		name = "USB Interface";
		break;
	case 0x01:
		name = "USB Audio Interface";
		break;
	case 0x02:
		name = "USB Communications Interface";
		break;
	case 0x03:
		name = "USB HID Interface";
		break;
	case 0x06:
		name = "USB Imaging Interface";
		break;
	case 0x07:
		name = "USB Printer Interface";
		break;
	case 0x08:
		name = "USB Mass Storage Interface";
		break;
	case 0x09:
		name = "USB Hub Interface";
		break;
	case 0x0a:
		name = "USB Data Interface";
		break;
	case 0x0b:
		name = "USB Chip/Smartcard Interface";
		break;
	case 0x0d:
		name = "USB Content Security Interface";
		break;
	case 0x0e:
		name = "USB Video Interface";
		break;
	case 0xdc:
		name = "USB Diagnostic Interface";
		break;
	case 0xe0:
		name = "USB Wireless Interface";
		break;
	case 0xef:
		name = "USB Miscelleneous Interface";
		break;
	case 0xfe:
		name = "USB Application Specific Interface";
		break;
	case 0xff:
		name = "USB Vendor Specific Interface";
		break;
	}

	hal_device_property_set_string (d, "usb.product", name);
	hal_device_property_set_string (d, "info.product", name);
}

static HalDevice *
usb_add (const gchar *sysfs_path, HalDevice *parent)
{
	HalDevice *d;
	const gchar *bus_id;

	d = hal_device_new ();
	hal_device_property_set_string (d, "linux.sysfs_path", sysfs_path);
	hal_device_property_set_string (d, "linux.sysfs_path_device", sysfs_path);
	if (parent != NULL) {
		hal_device_property_set_string (d, "info.parent", parent->udi);
	}

	/* only USB interfaces got a : in the bus_id */
	bus_id = hal_util_get_last_element (sysfs_path);
	if (strchr (bus_id, ':') == NULL) {
		gint bmAttributes;

		hal_device_property_set_string (d, "info.bus", "usb_device");

		hal_device_property_set_string (d, "usb_device.linux.sysfs_path", sysfs_path);

		hal_util_set_int_from_file (d, "usb_device.configuration_value", sysfs_path, "bConfigurationValue", 10);
		hal_util_set_int_from_file (d, "usb_device.num_configurations", sysfs_path, "bNumConfigurations", 10);
		hal_util_set_int_from_file (d, "usb_device.num_interfaces", sysfs_path, "bNumInterfaces", 10);

		hal_util_set_int_from_file (d, "usb_device.device_class", sysfs_path, "bDeviceClass", 16);
		hal_util_set_int_from_file (d, "usb_device.device_subclass", sysfs_path, "bDeviceSubClass", 16);
		hal_util_set_int_from_file (d, "usb_device.device_protocol", sysfs_path, "bDeviceProtocol", 16);

		hal_util_set_int_from_file (d, "usb_device.vendor_id", sysfs_path, "idVendor", 16);
		hal_util_set_int_from_file (d, "usb_device.product_id", sysfs_path, "idProduct", 16);

		{
			gchar buf[64];
			char *vendor_name;
			char *product_name;

			ids_find_usb (hal_device_property_get_int (d, "usb_device.vendor_id"), 
				      hal_device_property_get_int (d, "usb_device.product_id"), 
				      &vendor_name, &product_name);

			if (vendor_name != NULL) {
				hal_device_property_set_string (d, "usb_device.vendor", vendor_name);
			} else {
				if (!hal_util_set_string_from_file (d, "usb_device.vendor", 
								    sysfs_path, "manufacturer")) {
					g_snprintf (buf, sizeof (buf), "Unknown (0x%04x)", 
						    hal_device_property_get_int (d, "usb_device.vendor_id"));
					hal_device_property_set_string (d, "usb_device.vendor", buf); 
				}
			}
			hal_device_property_set_string (d, "info.vendor",
							hal_device_property_get_string (d, "usb_device.vendor"));

			if (product_name != NULL) {
				hal_device_property_set_string (d, "usb_device.product", product_name);
			} else {
				if (!hal_util_set_string_from_file (d, "usb_device.product", 
								    sysfs_path, "product")) {
					g_snprintf (buf, sizeof (buf), "Unknown (0x%04x)", 
						    hal_device_property_get_int (d, "usb_device.product_id"));
					hal_device_property_set_string (d, "usb_device.product", buf); 
				}
			}
			hal_device_property_set_string (d, "info.product",
							hal_device_property_get_string (d, "usb_device.product"));
		}

		hal_util_set_int_from_file (d, "usb_device.device_revision_bcd", sysfs_path, "bcdDevice", 16);

		hal_util_set_int_from_file (d, "usb_device.max_power", sysfs_path, "bMaxPower", 10);
		hal_util_set_int_from_file (d, "usb_device.num_ports", sysfs_path, "maxchild", 10);
		hal_util_set_int_from_file (d, "usb_device.linux.device_number", sysfs_path, "devnum", 10);

		hal_util_set_string_from_file (d, "usb_device.serial", sysfs_path, "serial");

		hal_util_set_string_from_file (d, "usb_device.serial", sysfs_path, "serial");
		hal_util_set_bcd2_from_file (d, "usb_device.speed_bcd", sysfs_path, "speed");
		hal_util_set_bcd2_from_file (d, "usb_device.version_bcd", sysfs_path, "version");

		hal_util_get_int_from_file (sysfs_path, "bmAttributes", &bmAttributes, 16);
		hal_device_property_set_bool (d, "usb_device.is_self_powered", (bmAttributes & 0x40) != 0);
		hal_device_property_set_bool (d, "usb_device.can_wake_up", (bmAttributes & 0x20) != 0);

		if (strncmp (bus_id, "usb", 3) == 0)
			hal_device_property_set_int (d, "usb_device.bus_number", atoi (bus_id + 3));
		else
			hal_device_property_set_int (d, "usb_device.bus_number", atoi (bus_id));

		/* TODO:  .level_number .parent_number  */

	} else {
		hal_device_property_set_string (d, "info.bus", "usb");

		/* take all usb_device.* properties from parent and make them usb.* on this object */
		hal_device_merge_with_rewrite (d, parent, "usb.", "usb_device.");

		hal_device_property_set_string (d, "usb.linux.sysfs_path", sysfs_path);

		hal_util_set_int_from_file (d, "usb.interface.number", sysfs_path, "bInterfaceNumber", 10);

		hal_util_set_int_from_file (d, "usb.interface.class", sysfs_path, "bInterfaceClass", 16);
		hal_util_set_int_from_file (d, "usb.interface.subclass", sysfs_path, "bInterfaceSubClass", 16);
		hal_util_set_int_from_file (d, "usb.interface.protocol", sysfs_path, "bInterfaceProtocol", 16);

		usbif_set_name (d, 
				hal_device_property_get_int (d, "usb.interface.class"),
				hal_device_property_get_int (d, "usb.interface.subclass"),
				hal_device_property_get_int (d, "usb.interface.protocol"));
	}

	return d;
}

static gboolean
usb_compute_udi (HalDevice *d)
{
	gchar udi[256];

	if (hal_device_has_property (d, "usb.interface.number")) {
		hal_util_compute_udi (hald_get_gdl (), udi, sizeof (udi),
				      "%s_if%d",
				      hal_device_property_get_string (d, "info.parent"),
				      hal_device_property_get_int (d, "usb.interface.number"));
		hal_device_set_udi (d, udi);
		hal_device_property_set_string (d, "info.udi", udi);
	} else {
		hal_util_compute_udi (hald_get_gdl (), udi, sizeof (udi),
				      "/org/freedesktop/Hal/devices/usb_device_%x_%x_%s",
				      hal_device_property_get_int (d, "usb_device.vendor_id"),
				      hal_device_property_get_int (d, "usb_device.product_id"),
				      hal_device_has_property (d, "usb_device.serial") ?
				        hal_device_property_get_string (d, "usb_device.serial") :
				        "noserial");
		hal_device_set_udi (d, udi);
		hal_device_property_set_string (d, "info.udi", udi);
	}

	return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

static HalDevice *
ide_add (const gchar *sysfs_path, HalDevice *parent)
{
	HalDevice *d;
	const gchar *bus_id;
	guint host, channel;

	d = hal_device_new ();
	hal_device_property_set_string (d, "linux.sysfs_path", sysfs_path);
	hal_device_property_set_string (d, "linux.sysfs_path_device", sysfs_path);
	hal_device_property_set_string (d, "info.bus", "ide");
	if (parent != NULL) {
		hal_device_property_set_string (d, "info.parent", parent->udi);
	} else {
		hal_device_property_set_string (d, "info.parent", "/org/freedesktop/Hal/devices/computer");
	}

	bus_id = hal_util_get_last_element (sysfs_path);

	sscanf (bus_id, "%d.%d", &host, &channel);
	hal_device_property_set_int (d, "ide.host", host);
	hal_device_property_set_int (d, "ide.channel", channel);

	if (channel == 0) {
		hal_device_property_set_string (d, "info.product", "IDE device (master)");
	} else {
		hal_device_property_set_string (d, "info.product", "IDE device (slave)");
	}
	
	return d;
}

static gboolean
ide_compute_udi (HalDevice *d)
{
	gchar udi[256];

	hal_util_compute_udi (hald_get_gdl (), udi, sizeof (udi),
			      "%s_ide_%d_%d",
			      hal_device_property_get_string (d, "info.parent"),
			      hal_device_property_get_int (d, "ide.host"),
			      hal_device_property_get_int (d, "ide.channel"));
	hal_device_set_udi (d, udi);
	hal_device_property_set_string (d, "info.udi", udi);

	return TRUE;

}

/*--------------------------------------------------------------------------------------------------------------*/

static HalDevice *
pnp_add (const gchar *sysfs_path, HalDevice *parent)
{
	HalDevice *d;

	d = hal_device_new ();
	hal_device_property_set_string (d, "linux.sysfs_path", sysfs_path);
	hal_device_property_set_string (d, "linux.sysfs_path_device", sysfs_path);
	hal_device_property_set_string (d, "info.bus", "pnp");
	if (parent != NULL) {
		hal_device_property_set_string (d, "info.parent", parent->udi);
	} else {
		hal_device_property_set_string (d, "info.parent", "/org/freedesktop/Hal/devices/computer");
	}

	hal_util_set_string_from_file (d, "pnp.id", sysfs_path, "id");
	if (hal_device_has_property (d, "pnp.id")) {
		gchar *pnp_description;
		ids_find_pnp (hal_device_property_get_string (d, "pnp.id"), &pnp_description);
		if (pnp_description != NULL) {
			hal_device_property_set_string (d, "pnp.description", pnp_description);
			hal_device_property_set_string (d, "info.product", pnp_description);
		}
	}

	if (!hal_device_has_property (d, "info.product")) {
		gchar buf[64];
		g_snprintf (buf, sizeof (buf), "PnP Device (%s)", hal_device_property_get_string (d, "pnp.id"));
		hal_device_property_set_string (d, "info.product", buf);
	}

	
	return d;
}

static gboolean
pnp_compute_udi (HalDevice *d)
{
	gchar udi[256];

	hal_util_compute_udi (hald_get_gdl (), udi, sizeof (udi),
			      "/org/freedesktop/Hal/devices/pnp_%s",
			      hal_device_property_get_string (d, "pnp.id"));
	hal_device_set_udi (d, udi);
	hal_device_property_set_string (d, "info.udi", udi);

	return TRUE;

}

/*--------------------------------------------------------------------------------------------------------------*/

static HalDevice *
serio_add (const gchar *sysfs_path, HalDevice *parent)
{
	HalDevice *d;
	const gchar *bus_id;

	d = hal_device_new ();
	hal_device_property_set_string (d, "linux.sysfs_path", sysfs_path);
	hal_device_property_set_string (d, "linux.sysfs_path_device", sysfs_path);
	hal_device_property_set_string (d, "info.bus", "serio");
	if (parent != NULL) {
		hal_device_property_set_string (d, "info.parent", parent->udi);
	} else {
		hal_device_property_set_string (d, "info.parent", "/org/freedesktop/Hal/devices/computer");
	}

	bus_id = hal_util_get_last_element (sysfs_path);
	hal_device_property_set_string (d, "serio.id", bus_id);
	if (!hal_util_set_string_from_file (d, "serio.description", sysfs_path, "description")) {
		hal_device_property_set_string (d, "serio.description", hal_device_property_get_string (d, "serio.id"));
	}
	hal_device_property_set_string (d, "info.product", hal_device_property_get_string (d, "serio.description"));
	
	return d;
}

static gboolean
serio_compute_udi (HalDevice *d)
{
	gchar udi[256];

	hal_util_compute_udi (hald_get_gdl (), udi, sizeof (udi),
			      "%s_%s",
			      hal_device_property_get_string (d, "info.parent"),
			      hal_device_property_get_string (d, "serio.description"));
	hal_device_set_udi (d, udi);
	hal_device_property_set_string (d, "info.udi", udi);
	return TRUE;

}

/*--------------------------------------------------------------------------------------------------------------*/

static HalDevice *
pcmcia_add (const gchar *sysfs_path, HalDevice *parent)
{
	HalDevice *d;
	const gchar *bus_id;
	guint socket, function;

	d = hal_device_new ();
	hal_device_property_set_string (d, "linux.sysfs_path", sysfs_path);
	hal_device_property_set_string (d, "linux.sysfs_path_device", sysfs_path);
	hal_device_property_set_string (d, "info.bus", "pcmcia");
	if (parent != NULL) {
		hal_device_property_set_string (d, "info.parent", parent->udi);
	} else {
		hal_device_property_set_string (d, "info.parent", "/org/freedesktop/Hal/devices/computer");
	}

	bus_id = hal_util_get_last_element (sysfs_path);

	/* not sure if %d.%d means socket function - need to revisit */
	sscanf (bus_id, "%d.%d", &socket, &function);
	hal_device_property_set_int (d, "pcmcia.socket_number", socket);

	/* TODO: need to read this from sysfs instead of relying on stab */
	{
		pcmcia_card_info *info;

		info = pcmcia_card_info_get (socket);
		if (info != NULL) {
			const char *type;

			if (info->productid_1 != NULL && strlen (info->productid_1) > 0)
				hal_device_property_set_string (d, "pcmcia.productid_1", info->productid_1);
			else
				hal_device_property_set_string (d, "pcmcia.productid_1", "");
			if (info->productid_2 != NULL && strlen (info->productid_2) > 0)
				hal_device_property_set_string (d, "pcmcia.productid_2", info->productid_2);
			else
				hal_device_property_set_string (d, "pcmcia.productid_2", "");
			if (info->productid_3 != NULL && strlen (info->productid_3) > 0)
				hal_device_property_set_string (d, "pcmcia.productid_3", info->productid_3);
			else
				hal_device_property_set_string (d, "pcmcia.productid_3", "");
			if (info->productid_4 != NULL && strlen (info->productid_4) > 0)
				hal_device_property_set_string (d, "pcmcia.productid_4", info->productid_4);
			else
				hal_device_property_set_string (d, "pcmcia.productid_4", "");

			if ((type = pcmcia_card_type_string_from_type (info->type)))
				hal_device_property_set_string (d, "pcmcia.function", type);
			else
				hal_device_property_set_string (d, "pcmcia.function", "");

			hal_device_property_set_int (d, "pcmcia.manfid1", info->manfid_1);
			hal_device_property_set_int (d, "pcmcia.manfid2", info->manfid_2);

			/* Provide best-guess of vendor, goes in Vendor property */
			if (info->productid_1 != NULL) {
				hal_device_property_set_string (d, "info.vendor", info->productid_1);
			} else {
				gchar buf[50];
				g_snprintf (buf, sizeof(buf), "Unknown (0x%04x)", info->manfid_1);
				hal_device_property_set_string (d, "info.vendor", buf);
			}

			/* Provide best-guess of name, goes in Product property */
			if (info->productid_2 != NULL) {
				hal_device_property_set_string (d, "info.product", info->productid_2);
			} else {
				gchar buf[50];
				g_snprintf (buf, sizeof(buf), "Unknown (0x%04x)", info->manfid_2);
				hal_device_property_set_string (d, "info.product", buf);
			}

			pcmcia_card_info_free (info);
		}
	}


	return d;
}

static gboolean
pcmcia_compute_udi (HalDevice *d)
{
	gchar udi[256];

	hal_util_compute_udi (hald_get_gdl (), udi, sizeof (udi),
			      "/org/freedesktop/Hal/devices/pcmcia_%d_%d",
			      hal_device_property_get_int (d, "pcmcia.manfid1"),
			      hal_device_property_get_int (d, "pcmcia.manfid2"));
	hal_device_set_udi (d, udi);
	hal_device_property_set_string (d, "info.udi", udi);
	return TRUE;

}

/*--------------------------------------------------------------------------------------------------------------*/

static HalDevice *
scsi_add (const gchar *sysfs_path, HalDevice *parent)
{
	HalDevice *d;
	const gchar *bus_id;
	gint host_num, bus_num, target_num, lun_num;

	if (parent == NULL) {
		d = NULL;
		goto out;
	}

	d = hal_device_new ();
	hal_device_property_set_string (d, "linux.sysfs_path", sysfs_path);
	hal_device_property_set_string (d, "linux.sysfs_path_device", sysfs_path);
	hal_device_property_set_string (d, "info.bus", "scsi");
	hal_device_property_set_string (d, "info.parent", parent->udi);

	bus_id = hal_util_get_last_element (sysfs_path);
	sscanf (bus_id, "%d:%d:%d:%d", &host_num, &bus_num, &target_num, &lun_num);
	hal_device_property_set_int (d, "scsi.host", host_num);
	hal_device_property_set_int (d, "scsi.bus", bus_num);
	hal_device_property_set_int (d, "scsi.target", target_num);
	hal_device_property_set_int (d, "scsi.lun", lun_num);

	/* guestimate product name */
	hal_device_property_set_string (d, "info.product", "SCSI Device");

out:
	return d;
}

static gboolean
scsi_compute_udi (HalDevice *d)
{
	gchar udi[256];

	hal_util_compute_udi (hald_get_gdl (), udi, sizeof (udi),
			      "%s_scsi_device_lun%d",
			      hal_device_property_get_string (d, "info.parent"),
			      hal_device_property_get_int (d, "scsi.lun"));
	hal_device_set_udi (d, udi);
	hal_device_property_set_string (d, "info.udi", udi);
	return TRUE;

}

/*--------------------------------------------------------------------------------------------------------------*/

static HalDevice *
mmc_add (const gchar *sysfs_path, HalDevice *parent)
{
	HalDevice *d;
	const gchar *bus_id;
	gint host_num, rca, manfid, oemid;
	gchar *scr;

	if (parent == NULL) {
		d = NULL;
		goto out;
	}

	d = hal_device_new ();
	hal_device_property_set_string (d, "linux.sysfs_path", sysfs_path);
	hal_device_property_set_string (d, "linux.sysfs_path_device", sysfs_path);
	hal_device_property_set_string (d, "info.bus", "mmc");
	hal_device_property_set_string (d, "info.parent", parent->udi);

	bus_id = hal_util_get_last_element (sysfs_path);
	sscanf (bus_id, "mmc%d:%x", &host_num, &rca);
	hal_device_property_set_int (d, "mmc.rca", rca);
	
	hal_util_set_string_from_file (d, "mmc.cid", sysfs_path, "cid");
	hal_util_set_string_from_file (d, "mmc.csd", sysfs_path, "csd");
	
	scr = hal_util_get_string_from_file (sysfs_path, "scr");
	if (scr != NULL) {
		if (strcmp (scr, "0000000000000000") == 0)
			scr = NULL;
		else
			hal_device_property_set_string (d, "mmc.scr", scr);
	}

	if (!hal_util_set_string_from_file (d, "info.product", sysfs_path, "name")) {
		if (scr != NULL)
			hal_device_property_set_string (d, "info.product", "SD Card");
		else
			hal_device_property_set_string (d, "info.product", "MMC Card");
	}
	
	if (hal_util_get_int_from_file (sysfs_path, "manfid", &manfid, 16)) {
		/* Here we should have a mapping to a name */
		char vendor[256];
		snprintf(vendor, 256, "Unknown (%d)", manfid);
		hal_device_property_set_string (d, "info.vendor", vendor);
	}
	if (hal_util_get_int_from_file (sysfs_path, "oemid", &oemid, 16)) {
		/* Here we should have a mapping to a name */
		char oem[256];
		snprintf(oem, 256, "Unknown (%d)", oemid);
		hal_device_property_set_string (d, "mmc.oem", oem);
	}

	hal_util_set_string_from_file (d, "mmc.date", sysfs_path, "date");
	hal_util_set_int_from_file (d, "mmc.hwrev", sysfs_path, "hwrev", 16);
	hal_util_set_int_from_file (d, "mmc.fwrev", sysfs_path, "fwrev", 16);
	hal_util_set_int_from_file (d, "mmc.serial", sysfs_path, "serial", 16);

out:
	return d;
}

static gboolean
mmc_compute_udi (HalDevice *d)
{
	gchar udi[256];

	hal_util_compute_udi (hald_get_gdl (), udi, sizeof (udi),
			      "%s_mmc_card_rca%d",
			      hal_device_property_get_string (d, "info.parent"),
			      hal_device_property_get_int (d, "mmc.rca"));
	hal_device_set_udi (d, udi);
	hal_device_property_set_string (d, "info.udi", udi);
	return TRUE;

}

/*--------------------------------------------------------------------------------------------------------------*/

static gboolean
physdev_remove (HalDevice *d)
{
	return TRUE;
}

/*--------------------------------------------------------------------------------------------------------------*/

typedef struct
{
	const gchar *subsystem;
	HalDevice *(*add) (const gchar *sysfs_path, HalDevice *parent);
	gboolean (*compute_udi) (HalDevice *d);
	gboolean (*remove) (HalDevice *d);
} PhysDevHandler;

static PhysDevHandler physdev_handler_pci = { 
	.subsystem   = "pci",
	.add         = pci_add,
	.compute_udi = pci_compute_udi,
	.remove      = physdev_remove
};

static PhysDevHandler physdev_handler_usb = { 
	.subsystem   = "usb",
	.add         = usb_add,
	.compute_udi = usb_compute_udi,
	.remove      = physdev_remove
};

static PhysDevHandler physdev_handler_ide = { 
	.subsystem   = "ide",
	.add         = ide_add,
	.compute_udi = ide_compute_udi,
	.remove      = physdev_remove
};

static PhysDevHandler physdev_handler_pnp = { 
	.subsystem   = "pnp",
	.add         = pnp_add,
	.compute_udi = pnp_compute_udi,
	.remove      = physdev_remove
};

static PhysDevHandler physdev_handler_serio = { 
	.subsystem   = "serio",
	.add         = serio_add,
	.compute_udi = serio_compute_udi,
	.remove      = physdev_remove
};

static PhysDevHandler physdev_handler_pcmcia = { 
	.subsystem   = "pcmcia",
	.add         = pcmcia_add,
	.compute_udi = pcmcia_compute_udi,
	.remove      = physdev_remove
};

static PhysDevHandler physdev_handler_scsi = { 
	.subsystem   = "scsi",
	.add         = scsi_add,
	.compute_udi = scsi_compute_udi,
	.remove      = physdev_remove
};

static PhysDevHandler physdev_handler_mmc = { 
	.subsystem   = "mmc",
	.add         = mmc_add,
	.compute_udi = mmc_compute_udi,
	.remove      = physdev_remove
};
	

static PhysDevHandler *phys_handlers[] = {
	&physdev_handler_pci,
	&physdev_handler_usb,
	&physdev_handler_ide,
	&physdev_handler_pnp,
	&physdev_handler_serio,
	&physdev_handler_pcmcia,
	&physdev_handler_scsi,
	&physdev_handler_mmc,
	NULL
};

/*--------------------------------------------------------------------------------------------------------------*/

static void 
physdev_callouts_add_done (HalDevice *d, gpointer userdata)
{
	void *end_token = (void *) userdata;

	HAL_INFO (("Add callouts completed udi=%s", d->udi));

	/* Move from temporary to global device store */
	hal_device_store_remove (hald_get_tdl (), d);
	hal_device_store_add (hald_get_gdl (), d);

	hotplug_event_end (end_token);
}

static void 
physdev_callouts_remove_done (HalDevice *d, gpointer userdata)
{
	void *end_token = (void *) userdata;

	HAL_INFO (("Remove callouts completed udi=%s", d->udi));

	if (!hal_device_store_remove (hald_get_gdl (), d)) {
		HAL_WARNING (("Error removing device"));
	}

	hotplug_event_end (end_token);
}

void
hotplug_event_begin_add_physdev (const gchar *subsystem, const gchar *sysfs_path, HalDevice *parent, void *end_token)
{
	guint i;

	HAL_INFO (("phys_add: subsys=%s sysfs_path=%s, parent=0x%08x", subsystem, sysfs_path, parent));

	for (i = 0; phys_handlers [i] != NULL; i++) {
		PhysDevHandler *handler;

		handler = phys_handlers[i];
		if (strcmp (handler->subsystem, subsystem) == 0) {
			HalDevice *d;

			d = handler->add (sysfs_path, parent);
			if (d == NULL) {
				/* didn't find anything - thus, ignore this hotplug event */
				hotplug_event_end (end_token);
				goto out;
			}

			hal_device_property_set_int (d, "linux.hotplug_type", HOTPLUG_EVENT_SYSFS_BUS);
			hal_device_property_set_string (d, "linux.subsystem", subsystem);

			/* Add to temporary device store */
			hal_device_store_add (hald_get_tdl (), d);

			/* Merge properties from .fdi files */
			di_search_and_merge (d);

			/* Compute UDI */
			if (!handler->compute_udi (d)) {
				hal_device_store_remove (hald_get_tdl (), d);
				hotplug_event_end (end_token);
				goto out;
			}

			/* Run callouts */
			hal_util_callout_device_add (d, physdev_callouts_add_done, end_token);
			goto out;
		}
	}
	
	/* didn't find anything - thus, ignore this hotplug event */
	hotplug_event_end (end_token);
out:
	;
}

void
hotplug_event_begin_remove_physdev (const gchar *subsystem, const gchar *sysfs_path, void *end_token)
{
	guint i;
	HalDevice *d;

	HAL_INFO (("phys_rem: subsys=%s sysfs_path=%s", subsystem, sysfs_path));

	d = hal_device_store_match_key_value_string (hald_get_gdl (), 
						     "linux.sysfs_path", 
						     sysfs_path);
	if (d == NULL) {
		HAL_WARNING (("Couldn't remove device with sysfs path %s - not found", sysfs_path));
		goto out;
	}
	
	for (i = 0; phys_handlers [i] != NULL; i++) {
		PhysDevHandler *handler;
		
		handler = phys_handlers[i];
		if (strcmp (handler->subsystem, subsystem) == 0) {
			handler->remove (d);
			
			hal_util_callout_device_remove (d, physdev_callouts_remove_done, end_token);
			goto out2;
		}
	}

out:
	/* didn't find anything - thus, ignore this hotplug event */
	hotplug_event_end (end_token);
out2:
	;
}

gboolean
physdev_rescan_device (HalDevice *d)
{
	return FALSE;
}

HotplugEvent *
physdev_generate_add_hotplug_event (HalDevice *d)
{
	const char *subsystem;
	const char *sysfs_path;
	HotplugEvent *hotplug_event;

	subsystem = hal_device_property_get_string (d, "linux.subsystem");
	sysfs_path = hal_device_property_get_string (d, "linux.sysfs_path");

	hotplug_event = g_new0 (HotplugEvent, 1);
	hotplug_event->is_add = TRUE;
	hotplug_event->type = HOTPLUG_EVENT_SYSFS;
	g_strlcpy (hotplug_event->sysfs.subsystem, subsystem, sizeof (hotplug_event->sysfs.subsystem));
	g_strlcpy (hotplug_event->sysfs.sysfs_path, sysfs_path, sizeof (hotplug_event->sysfs.sysfs_path));
	hotplug_event->sysfs.device_file[0] = '\0';
	hotplug_event->sysfs.net_ifindex = -1;

	return hotplug_event;
}

HotplugEvent *
physdev_generate_remove_hotplug_event (HalDevice *d)
{
	const char *subsystem;
	const char *sysfs_path;
	HotplugEvent *hotplug_event;

	subsystem = hal_device_property_get_string (d, "linux.subsystem");
	sysfs_path = hal_device_property_get_string (d, "linux.sysfs_path");

	hotplug_event = g_new0 (HotplugEvent, 1);
	hotplug_event->is_add = FALSE;
	hotplug_event->type = HOTPLUG_EVENT_SYSFS;
	g_strlcpy (hotplug_event->sysfs.subsystem, subsystem, sizeof (hotplug_event->sysfs.subsystem));
	g_strlcpy (hotplug_event->sysfs.sysfs_path, sysfs_path, sizeof (hotplug_event->sysfs.sysfs_path));
	hotplug_event->sysfs.device_file[0] = '\0';
	hotplug_event->sysfs.net_ifindex = -1;

	return hotplug_event;
}