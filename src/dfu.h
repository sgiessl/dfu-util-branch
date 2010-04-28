/*
 * dfu-programmer
 *
 * $Id: dfu.h,v 1.2 2005/09/25 01:27:42 schmidtw Exp $
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
 */

#ifndef __DFU_H__
#define __DFU_H__

#include <usb.h>
#include "usb_dfu.h"
#include "dfu_quirks.h"

/* This is based off of DFU_GETSTATUS
 *
 *  1 unsigned byte bStatus
 *  3 unsigned byte bwPollTimeout
 *  1 unsigned byte bState
 *  1 unsigned byte iString
*/

/* DFU 1.0 status structure, with the only difference of
   bwPollTimeout having been converted from little-endian to
   host-specific endianness. */
struct dfu_status {
    unsigned char bStatus;
    unsigned int  bwPollTimeout;
    unsigned char bState;
    unsigned char iString;
};

/**
 * describe a current DFU specification version being used
 *
 * note: DFU 1.1 isn't supported yet, it's in mainly for dealing with
 * some DFU 1.1 stuff already implemented.
 */
enum DFU_VERSION {
	DFU_VERSION_1_0 = 0,
	DFU_VERSION_1_1,
};

/* dfu-util specific: structure containing various sorts of control
   information specific to the device we are currently attached to. */
typedef struct _dfu_handle
{
	struct usb_dev_handle *device;
	unsigned short interface;
	/* usb timeout setting: msecs before a usb request fails */
	unsigned int usb_timeout;
	/* DFU functional descriptor, containing some device
	   configuration info */
	struct usb_dfu_func_descriptor func_dfu;
	/* latest known / expected DFU device state. this shouldn't be
	   accessed directly, but it's tracked automatically via dfu_*
	   and dfu_sm_* functions. */
	unsigned int dfu_state;
	/* dfu upload/download request count */
	unsigned short transaction;
	/* dfu version being used */
	unsigned int dfu_ver;
	/* a set of quirks documenting the difference from the
	   currently selected DFU version */
	dfu_quirks quirk_flags;
} dfu_handle;

/* portable USB data endianness conversion */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define cpu_to_le16(d)  (d)
#define cpu_to_le32(d)  (d)
#define le16_to_cpu(d)  (d)
#define le32_to_cpu(d)  (d)
#elif __BYTE_ORDER == __BIG_ENDIAN
#define cpu_to_le16(d)  bswap_16(d)
#define cpu_to_le32(d)  bswap_32(d)
#define le16_to_cpu(d)  bswap_16(d)
#define le32_to_cpu(d)  bswap_32(d)
#else
#error "Unknown byte order"
#endif

void dfu_init( dfu_handle *handle,
	       const int usb_timeout);

void dfu_debug( const int level );
int dfu_detach(dfu_handle *handle,
                const unsigned short timeout );
int dfu_usb_reset(dfu_handle *handle);
int dfu_status_poll_timeout(dfu_handle *handle,
			     unsigned int poll_timeout );
int dfu_download( dfu_handle *handle,
                  const unsigned short length,
                  char* data );
int dfu_upload( dfu_handle *handle,
                const unsigned short length,
                char* data );
int dfu_get_status( dfu_handle *handle,
                    struct dfu_status *status );
int dfu_clear_status( dfu_handle *handle );
int dfu_get_state( dfu_handle *handle );
int dfu_abort( dfu_handle *handle );

char* dfu_state_to_string( int state );

const char *dfu_status_to_string(int status);

const char *dfu_func_descriptor_to_string(struct usb_dfu_func_descriptor *func_desc);

int add_file_suffix(const char *fname);

/**
 * Descriptor of handlers for a specific DFU implementation
 */
struct dfu_transition_handlers {
	int (*detach)( dfu_handle *handle,
		       const unsigned short timeout );
	int (*device_reset)( dfu_handle *handle );
	int (*status_poll_timeout)( dfu_handle *handle,
				    unsigned int poll_timeout );
	int (*download)( dfu_handle *handle,
			 const int transaction,
			 const unsigned short length,
			 char* data );
	int (*upload)( dfu_handle *handle,
		       const int transaction,
		       const unsigned short length,
		       char* data );
	int (*get_status)( dfu_handle *handle,
			   struct dfu_status *status );
	int (*clear_status)( dfu_handle *handle );
	int (*get_state)( dfu_handle *handle );
	int (*abort)( dfu_handle *handle );
};

/**
 * DFU 1.0 transition handlers - defined in usb_dfu10.c
 */
const struct dfu_transition_handlers *usb_dfu_handlers(enum DFU_VERSION version);


int debug;

#endif
