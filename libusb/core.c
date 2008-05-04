/*
 * Core functions for libusb-compat-0.1
 * Copyright (C) 2008 Daniel Drake <dsd@gentoo.org>
 * Copyright (c) 2000-2003 Johannes Erdfelt <johannes@erdfelt.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <config.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <libusb.h>

#include "usb.h"
#include "usbi.h"

API_EXPORTED struct usb_bus *usb_busses = NULL;

void usbi_log(enum usbi_log_level level, const char *function,
	const char *format, ...)
{
	va_list args;
	FILE *stream = stdout;
	const char *prefix;

	switch (level) {
	case LOG_LEVEL_INFO:
		prefix = "info";
		break;
	case LOG_LEVEL_WARNING:
		stream = stderr;
		prefix = "warning";
		break;
	case LOG_LEVEL_ERROR:
		stream = stderr;
		prefix = "error";
		break;
	case LOG_LEVEL_DEBUG:
		stream = stderr;
		prefix = "debug";
		break;
	default:
		stream = stderr;
		prefix = "unknown";
		break;
	}

	fprintf(stream, "libusb-compat %s: %s: ", prefix, function);

	va_start (args, format);
	vfprintf(stream, format, args);
	va_end (args);

	fprintf(stream, "\n");
}

API_EXPORTED void usb_init(void)
{
	int r;
	usbi_dbg("");

	r = libusb_init();
	if (r < 0)
		usbi_err("initialization failed!");
}

static int find_busses(struct usb_bus **ret)
{
	libusb_device **dev_list = NULL;
	struct usb_bus *busses = NULL;
	struct usb_bus *bus;
	int dev_list_len = 0;
	int i;
	int r;

	r = libusb_get_device_list(&dev_list);
	if (r < 0) {
		usbi_err("get_device_list failed with error %d", r);
		return -1;
	}

	if (r == 0) {
		libusb_free_device_list(dev_list, 1);
		/* no buses */
		return 0;
	}

	/* iterate over the device list, identifying the individual busses.
	 * we use the location field of the usb_bus structure to store the
	 * bus number. */

	dev_list_len = r;
	for (i = 0; i < dev_list_len; i++) {
		libusb_device *dev = dev_list[i];
		uint8_t bus_num = libusb_get_bus_number(dev);

		/* if we already know about it, continue */
		if (busses) {
			bus = busses;
			int found = 0;
			do {
				if (bus_num == bus->location) {
					found = 1;
					break;
				}
			} while ((bus = bus->next) != NULL);
			if (found)
				continue;
		}

		/* add it to the list of busses */
		bus = malloc(sizeof(*bus));
		if (!bus)
			goto err;

		memset(bus, 0, sizeof(*bus));
		bus->location = bus_num;
		LIST_ADD(busses, bus);
	}

	libusb_free_device_list(dev_list, 1);
	*ret = busses;
	return 0;

err:
	bus = busses;
	while (bus) {
		struct usb_bus *tbus = bus->next;
		free(bus);
		bus = tbus;
	}
	return -ENOMEM;
}

API_EXPORTED int usb_find_busses(void)
{
	struct usb_bus *new_busses = NULL;
	struct usb_bus *bus;
	int changes = 0;
	int r;

	usbi_dbg("");
	
	r = find_busses(&new_busses);
	if (r < 0) {
		usbi_err("find_busses failed with error %d", r);
		return r;
	}

	/* walk through all busses we already know about, removing duplicates
	 * from the new list. if we do not find it in the new list, the bus
	 * has been removed. */

	bus = usb_busses;
	while (bus) {
		struct usb_bus *tbus = bus->next;
		struct usb_bus *nbus = new_busses;
		int found = 0;
		usbi_dbg("in loop");

		while (nbus) {
			struct usb_bus *tnbus = nbus->next;

			if (bus->location == nbus->location) {
				LIST_DEL(new_busses, nbus);
				free(nbus);
				found = 1;
				break;
			}
			nbus = tnbus;
		}

		if (!found) {
			/* bus removed */
			usbi_dbg("bus %d removed", bus->location);
			changes++;
			LIST_DEL(usb_busses, bus);
			free(bus);
		}

		bus = tbus;
	}

	/* anything remaining in new_busses is a new bus */
	bus = new_busses;
	while (bus) {
		struct usb_bus *tbus = bus->next;
		usbi_dbg("bus %d added", bus->location);
		LIST_DEL(new_busses, bus);
		LIST_ADD(usb_busses, bus);
		changes++;
		bus = tbus;
	}

	return changes;
}

static int find_devices(libusb_device **dev_list, int dev_list_len,
	struct usb_bus *bus, struct usb_device **ret)
{
	struct usb_device *devices = NULL;
	struct usb_device *dev;
	int i;

	for (i = 0; i < dev_list_len; i++) {
		libusb_device *newlib_dev = dev_list[i];
		uint8_t bus_num = libusb_get_bus_number(newlib_dev);

		if (bus_num != bus->location)
			continue;

		dev = malloc(sizeof(*dev));
		if (!dev)
			goto err;

		/* No need to reference the device now, just take the pointer. We
		 * increase the reference count later if we keep the device. */
		dev->dev = newlib_dev;

		dev->bus = bus;
		dev->devnum = libusb_get_device_address(newlib_dev);
		LIST_ADD(devices, dev);
	}

	*ret = devices;
	return 0;

err:
	dev = devices;
	while (dev) {
		struct usb_device *tdev = dev->next;
		free(dev);
		dev = tdev;
	}
	return -ENOMEM;
}

static void initialize_device(struct usb_device *dev)
{
	libusb_device *newlib_dev = dev->dev;
	const struct libusb_device_descriptor *newlib_dev_desc;

	libusb_ref_device(newlib_dev);
	newlib_dev_desc = libusb_get_device_descriptor(newlib_dev);

	/* descriptors are identical through both libs */
	memcpy(&dev->descriptor, newlib_dev_desc,
		sizeof(struct usb_device_descriptor));
	dev->config = (struct usb_config_descriptor *)
		libusb_get_config_descriptor(newlib_dev);

	/* FIXME: implement later */
	dev->num_children = 0;
	dev->children = NULL;
}

API_EXPORTED int usb_find_devices(void)
{
	struct usb_bus *bus;
	libusb_device **dev_list;
	int dev_list_len;
	int r;
	int changes = 0;

	usbi_dbg("");
	dev_list_len = libusb_get_device_list(&dev_list);
	if (dev_list_len < 0)
		return dev_list_len;

	for (bus = usb_busses; bus; bus = bus->next) {
		struct usb_device *new_devices = NULL;
		struct usb_device *dev;

		r = find_devices(dev_list, dev_list_len, bus, &new_devices);
		if (r < 0) {
			libusb_free_device_list(dev_list, 1);
			return r;
		}

		/* walk through the devices we already know about, removing duplicates
		 * from the new list. if we do not find it in the new list, the device
		 * has been removed. */
		dev = bus->devices;
		while (dev) {
			int found = 0;
			struct usb_device *tdev = dev->next;
			struct usb_device *ndev = new_devices;

			while (ndev) {
				if (ndev->devnum == dev->devnum) {
					LIST_DEL(new_devices, ndev);
					free(ndev);
					found = 1;
					break;
				}
				ndev = ndev->next;
			}

			if (!found) {
				usbi_dbg("device %d.%d removed",
					dev->bus->location, dev->devnum);
				LIST_DEL(bus->devices, dev);
				libusb_unref_device(dev->dev);
				free(dev);
				changes++;
			}

			dev = tdev;
		}

		/* anything left in new_devices is a new device */
		dev = new_devices;
		while (dev) {
			struct usb_device *tdev = dev->next;
			usbi_dbg("device %d.%d added", dev->bus->location, dev->devnum);
			LIST_DEL(new_devices, dev);
			LIST_ADD(bus->devices, dev);
			initialize_device(dev);
			changes++;
			dev = tdev;
		}
	}

	libusb_free_device_list(dev_list, 1);
	return changes;
}

API_EXPORTED struct usb_bus *usb_get_busses(void)
{
	return usb_busses;
}

API_EXPORTED usb_dev_handle *usb_open(struct usb_device *dev)
{
	usbi_dbg("");

	usb_dev_handle *udev = malloc(sizeof(*udev));
	if (!udev)
		return NULL;

	udev->handle = libusb_open((libusb_device *) dev->dev);
	if (!udev->handle) {
		usbi_err("could not open device");
		free(udev);
		return NULL;
	}

	udev->last_claimed_interface = -1;
	return udev;
}

API_EXPORTED int usb_close(usb_dev_handle *dev)
{
	usbi_dbg("");
	libusb_close(dev->handle);
	free(dev);
	return 0;
}

API_EXPORTED int usb_set_configuration(usb_dev_handle *dev, int configuration)
{
	usbi_dbg("configuration %d", configuration);
	return libusb_set_configuration(dev->handle, configuration);
}

API_EXPORTED int usb_claim_interface(usb_dev_handle *dev, int interface)
{
	int r;
	usbi_dbg("interface %d", interface);

	r = libusb_claim_interface(dev->handle, interface);
	if (r == 0) {
		dev->last_claimed_interface = interface;
		return 0;
	}

	switch (r) {
	case LIBUSB_ERROR_NO_MEM:
		return -ENOMEM;
	case LIBUSB_ERROR_BUSY:
		return -EBUSY;
	default:
		return r;
	}
}

API_EXPORTED int usb_release_interface(usb_dev_handle *dev, int interface)
{
	int r;
	usbi_dbg("interface %d", interface);

	r = libusb_release_interface(dev->handle, interface);
	if (r == 0)
		dev->last_claimed_interface = -1;

	return r;
}

API_EXPORTED int usb_set_altinterface(usb_dev_handle *dev, int alternate)
{
	usbi_dbg("alternate %d", alternate);
	if (dev->last_claimed_interface < 0)
		return -EINVAL;
	
	return libusb_set_interface_alt_setting(dev->handle,
		dev->last_claimed_interface, alternate);
}

API_EXPORTED int usb_resetep(usb_dev_handle *dev, unsigned int ep)
{
	return usb_clear_halt(dev, ep);
}

API_EXPORTED int usb_clear_halt(usb_dev_handle *dev, unsigned int ep)
{
	usbi_dbg("endpoint %x", ep);
	return libusb_clear_halt(dev->handle, ep & 0xff);
}

API_EXPORTED int usb_reset(usb_dev_handle *dev)
{
	usbi_dbg("");
	return libusb_reset_device(dev->handle);
}

static int usb_bulk_io(usb_dev_handle *dev, int ep, char *bytes,
	int size, int timeout)
{
	int actual_length;
	int r;
	usbi_dbg("endpoint %x size %d timeout %d", ep, size, timeout);
	r = libusb_bulk_transfer(dev->handle, ep & 0xff, bytes, size,
		&actual_length, timeout);
	
	/* if we timed out but did transfer some data, report as successful short
	 * read. FIXME: is this how libusb-0.1 works? */
	if (r == 0 || (r == LIBUSB_ERROR_TIMEOUT && actual_length > 0))
		return actual_length;

	return r;
}

API_EXPORTED int usb_bulk_read(usb_dev_handle *dev, int ep, char *bytes,
	int size, int timeout)
{
	return usb_bulk_io(dev, ep, bytes, size, timeout);
}

API_EXPORTED int usb_bulk_write(usb_dev_handle *dev, int ep, char *bytes,
	int size, int timeout)
{
	return usb_bulk_io(dev, ep, bytes, size, timeout);
}

static int usb_interrupt_io(usb_dev_handle *dev, int ep, char *bytes,
	int size, int timeout)
{
	int actual_length;
	int r;
	usbi_dbg("endpoint %x size %d timeout %d", ep, size, timeout);
	r = libusb_interrupt_transfer(dev->handle, ep & 0xff, bytes, size,
		&actual_length, timeout);
	
	/* if we timed out but did transfer some data, report as successful short
	 * read. FIXME: is this how libusb-0.1 works? */
	if (r == 0 || (r == LIBUSB_ERROR_TIMEOUT && actual_length > 0))
		return actual_length;

	return r;
}

API_EXPORTED int usb_interrupt_read(usb_dev_handle *dev, int ep, char *bytes,
	int size, int timeout)
{
	return usb_interrupt_io(dev, ep, bytes, size, timeout);
}

API_EXPORTED int usb_interrupt_write(usb_dev_handle *dev, int ep, char *bytes,
	int size, int timeout)
{
	return usb_interrupt_io(dev, ep, bytes, size, timeout);
}

API_EXPORTED int usb_control_msg(usb_dev_handle *dev, int bmRequestType,
	int bRequest, int wValue, int wIndex, char *bytes, int size, int timeout)
{
	int r;
	usbi_dbg("RQT=%x RQ=%x V=%x I=%x len=%d timeout=%d", bmRequestType,
		bRequest, wValue, wIndex, size, timeout);

	r = libusb_control_transfer(dev->handle, bmRequestType & 0xff,
		bRequest & 0xff, wValue & 0xffff, wIndex & 0xffff, bytes, size & 0xffff,
		timeout);

	if (r == LIBUSB_ERROR_TIMEOUT)
		return -ETIMEDOUT;
	else
		return r;
}

API_EXPORTED int usb_get_string(usb_dev_handle *dev, int desc_index, int langid,
	char *buf, size_t buflen)
{
	return libusb_get_string_descriptor(dev->handle, desc_index & 0xff,
		langid & 0xffff, buf, (int) buflen);
}

API_EXPORTED int usb_get_string_simple(usb_dev_handle *dev, int desc_index,
	char *buf, size_t buflen)
{
	return libusb_get_string_descriptor_ascii(dev->handle, desc_index & 0xff,
		buf, (int) buflen);
}

API_EXPORTED int usb_get_descriptor(usb_dev_handle *dev, unsigned char type,
	unsigned char desc_index, void *buf, int size)
{
	return libusb_get_descriptor(dev->handle, type, desc_index, buf, size);
}

API_EXPORTED int usb_get_descriptor_by_endpoint(usb_dev_handle *dev, int ep,
	unsigned char type, unsigned char desc_index, void *buf, int size)
{
	/* this function doesn't make much sense - the specs don't talk about
	 * getting a descriptor "by endpoint". libusb-1.0 does not provide this
	 * functionality so we just send a control message directly */
	return libusb_control_transfer(dev->handle,
		LIBUSB_ENDPOINT_IN | (ep & 0xff), LIBUSB_REQUEST_GET_DESCRIPTOR,
		(type << 8) | desc_index, 0, buf, size, 1000);
}

