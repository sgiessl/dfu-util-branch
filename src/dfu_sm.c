/*
 * dfu-util - statemachine
 *
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
#include <string.h>

#include "dfu.h"
#include "dfu_sm.h"
#include "usb_dfu.h"

static int dfu_state = DFU_STATE_dfuERROR;

static const char *dfu_event_names[] = {
	[DFU_EV_DETACH]		= "DFU_DETACH",
	[DFU_EV_DNLOAD]		= "DFU_DNLOAD",
	[DFU_EV_UPLOAD]		= "DFU_UPLOAD",
	[DFU_EV_GETSTATUS]		= "DFU_GETSTATUS",
	[DFU_EV_CLRSTATUS]		= "DFU_CLRSTATUS",
	[DFU_EV_GETSTATE]	        = "DFU_GETSTATE",
	[DFU_EV_ABORT]		= "DFU_ABORT",
	[DFU_EV_USB_RESET]	= "USB Reset",
	[DFU_EV_STATUS_POLL_TIMEOUT]	= "Status Poll Timeout",
	[DFU_EV_DETACH_TIMEOUT]	        = "Detach Timeout",
	[DFU_EV_INVALID_DFU_REQUEST]    = "Invalid DFU class-specific request"
};

static const char *dfu_sm_guard_names[] = {
	[DFU_GUARD_WLENGTH_GT_ZERO] = "wLength>0",
	[DFU_GUARD_UPLOAD_SHORT_FRAME] = "Short Frame",
	[DFU_GUARD_BLOCK_IN_PROGRESS] = "Block in Progress",
	[DFU_GUARD_MANIFESTATION_IN_PROGRESS] = "Manifestation in Progress",
	[DFU_GUARD_BIT_CAN_DNLOAD] = "bitCanDownload",
	[DFU_GUARD_BIT_MANIFESTATION_TOLERANT] = "bitManifestationTolerant",
	[DFU_GUARD_BIT_CAN_UPLOAD] = "bitCanUpload"
};

const char *dfu_sm_event_to_string(enum DFU_SM_EVENT event)
{
	if (event > (sizeof(dfu_event_names)/(sizeof(*dfu_event_names))))
		return "INVALID";
	if (dfu_event_names[event] == 0)
		return "INVALID";
	return dfu_event_names[event];
}

/*
 * Make a human readable list of provided guards. returned is a
 * pointer to a static string buffer, so this is NOT multithreading
 * safe, and you shouldn't keep a reference to the pointer.
 */
const char *dfu_sm_guards_to_string(int guard_flags)
{
	static char guard_buffer[512] = "";
	int i = 0;

	guard_buffer[0] = '\0';

	for(i = 0; i < dfu_event_guard_flags_count; ++i)
	{
		if(guard_flags & (1<<i) && dfu_sm_guard_names[i] != NULL)
		{
			if(guard_buffer[0] != '\0')
				strcat(guard_buffer, "|");
			strcat(guard_buffer, dfu_sm_guard_names[i]);
		}
	}

	return guard_buffer;
}

/**
 * Evaluate a event within the finite state machine. Complies to DFU 1.0 and DFU 1.1
 *
 * @param[in] event - event ID
 * @param[in] guardflags - flags of event guards
 * @param[out] event_exists - wether a event with ID event exists in current
 * @param[in] silent - be silent and don't bark on event errors
 state, but it does not necessarily need to be allowed (depends on the actual @p guardflags)
 */
static int _dfu_sm_get_next_state(enum DFU_SM_EVENT event, unsigned int guardflags, int *event_exists, int silent)
{
	int event_exists_dummy;
	if(!event_exists)
		event_exists = &event_exists_dummy;

	*event_exists = 0;

	int next = -1;

	/* DFU 1.0, A.2 */
	/* DFU 1.1, A.2 */

	switch(dfu_state)
	{
	case DFU_STATE_appIDLE:
		/* A.2.1 */
		switch(event)
		{
		case DFU_EV_DETACH:
			/* host wants to initiate DFU process; device
			   starts detach timer. */

			/* DFU 1.1:
			   bitWillDetach means the device generates
			   detach-attach sequence on the bus itself,
			   otherwise it's done as in 1.0 */

			*event_exists = 1;
			next = DFU_STATE_appDETACH;
			break;


		case DFU_EV_GETSTATUS:
		case DFU_EV_GETSTATE:
			/* - DFU_EV_GETSTATUS: may be optionally
			     treated as unsupported request. if
			     supported, bwPollTimeout is ignored by
			     the host

			   - DFU_EV_GETSTATE: may be optionally
			     treated as unsupported request req. */
			*event_exists = 1;
			next = dfu_state;
			break;

		default:
			/* any unsupported request stalls control pipe. */
			break;
		}
		break;

	case DFU_STATE_appDETACH:
		/* A.2.2 */
		switch(event)
		{
		case DFU_EV_GETSTATUS:
		case DFU_EV_GETSTATE:
			/* DFU_EV_GETSTATUS: bwPollTimeout is ignored. */
			*event_exists = 1;
			next = dfu_state;
			break;

		case DFU_EV_POWER_RESET:
			/* lose all DFU context, operate normally.

			   note: I don't know how this could be
			   detected by dfu-util. */

			*event_exists = 1;
			next = DFU_STATE_dfuIDLE;
			break;

		case DFU_EV_USB_RESET:
			/* if detach timer is running: enumerate DFU
			   descriptors, enter DFU mode */
			*event_exists = 1;
			if(guardflags & DFU_GUARD_DETACH_TIMER_ELAPSED)
			{
				/* it's likely the device isn't
				   actually in appDETACH state
				   anymore, due to the timer
				   elapsing! */
				next = DFU_STATE_appIDLE;
			}
			else
			{
				next = DFU_STATE_dfuIDLE;
			}
			break;

		default:
			/* control pipe stall, and appIDLE event */
			*event_exists = 1;
			next = DFU_STATE_appIDLE;
			break;
		}
		break;


	case DFU_STATE_dfuIDLE:
		/* A.2.3 */

		switch(event)
		{
		case DFU_EV_DNLOAD:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_WLENGTH_GT_ZERO &&
			   guardflags & DFU_GUARD_BIT_CAN_DNLOAD)
			{
				/* start of a download block. */
				next = DFU_STATE_dfuDNLOAD_SYNC;
			}
			else
			{
				/* wLength = 0, or bitCanDownload = 0:
				   control pipe stall */
				next = DFU_STATE_dfuERROR;
			}
			break;

		case DFU_EV_UPLOAD:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_BIT_CAN_UPLOAD)
			{
				/* start of an upload block */
				next = DFU_STATE_dfuUPLOAD_IDLE;
			}
			else
			{
				/* device stalls control pipe */
				next = DFU_STATE_dfuERROR;
			}
			break;

		case DFU_EV_ABORT:
			/* do nothing */
		case DFU_EV_GETSTATUS:
			/* answer */
		case DFU_EV_GETSTATE:
			/* answer */
			*event_exists = 1;
			next = DFU_STATE_dfuIDLE;
			break;

		case DFU_EV_POWER_RESET:
		case DFU_EV_USB_RESET:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_FIRMWARE_VALID)
				next = DFU_STATE_appIDLE;
			else
			{
				/* await recovery attempts by the host. */
				next = DFU_STATE_dfuERROR;
			}
			break;

		case DFU_EV_INVALID_DFU_REQUEST:
		default:
			/* device stalls control pipe */
			*event_exists = 1;
			next = DFU_STATE_dfuERROR;
			break;
		}
		break;

	case DFU_STATE_dfuDNLOAD_SYNC:
		/* A.2.4 */

		switch(event)
		{
		case DFU_EV_GETSTATUS:

			*event_exists = 1;
			if(guardflags & DFU_GUARD_BLOCK_IN_PROGRESS)
				next = DFU_STATE_dfuDNBUSY;
			else
				next = DFU_STATE_dfuDNLOAD_IDLE;
			break;


		case DFU_EV_GETSTATE:
			*event_exists = 1;
			next = dfu_state;
			break;

		case DFU_EV_ABORT:
			/* this is NOT specified in A.2.4, but in the
			   diagram on page 26. I guess it's intended
			   to be here. */
			*event_exists = 1;
			next = DFU_STATE_dfuIDLE;
			break;

		case DFU_EV_POWER_RESET:
		case DFU_EV_USB_RESET:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_FIRMWARE_VALID)
				next = DFU_STATE_appIDLE;
			else
				next = DFU_STATE_dfuERROR;
			break;

		case DFU_EV_INVALID_DFU_REQUEST:
		default:
			/* control pipe stall */
			*event_exists = 1;
			next = DFU_STATE_dfuIDLE;
			break;
		}
		break;

	case DFU_STATE_dfuDNBUSY:
		/* A.2.5 */

		switch(event)
		{
		case DFU_EV_STATUS_POLL_TIMEOUT:
			/* DFU_GETSTATUS request is now allowed, after
			   the timeout */
			*event_exists = 1;
			next = DFU_STATE_dfuDNLOAD_SYNC;
			break;

		case DFU_EV_POWER_RESET:
		case DFU_EV_USB_RESET:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_FIRMWARE_VALID)
				next = DFU_STATE_appIDLE;
			else
				next = DFU_STATE_dfuERROR;
			break;

		case DFU_EV_INVALID_DFU_REQUEST:
		default:
			/* control pipe stall */
			*event_exists = 1;
			next = DFU_STATE_dfuERROR;
			break;
		}
		break;

	case DFU_STATE_dfuDNLOAD_IDLE:
		/* A.2.6 */

		switch(event)
		{
		case DFU_EV_DNLOAD:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_WLENGTH_GT_ZERO)
			{
				/* wLength > 0 -> begin dnload block */
				next = DFU_STATE_dfuDNLOAD_SYNC;
			}
			else
			{
				/* host says: no more data to download */
				if(guardflags & DFU_GUARD_DEV_DISAGREES_DNLOAD_END)
				{
					/* host&device not
					   synchronized about how much
					   is to be downloaded:

					   - host should initiate recovery

					   - device stalls control pipe */
					next = DFU_STATE_dfuERROR;
				}
				else
				{
					/* fine. */
					next = DFU_STATE_dfuMANIFEST_SYNC;
				}
			}
			break;

		case DFU_EV_ABORT:
			/* host is terminating dnload transfer; if icnomplete, firmware may be corrupt. */
			*event_exists = 1;
			next = DFU_STATE_dfuIDLE;
			break;

		case DFU_EV_GETSTATUS:
			/* answer */
		case DFU_EV_GETSTATE:
			/* answer */
			*event_exists = 1;
			next = dfu_state;
			break;

		case DFU_EV_POWER_RESET:
		case DFU_EV_USB_RESET:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_FIRMWARE_VALID)
				next = DFU_STATE_appIDLE;
			else
				next = DFU_STATE_dfuERROR;
			break;

		case DFU_EV_INVALID_DFU_REQUEST:
		default:
			/* ctrl pipe stall */
			*event_exists = 1;
			next = DFU_STATE_dfuERROR;
			break;
		}
		break;

	case DFU_STATE_dfuMANIFEST_SYNC:
		/* A.2.7 */

		switch(event)
		{
		case DFU_EV_GETSTATUS:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_MANIFESTATION_IN_PROGRESS)
				next = DFU_STATE_dfuMANIFEST;
			else if(guardflags & DFU_GUARD_BIT_MANIFESTATION_TOLERANT)
			{
				/* manifestation complete */
				next = DFU_STATE_dfuIDLE;
			}
			else
			{
				/* control pipe stall */
				next = DFU_STATE_dfuERROR;
			}

			break;

		case DFU_EV_GETSTATE:
			/* answer */
			*event_exists = 1;
			next = dfu_state;
			break;

		case DFU_EV_POWER_RESET:
		case DFU_EV_USB_RESET:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_FIRMWARE_VALID)
				next = DFU_STATE_appIDLE;
			else
				next = DFU_STATE_dfuERROR;
			break;

		case DFU_EV_ABORT:
			/* this is NOT specified in A.2.7, but in figure A.1 */
			*event_exists = 1;
			next = DFU_STATE_dfuIDLE;
			break;

		case DFU_EV_INVALID_DFU_REQUEST:
		default:
			/* control pipe stall */
			*event_exists = 1;
			next = DFU_STATE_dfuERROR;
			break;
		}
		break;

	case DFU_STATE_dfuMANIFEST:
		/* A.2.8 */

		switch(event)
		{
		case DFU_EV_STATUS_POLL_TIMEOUT:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_BIT_MANIFESTATION_TOLERANT)
			{
				/* bitManfestationTolerant=1:

				 dev can still communicate via USB
				 after manifestation. */
				next = DFU_STATE_dfuMANIFEST_SYNC;
			}
			else
			{
				/* bitManfestationTolerant=0:

				 limited to no USB after manifestation*/
				next = DFU_STATE_dfuMANIFEST_WAIT_RESET;
			}
			break;

		case DFU_EV_POWER_RESET:
		case DFU_EV_USB_RESET:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_FIRMWARE_VALID)
				next = DFU_STATE_appIDLE;
			else
				next = DFU_STATE_dfuERROR;
			break;

		case DFU_EV_INVALID_DFU_REQUEST:
		default:
			/* control pipe stall */
			*event_exists = 1;
			next = DFU_STATE_dfuERROR;
			break;
		}
		break;

	case DFU_STATE_dfuMANIFEST_WAIT_RESET:
		/* A.2.9 */

		switch(event)
		{

		case DFU_EV_POWER_RESET:
		case DFU_EV_USB_RESET:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_FIRMWARE_VALID)
				next = DFU_STATE_appIDLE;
			else
				next = DFU_STATE_dfuERROR;
			break;

		case DFU_EV_INVALID_DFU_REQUEST:
		default:
			/* control pipe stall; dev can't do anything
			   on USB (this limitation is why the device
			   is in this state, after all), it probably
			   won't even get the USB request */
			next = DFU_STATE_dfuMANIFEST_WAIT_RESET;
			break;
		}
		break;

	case DFU_STATE_dfuUPLOAD_IDLE:
		/* A.2.10 */

		switch(event)
		{
		case DFU_EV_UPLOAD:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_WLENGTH_GT_ZERO &&
			   ! (guardflags & DFU_GUARD_UPLOAD_SHORT_FRAME))
			{
				next = DFU_STATE_dfuUPLOAD_IDLE;
			}
			else if(guardflags & DFU_GUARD_UPLOAD_SHORT_FRAME)
			{
				/* finished upload - complete
				   control-read op. */
				next = DFU_STATE_dfuIDLE;
			}
			else
				next = DFU_STATE_dfuERROR;
			break;

		case DFU_EV_ABORT:
			/* terminate upload transfer */
			*event_exists = 1;
			next = DFU_STATE_dfuIDLE;
			break;

		case DFU_EV_GETSTATUS:
			/* answer */
		case DFU_EV_GETSTATE:
			/* answer */
			*event_exists = 1;
			next = dfu_state;
			break;

		case DFU_EV_POWER_RESET:
		case DFU_EV_USB_RESET:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_FIRMWARE_VALID)
				next = DFU_STATE_appIDLE;
			else
				next = DFU_STATE_dfuERROR;
			break;

		case DFU_EV_INVALID_DFU_REQUEST:
		default:
			/* control pipe stall */
			*event_exists = 1;
			next = DFU_STATE_dfuERROR;
			break;
		}
		break;

	case DFU_STATE_dfuERROR:
		/* A.2.11 */

		switch(event)
		{
		case DFU_EV_GETSTATUS:
		case DFU_EV_GETSTATE:
			/* remain in dfuERROR */
			*event_exists = 1;
			next = dfu_state;
			break;

		case DFU_EV_CLRSTATUS:
			/* clear to status OK */
			*event_exists = 1;
			next = DFU_STATE_dfuIDLE;
			break;

		case DFU_EV_POWER_RESET:
		case DFU_EV_USB_RESET:
			*event_exists = 1;
			if(guardflags & DFU_GUARD_FIRMWARE_VALID)
				next = DFU_STATE_appIDLE;
			else
				next = DFU_STATE_dfuERROR;
			break;

		case DFU_EV_INVALID_DFU_REQUEST:
		default:
			/* control pipe stall */
			*event_exists = 1;
			next = DFU_STATE_dfuERROR;
			break;
		}
		break;

	default:
		break;
	}

	if(!silent && next == -1)
	{
		if(*event_exists)
		{
			fprintf( stderr, "ERROR: The event %s exists but it's invalid because guards don't match (state = %s, guards = %s).\n",
				 dfu_sm_event_to_string(event),
				 dfu_state_to_string(dfu_state),
				 dfu_sm_guards_to_string(guardflags) );
		}
		else
		{
			fprintf( stderr, "ERROR: The event %s from current state does not exist (state = %s, guards = %s).\n",
				 dfu_sm_event_to_string(event),
				 dfu_state_to_string(dfu_state),
				 dfu_sm_guards_to_string(guardflags) );
		}
		return -1;
	}

	return next;
}

int dfu_sm_get_next_state(dfu_handle *handle, enum DFU_SM_EVENT event, unsigned int guardflags)
{
	return _dfu_sm_get_next_state(event, guardflags, NULL, 0);
}

/**
 * check if the current state does contain any event of ID @p
 * event. this does not necessary tell if the event is
 * actually valid, because events may be ambigous, and need to be
 * unique by specifiying (event + guard-flags).
 *
 * @return 1 if the event does exist, or 0
 */
int dfu_sm_state_has_event(dfu_handle *handle, enum DFU_SM_EVENT event)
{
	int res;
	_dfu_sm_get_next_state(event, 0, &res, 1);

	if(!res)
	{
		fprintf( stderr, "ERROR: The event %s from current state does not exist (state = %s).\n",
			 dfu_sm_event_to_string(event),
			 dfu_state_to_string(dfu_state) );
	}

	return res!=0;
}

/* list of valid DFU 1.0 transitions */
static uint _sm_transitions[] = {
	[DFU_STATE_appIDLE]
	= 1<<DFU_STATE_appDETACH | 1<<DFU_STATE_appIDLE,

	[DFU_STATE_appDETACH]
	= 1<<DFU_STATE_appIDLE | 1<<DFU_STATE_dfuIDLE | 1<<DFU_STATE_appDETACH,

	[DFU_STATE_dfuIDLE]
	= 1<<DFU_STATE_dfuIDLE | 1<<DFU_STATE_dfuDNLOAD_SYNC |
	  1<<DFU_STATE_dfuUPLOAD_IDLE | 1<<DFU_STATE_dfuERROR,

	[DFU_STATE_dfuDNLOAD_SYNC]
	= 1<<DFU_STATE_dfuDNLOAD_SYNC | 1<<DFU_STATE_dfuIDLE |
	  1<<DFU_STATE_dfuDNLOAD_IDLE | 1<<DFU_STATE_dfuDNBUSY |1<<DFU_STATE_dfuERROR,

	[DFU_STATE_dfuDNBUSY]
	= 1<<DFU_STATE_dfuDNLOAD_SYNC | 1<<DFU_STATE_dfuERROR,

	[DFU_STATE_dfuDNLOAD_IDLE]
	= 1<<DFU_STATE_dfuDNLOAD_IDLE | 1<<DFU_STATE_dfuIDLE |
	  1<<DFU_STATE_dfuDNLOAD_SYNC | 1<<DFU_STATE_dfuMANIFEST_SYNC |	1<<DFU_STATE_dfuERROR,

	[DFU_STATE_dfuMANIFEST_SYNC]
	= 1<<DFU_STATE_dfuMANIFEST_SYNC | 1<<DFU_STATE_dfuIDLE |
	  1<<DFU_STATE_dfuMANIFEST | 1<<DFU_STATE_dfuERROR,

	[DFU_STATE_dfuMANIFEST]
	= 1<<DFU_STATE_dfuMANIFEST_SYNC | 1<<DFU_STATE_dfuMANIFEST_WAIT_RESET |
	  1<<DFU_STATE_dfuERROR,

	[DFU_STATE_dfuMANIFEST_WAIT_RESET]
	= 1<<DFU_STATE_dfuERROR,

	[DFU_STATE_dfuUPLOAD_IDLE]
	= 1<<DFU_STATE_dfuUPLOAD_IDLE | 1<<DFU_STATE_dfuIDLE | 1<<DFU_STATE_dfuERROR,

	[DFU_STATE_dfuERROR]
	= 1<<DFU_STATE_dfuIDLE | 1<<DFU_STATE_dfuERROR,
};

/**
 * do state transition
 *
 * @p state - new state
 * @return 0 on success, -1 on error
 */
int dfu_sm_set_state_checked(dfu_handle *handle, enum dfu_state state)
{
	int valid = 0;

	/* is the new state available & a valid transition? */
	if(state >= 0 && state < dfu_state_count &&
	   _sm_transitions[dfu_state] & (1<<state))
	{
		valid = 1;
	}

	if(!valid)
	{
		printf("Fatal error: illegal state transition detected (%s (=%d) -> %s (=%d))\n",
		       dfu_state_to_string(dfu_state),
		       dfu_state,
		       dfu_state_to_string(state),
		       state);
		return -1;
	}

	/* printf("[%s -> %s]\n", */
	/*        dfu_state_to_string(dfu_state), */
	/*        dfu_state_to_string(state)); */
	/* fflush(stdout); */

	/* error msg output */
	if(dfu_state != state &&
	   state == DFU_STATE_dfuERROR)
	{
		printf("Device entered error state!");
	}

	dfu_state = state;

	return 0;
}

/**
 * get current state
 *
 * @res state - the current state
 */
int dfu_sm_get_state(dfu_handle *handle)
{
	return dfu_state;
}

/**
 * set current state without doing any state transition
 *
 * @res state - the current state
 */
void dfu_sm_set_state_unchecked(dfu_handle *handle, enum dfu_state state)
{
	/* fprintf( stderr, */
	/* 	 "[state reset: -> %s]\n", */
	/* 	 dfu_state_to_string(state)); */

	dfu_state = state;
}
