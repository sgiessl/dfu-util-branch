/*
 * dfu-util - DFU CRC / file suffix
 *
 * Copyright (C) 2007-2008 by Harald Welte <laforge@gnumonks.org>
 * Copyright (C) 2010      by Sandro Giessl <s.giessl@niftylight.de>
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
#include "crc32.h"

static int _dfu_verify_init(dfu_handle *handle, const char *function );

/* ugly hack for Win32 */
#ifndef O_BINARY
#define O_BINARY 0
#endif

#define INVALID_DFU_TIMEOUT -1

static int dfu_debug_level = 0;

void dfu_init( dfu_handle *handle,
	      const int usb_timeout)
{
	if(!handle)
		fprintf( stderr, "dfu_init: Invalid handle.\n" );

	handle->device = NULL;
	handle->interface = 0;
	handle->dfu_ver = DFU_VERSION_1_0;

	dfu_sm_set_state_unchecked(handle, DFU_STATE_appIDLE);

	handle->transaction = 0;

	handle->usb_timeout = -1;
	if( usb_timeout > 0 ) {
		handle->usb_timeout = usb_timeout;
	} else {
		if( 0 != dfu_debug_level )
			fprintf( stderr, "dfu_init: Invalid timeout value.\n" );
	}
}

static int _dfu_verify_init(dfu_handle *handle, const char *function )
{
    if( !handle || INVALID_DFU_TIMEOUT == handle->usb_timeout ) {
        if( 0 != dfu_debug_level )
            fprintf( stderr,
                     "%s: dfu system not property initialized.\n",
                     function );
        return -1;
    }

    return 0;
}

void dfu_debug( const int level )
{
    dfu_debug_level = level;
}

/* ensure that the device state is equal to the currently expected
   device state */
static int _dfu_state_verify(dfu_handle *handle,
			     int expected_state,
			     const char *function)
{
	/* only do the validation in states where it's allowed to
	   request the device's state. */
	if(dfu_sm_get_next_state(handle, DFU_EV_GETSTATE, 0) < 0)
	{
		/* otherwise silently return, without invoking an
		   error. */
		return 0;
	}

	int device_state = dfu_get_state(handle);

	if(device_state != expected_state)
	{
		fprintf( stderr,
			 "%s: FATAL! The DFU device is in state %s, but we expected it to be in %s\n",
			 function,
			 dfu_state_to_string(device_state),
			 dfu_state_to_string(expected_state));
		return -1;
	}

	return 0;
}

/*
 *  DFU_DETACH Request (DFU Spec 1.0, Section 5.1)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  timeout   - the timeout in ms the USB device should wait for a pending
 *              USB reset before giving up and terminating the operation
 *
 *  returns 0 or < 0 on error
 */
int dfu_detach( dfu_handle *handle,
                const unsigned short timeout )
{
	int next_state = -1;

	if( (next_state = dfu_sm_get_next_state(handle, DFU_EV_DETACH, 0)) < 0)
		return -1;

	/* do the actual "work" */
	if(usb_dfu_handlers(handle->dfu_ver)->detach(handle,
					timeout) < 0)
		return -1;

	if(_dfu_state_verify(handle, next_state, __FUNCTION__) < 0)
		return -1;

	return dfu_sm_set_state_checked(handle, next_state);
}

/*
 *  DFU USB Reset
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *
 *  returns 0 or < 0 on error
 */
int dfu_usb_reset( dfu_handle *handle )
{
	int ret = -1;
	int next_state = -1;

	if( 0 != _dfu_verify_init(handle, __FUNCTION__) )
		return -1;

	if( (next_state = dfu_sm_get_next_state(handle, DFU_EV_USB_RESET, 0)) < 0)
		return -1;

	/* do the actual "work" */
	if( (ret = usb_dfu_handlers(handle->dfu_ver)->device_reset(handle)) < 0)
		return -1;

	return dfu_sm_set_state_checked(handle, next_state);
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
int dfu_status_poll_timeout( dfu_handle *handle,
			     unsigned int poll_timeout )
{
	int next_state = -1;

	if( 0 != _dfu_verify_init(handle, __FUNCTION__) )
		return -1;

	int guards = 0;

	if(handle->func_dfu.bmAttributes & USB_DFU_MANIFEST_TOL)
		guards |= DFU_GUARD_BIT_MANIFESTATION_TOLERANT;

	if( (next_state = dfu_sm_get_next_state(handle, DFU_EV_STATUS_POLL_TIMEOUT, guards)) < 0)
		return -1;

	/* do the actual "work" */
	if(usb_dfu_handlers(handle->dfu_ver)->status_poll_timeout(handle,
						  poll_timeout) < 0)
		return -1;

	/* if(_dfu_state_verify(handle, next_state, __FUNCTION__) < 0) */
	/* 	return -1; */

	return dfu_sm_set_state_checked(handle, next_state);
}

/*
 *  DFU_DNLOAD Request (DFU Spec 1.0, Section 6.1.1)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  length    - the total number of bytes to transfer to the USB
 *              device - must be less than wTransferSize
 *  data      - the data to transfer
 *
 *  returns the number of bytes written or < 0 on error
 */
int dfu_download( dfu_handle *handle,
                  const unsigned short length,
                  char* data )
{
	int ret = -1;
	int next_state = -1;

	if( 0 != _dfu_verify_init(handle, __FUNCTION__) )
		return -1;

	/* Sanity checks */
	if( (0 != length) && (NULL == data) ) {
		if( 0 != dfu_debug_level )
			fprintf( stderr,
				 "%s: data was NULL, but length != 0\n",
				 __FUNCTION__ );
		return -1;
	}
	if( (0 == length) && (NULL != data) ) {
		if( 0 != dfu_debug_level )
			fprintf( stderr,
				 "%s: data was not NULL, but length == 0\n",
				 __FUNCTION__ );
		return -2;
	}

	int guards = 0;
	if(length > 0)
		guards |= DFU_GUARD_WLENGTH_GT_ZERO;
	if(handle->func_dfu.bmAttributes & USB_DFU_CAN_DOWNLOAD)
		guards |= DFU_GUARD_BIT_CAN_DNLOAD;

	if( (next_state = dfu_sm_get_next_state(handle, DFU_EV_DNLOAD, guards)) < 0)
		return -1;

	/* do the actual "work" */
	if( (ret = usb_dfu_handlers(handle->dfu_ver)->download(handle,
					       handle->transaction++,
					       length, data)) < 0)
		return -1;

	/* if(_dfu_state_verify(handle, next_state, __FUNCTION__) < 0) */
	/* 	return -1; */

	if(dfu_sm_set_state_checked(handle, next_state) < 0)
		return -1;

	return ret;
}

/*
 *  DFU_UPLOAD Request (DFU Spec 1.0, Section 6.2)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  length    - the maximum number of bytes to receive from the USB
 *              device - must be less than wTransferSize
 *  data      - the buffer to put the received data in
 *
 *  returns the number of bytes received or < 0 on error
 */
int dfu_upload( dfu_handle *handle,
                const unsigned short length,
                char* data )
{
	int ret = -1;
	int next_state = -1;

	if( 0 != _dfu_verify_init(handle, __FUNCTION__) )
		return -1;

	/* Sanity checks */
	if( (0 == length) || (NULL == data) ) {
		if( 0 != dfu_debug_level )
			fprintf( stderr,
				 "%s: data was NULL, or length is 0\n",
				 __FUNCTION__ );
		return -1;
	}

	if(!dfu_sm_state_has_event(handle, DFU_EV_UPLOAD))
		return -1;

	/* do the actual "work" */
	if( (ret = usb_dfu_handlers(handle->dfu_ver)->upload(handle,
					     handle->transaction++,
					     length, data)) < 0)
	{
		return -1;
	}

	/* determine next state & do state transition */
	int guards = 0;
	if(handle->func_dfu.bmAttributes & USB_DFU_CAN_UPLOAD)
		guards |= DFU_GUARD_BIT_CAN_UPLOAD;
	if(length > 0)
		guards |= DFU_GUARD_WLENGTH_GT_ZERO;
	if(ret < length)
		guards |= DFU_GUARD_UPLOAD_SHORT_FRAME;

	if( (next_state = dfu_sm_get_next_state(handle, DFU_EV_UPLOAD, guards)) < 0)
		return -1;

	if(_dfu_state_verify(handle, next_state, __FUNCTION__) < 0)
		return -1;

	if(dfu_sm_set_state_checked(handle, next_state) < 0)
		return -1;

	return ret;
}

/*
 *  DFU_GETSTATUS Request (DFU Spec 1.0, Section 6.1.2)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *  status    - the data structure to be populated with the results
 *
 *  return the number of bytes read in or < 0 on an error
 */
int dfu_get_status( dfu_handle *handle,
                    struct dfu_status *status )
{
	int next_state = -1;

	if( 0 != _dfu_verify_init(handle, __FUNCTION__) )
		return -1;

	if(!dfu_sm_state_has_event(handle, DFU_EV_GETSTATUS))
		return -1;

	/* do the actual "work" */
	if(usb_dfu_handlers(handle->dfu_ver)->get_status( handle,
					  status) < 0)
		return -1;

	next_state = status->bState;

	return dfu_sm_set_state_checked(handle, next_state);
}

/*
 *  DFU_CLRSTATUS Request (DFU Spec 1.0, Section 6.1.3)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *
 *  return 0 or < 0 on an error
 */
int dfu_clear_status( dfu_handle *handle )
{
	int next_state = -1;

	if( 0 != _dfu_verify_init(handle, __FUNCTION__) )
		return -1;

	if( (next_state = dfu_sm_get_next_state(handle, DFU_EV_CLRSTATUS, 0)) < 0)
		return -1;

	/* do the actual "work" */
	if( usb_dfu_handlers(handle->dfu_ver)->clear_status(handle) < 0)
		return -1;


	if(_dfu_state_verify(handle, next_state, __FUNCTION__) < 0)
		return -1;

	return dfu_sm_set_state_checked(handle, next_state);
}

/*
 *  DFU_GETSTATE Request (DFU Spec 1.0, Section 6.1.5)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *
 *  returns the state or < 0 on error
 */
int dfu_get_state( dfu_handle *handle )
{
	int ret;
	int next_state = -1;

	if( 0 != _dfu_verify_init(handle, __FUNCTION__) )
		return -1;

	if( (next_state = dfu_sm_get_next_state(handle, DFU_EV_GETSTATE, 0)) < 0)
		return -1;


	/* do the actual "work" */
	if( (ret = usb_dfu_handlers(handle->dfu_ver)->get_state(handle)) < 0)
		return -1;


	/* do not validate the current state here. */

	if(dfu_sm_set_state_checked(handle, next_state) < 0)
		return -1;

	/* return state */
	return ret;
}

/*
 *  DFU_ABORT Request (DFU Spec 1.0, Section 6.1.4)
 *
 *  device    - the usb_dev_handle to communicate with
 *  interface - the interface to communicate with
 *
 *  returns 0 or < 0 on an error
 */
int dfu_abort( dfu_handle *handle )
{
	int next_state = -1;

	if( 0 != _dfu_verify_init(handle, __FUNCTION__) )
		return -1;

	if( (next_state = dfu_sm_get_next_state(handle, DFU_EV_ABORT, 0)) < 0)
		return -1;


	/* do the actual "work" */
	if(usb_dfu_handlers(handle->dfu_ver)->abort(handle) < 0)
		return -1;


	if(_dfu_state_verify(handle, next_state, __FUNCTION__) < 0)
		return -1;

	return dfu_sm_set_state_checked(handle, next_state);
}


char* dfu_state_to_string( int state )
{
    char *message = NULL;

    switch( state ) {
        case DFU_STATE_appIDLE:
            message = "appIDLE";
            break;
        case DFU_STATE_appDETACH:
            message = "appDETACH";
            break;
        case DFU_STATE_dfuIDLE:
            message = "dfuIDLE";
            break;
        case DFU_STATE_dfuDNLOAD_SYNC:
            message = "dfuDNLOAD-SYNC";
            break;
        case DFU_STATE_dfuDNBUSY:
            message = "dfuDNBUSY";
            break;
        case DFU_STATE_dfuDNLOAD_IDLE:
            message = "dfuDNLOAD-IDLE";
            break;
        case DFU_STATE_dfuMANIFEST_SYNC:
            message = "dfuMANIFEST-SYNC";
            break;
        case DFU_STATE_dfuMANIFEST:
            message = "dfuMANIFEST";
            break;
        case DFU_STATE_dfuMANIFEST_WAIT_RESET:
            message = "dfuMANIFEST-WAIT-RESET";
            break;
        case DFU_STATE_dfuUPLOAD_IDLE:
            message = "dfuUPLOAD-IDLE";
            break;
        case DFU_STATE_dfuERROR:
            message = "dfuERROR";
            break;
        default:
		message = "n/a";
		break;
    }

    return message;
}

/* Chapter 6.1.2 */
static const char *dfu_status_names[] = {
	[DFU_STATUS_OK]			= "No error condition is present",
	[DFU_STATUS_errTARGET]		= 
		"File is not targeted for use by this device",
	[DFU_STATUS_errFILE]		=
		"File is for this device but fails some vendor-specific test",
	[DFU_STATUS_errWRITE]		=
		"Device is unable to write memory",
	[DFU_STATUS_errERASE]		=
		"Memory erase function failed",
	[DFU_STATUS_errCHECK_ERASED]	=
		"Memory erase check failed",
	[DFU_STATUS_errPROG]		=
		"Program memory function failed",
	[DFU_STATUS_errVERIFY]		=
		"Programmed emmory failed verification",
	[DFU_STATUS_errADDRESS]		=
		"Cannot program memory due to received address that is out of range",
	[DFU_STATUS_errNOTDONE]		=
		"Received DFU_DNLOAD with wLength = 0, but device does not think that it has all data yet",
	[DFU_STATUS_errFIRMWARE]	=
		"Device's firmware is corrupt. It cannot return to run-time (non-DFU) operations",
	[DFU_STATUS_errVENDOR]		=
		"iString indicates a vendor specific error",
	[DFU_STATUS_errUSBR]		=
		"Device detected unexpected USB reset signalling",
	[DFU_STATUS_errPOR]		=
		"Device detected unexpected power on reset",
	[DFU_STATUS_errUNKNOWN]		=
		"Something went wrong, but the device does not know what it was",
	[DFU_STATUS_errSTALLEDPKT]	=
		"Device stalled an unexpected request",
};


const char *dfu_status_to_string(int status)
{
	if (status > DFU_STATUS_errSTALLEDPKT)
		return "INVALID";
	return dfu_status_names[status];
}

/*
 * Make a human readable list of provided guards. returned is a
 * pointer to a static string buffer, so this is NOT multithreading
 * safe, and you shouldn't keep a reference to the pointer.
 */
const char *dfu_func_descriptor_to_string(struct usb_dfu_func_descriptor *func_desc)
{
	static char str_buffer[512] = "";
	size_t str_buffer_size = 0;

	str_buffer[0] = '\0';

	int ret = 0;

	if( (ret = snprintf(str_buffer, sizeof(str_buffer)-str_buffer_size,
			    "wTransferSize = %d, bcdDFUVersion = 0x%.2x, bmAttributes = ",
			    le16_to_cpu(func_desc->wTransferSize),
			    le16_to_cpu(func_desc->bcdDFUVersion))) <= 0 )
		return "";
	str_buffer_size += ret;

	if(func_desc->bmAttributes & USB_DFU_CAN_DOWNLOAD)
	{
		if( (ret = snprintf(&str_buffer[str_buffer_size],
				    sizeof(str_buffer)-str_buffer_size,
				    "%s ", "bitCanDownload")) <= 0 )
			return "";
		str_buffer_size += ret;
	}

	if(func_desc->bmAttributes & USB_DFU_CAN_UPLOAD)
	{
		if( (ret = snprintf(&str_buffer[str_buffer_size],
				    sizeof(str_buffer)-str_buffer_size,
				    "%s ", "bitCanUpload")) <= 0 )
			return "";
		str_buffer_size += ret;
	}

	if(func_desc->bmAttributes & USB_DFU_MANIFEST_TOL)
	{
		if( (ret = snprintf(&str_buffer[str_buffer_size],
				    sizeof(str_buffer)-str_buffer_size,
				    "%s ", "bitManifestationTolerant")) <= 0 )
			return "";
		str_buffer_size += ret;
	}

	if(func_desc->bmAttributes & USB_DFU_WILL_DETACH)
	{
		if( (ret = snprintf(&str_buffer[str_buffer_size],
				    sizeof(str_buffer)-str_buffer_size,
				    "%s ", "bitWillDetach")) <= 0 )
			return "";
		str_buffer_size += ret;
	}

	return str_buffer;
}
