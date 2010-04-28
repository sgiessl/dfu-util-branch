/*
 * dfu-util - implementation DFU 1.0 action handlers
 *
 * Copyright (C) 2007-2008 by Harald Welte <laforge@gnumonks.org>
 * Copyright (C) 2010      by Sandro Giessl <sgiessl@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include <usb.h>
#include "dfu.h"
#include "dfu_sm.h"

/*
 *  DFU_DETACH Request (DFU Spec 1.0, Section 5.1)
 *
 *  returns 0 or < 0 on error
 */
static int _usb_dfu10_detach( dfu_handle *handle,
			      const unsigned short timeout )
{
	if(usb_control_msg( handle->device,
			    /* bmRequestType */
			    USB_ENDPOINT_OUT | USB_TYPE_CLASS |
			    USB_RECIP_INTERFACE,
			    /* bRequest      */ USB_REQ_DFU_DETACH,
			    /* wValue        */ timeout,
			    /* wIndex        */ handle->interface,
			    /* Data          */ NULL,
			    /* wLength       */ 0,
			    handle->usb_timeout ) < 0)
	{
		fprintf( stderr,
			 "%s: USB transaction failed (current state: %s): %s\n",
			 __FUNCTION__,
			 dfu_state_to_string(dfu_sm_get_state(handle)),
			 usb_strerror());
		return -1;
	}

	return 0;
}

/*
 *  DFU USB Reset
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *
 *  returns 0 or < 0 on error
 */
static int _usb_dfu10_usb_reset( dfu_handle *handle)
{
	int ret = -1;

	if( (ret = usb_reset(handle->device)) < 0 && ret != -ENODEV)
	{
		fprintf( stderr,
			 "%s: USB transaction failed (current state: %s): %s\n",
			 __FUNCTION__,
			 dfu_state_to_string(dfu_sm_get_state(handle)),
			 usb_strerror());
		return -1;
	}

	return 0;
}

/*
 * perform/await DFU status poll timeout
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  poll_timeout - the timeout the host is expected to wait, in milliseconds
 *
 *  returns 0 or < 0 on error
 */
static int _usb_dfu10_status_poll_timeout( dfu_handle *handle,
					   unsigned int poll_timeout )
{
	/* wait for timeout */
	usleep(poll_timeout*1000);

	return 0;
}

/*
 *  DFU_DNLOAD Request (DFU Spec 1.0, Section 6.1.1)
 *
 *  length    - the total number of bytes to transfer to the USB
 *              device - must be less than wTransferSize
 *  data      - the data to transfer
 *
 *  returns the number of bytes written or < 0 on error
 */
static int _usb_dfu10_download( dfu_handle *handle,
				const int transaction,
				const unsigned short length,
				char* data )
{
	int ret = -1;

	if( (ret = usb_control_msg( handle->device,
				    /* bmRequestType */ USB_ENDPOINT_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				    /* bRequest      */ USB_REQ_DFU_DNLOAD,
				    /* wValue        */ transaction,
				    /* wIndex        */ handle->interface,
				    /* Data          */ data,
				    /* wLength       */ length,
				    handle->usb_timeout )) < 0)
	{
		fprintf( stderr,
			 "%s: USB transaction failed (current state: %s): %s\n",
			 __FUNCTION__,
			 dfu_state_to_string(dfu_sm_get_state(handle)),
			 usb_strerror() );
		return -1;
	}

	return ret;
}

/*
 *  DFU_UPLOAD Request (DFU Spec 1.0, Section 6.2)
 *
 *  length    - the maximum number of bytes to receive from the USB
 *              device - must be less than wTransferSize
 *  data      - the buffer to put the received data in
 *
 *  returns the number of bytes received or < 0 on error
 */
static int _usb_dfu10_upload( dfu_handle *handle,
			      const int transaction,
			      const unsigned short length,
			      char* data )
{
	int ret = -1;

	if( (ret = usb_control_msg( handle->device,
				    /* bmRequestType */ USB_ENDPOINT_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				    /* bRequest      */ USB_REQ_DFU_UPLOAD,
				    /* wValue        */ transaction,
				    /* wIndex        */ handle->interface,
				    /* Data          */ data,
				    /* wLength       */ length,
				    handle->usb_timeout )) < 0)
	{
		fprintf( stderr,
			 "%s: USB transaction failed (current state: %s): %s\n",
			 __FUNCTION__,
			 dfu_state_to_string(dfu_sm_get_state(handle)),
			 usb_strerror() );
		return -1;
	}

	return ret;
}

/*
 *  DFU_GETSTATUS Request (DFU Spec 1.0, Section 6.1.2)
 *
 *  status    - the data structure to be populated with the results
 *
 *  return the number of bytes read in or < 0 on an error
 */
static int _usb_dfu10_get_status( dfu_handle *handle,
				  struct dfu_status *status )
{
	char buffer[6];

	if(usb_control_msg( handle->device,
			    /* bmRequestType */ USB_ENDPOINT_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			    /* bRequest      */ USB_REQ_DFU_GETSTATUS,
			    /* wValue        */ 0,
			    /* wIndex        */ handle->interface,
			    /* Data          */ buffer,
			    /* wLength       */ sizeof(buffer),
			    handle->usb_timeout ) != 6)
	{
		fprintf( stderr,
			 "%s: USB transaction failed (current state: %s): %s\n",
			 __FUNCTION__,
			 dfu_state_to_string(dfu_sm_get_state(handle)),
			 usb_strerror());
		return -1;
	}

	/* convert binary result to cpu byte ordered struct */
	status->bStatus = buffer[0];
	status->bwPollTimeout = ((0xff & buffer[3]) << 16) |
		((0xff & buffer[2]) << 8)  |
		(0xff & buffer[1]);
	status->bState  = buffer[4];
	status->iString = buffer[5];

	return 0;
}

/*
 *  DFU_CLRSTATUS Request (DFU Spec 1.0, Section 6.1.3)
 *
 *  return 0 or < 0 on an error
 */
static int _usb_dfu10_clear_status( dfu_handle *handle)
{
	if( usb_control_msg( handle->device,
			     /* bmRequestType */ USB_ENDPOINT_OUT| USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			     /* bRequest      */ USB_REQ_DFU_CLRSTATUS,
			     /* wValue        */ 0,
			     /* wIndex        */ handle->interface,
			     /* Data          */ NULL,
			     /* wLength       */ 0,
			     handle->usb_timeout ) < 0)
	{
		fprintf( stderr,
			 "%s: USB transaction failed (current state: %s): %s\n",
			 __FUNCTION__,
			 dfu_state_to_string(dfu_sm_get_state(handle)),
			 usb_strerror());
		return -1;
	}

	return 0;
}

/*
 *  DFU_GETSTATE Request (DFU Spec 1.0, Section 6.1.5)
 *
 *  length    - the maximum number of bytes to receive from the USB
 *              device - must be less than wTransferSize
 *  data      - the buffer to put the received data in
 *
 *  returns the state or < 0 on error
 */
static int _usb_dfu10_get_state( dfu_handle *handle)
{
	char buffer[1];

	if(usb_control_msg( handle->device,
			    /* bmRequestType */ USB_ENDPOINT_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			    /* bRequest      */ USB_REQ_DFU_GETSTATE,
			    /* wValue        */ 0,
			    /* wIndex        */ handle->interface,
			    /* Data          */ buffer,
			    /* wLength       */ 1,
			    handle->usb_timeout ) < 0)
	{
		fprintf( stderr,
			 "%s: USB transaction failed (current state: %s): %s\n",
			 __FUNCTION__,
			 dfu_state_to_string(dfu_sm_get_state(handle)), usb_strerror());
		return -1;
	}

	return buffer[0];
}

/*
 *  DFU_ABORT Request (DFU Spec 1.0, Section 6.1.4)
 *
 *  returns 0 or < 0 on an error
 */
static int _usb_dfu10_abort( dfu_handle *handle)
{
	if(usb_control_msg( handle->device,
			    /* bmRequestType */ USB_ENDPOINT_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			    /* bRequest      */ USB_REQ_DFU_ABORT,
			    /* wValue        */ 0,
			    /* wIndex        */ handle->interface,
			    /* Data          */ NULL,
			    /* wLength       */ 0,
			    handle->usb_timeout ) < 0)
	{
		fprintf( stderr, "%s: USB transaction failed (current state: %s): %s\n",
			 __FUNCTION__,
			 dfu_state_to_string(dfu_sm_get_state(handle)),
			 usb_strerror());
		return -1;
	}

	return 0;
}

const struct dfu_transition_handlers *usb_dfu_handlers(enum DFU_VERSION version)
{
	static struct dfu_transition_handlers handlers = {
		.detach = _usb_dfu10_detach,
		.device_reset = _usb_dfu10_usb_reset,
		.status_poll_timeout = _usb_dfu10_status_poll_timeout,
		.download = _usb_dfu10_download,
		.upload = _usb_dfu10_upload,
		.get_status = _usb_dfu10_get_status,
		.get_state = _usb_dfu10_get_state,
		.clear_status = _usb_dfu10_clear_status,
		.abort = _usb_dfu10_abort
	};

	/* as of now, handlers for both DFU 1.0 and DFU 1.1 are equal */
	return &handlers;
}

