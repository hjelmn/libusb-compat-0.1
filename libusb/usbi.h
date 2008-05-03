/*
 * Internal header for libusb-compat-0.1
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

#ifndef __LIBUSB_USBI_H__
#define __LIBUSB_USBI_H__

/* Some quick and generic macros for the simple kind of lists we use */
#define LIST_ADD(begin, ent) \
	do { \
	  if (begin) { \
	    ent->next = begin; \
	    ent->next->prev = ent; \
	  } else \
	    ent->next = NULL; \
	  ent->prev = NULL; \
	  begin = ent; \
	} while(0)

#define LIST_DEL(begin, ent) \
	do { \
	  if (ent->prev) \
	    ent->prev->next = ent->next; \
	  else \
	    begin = ent->next; \
	  if (ent->next) \
	    ent->next->prev = ent->prev; \
	  ent->prev = NULL; \
	  ent->next = NULL; \
	} while (0)

enum usbi_log_level {
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARNING,
	LOG_LEVEL_ERROR,
};

void usbi_log(enum usbi_log_level, const char *function,
	const char *format, ...);

#ifdef ENABLE_LOGGING
#define _usbi_log(level, fmt...) usbi_log(level, __FUNCTION__, fmt)
#else
#define _usbi_log(level, fmt...)
#endif

#ifdef ENABLE_DEBUG_LOGGING
#define usbi_dbg(fmt...) _usbi_log(LOG_LEVEL_DEBUG, fmt)
#else
#define usbi_dbg(fmt...)
#endif

#define usbi_info(fmt...) _usbi_log(LOG_LEVEL_INFO, fmt)
#define usbi_warn(fmt...) _usbi_log(LOG_LEVEL_WARNING, fmt)
#define usbi_err(fmt...) _usbi_log(LOG_LEVEL_ERROR, fmt)

struct usb_dev_handle {
	libusb_device_handle *handle;
};

#endif

