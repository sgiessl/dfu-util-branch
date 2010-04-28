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

/* DFU 1.0 command events, and Pseudo-events needed to
   describe the full DFU 1.0 state machine */
enum DFU_SM_EVENT {
	DFU_EV_DETACH =      0,
	DFU_EV_DNLOAD =      1,
	DFU_EV_UPLOAD =      2,
	DFU_EV_GETSTATUS =   3,
	DFU_EV_CLRSTATUS =   4,
	DFU_EV_GETSTATE =    5,
	DFU_EV_ABORT =       6,
	DFU_EV_USB_RESET =             11,
	DFU_EV_POWER_RESET =           12,
	DFU_EV_STATUS_POLL_TIMEOUT =   13,
	DFU_EV_DETACH_TIMEOUT =        14,
	DFU_EV_INVALID_DFU_REQUEST =   15
};

/**
 * Some statemachine guards needed to describe the full DFU 1.0 state machine
 */
enum dfu_event_guard_flags {
	DFU_GUARD_WLENGTH_GT_ZERO             = 1,
	DFU_GUARD_UPLOAD_SHORT_FRAME          = (1<<1),
	DFU_GUARD_BLOCK_IN_PROGRESS           = (1<<2),
	DFU_GUARD_MANIFESTATION_IN_PROGRESS   = (1<<3),
	DFU_GUARD_BIT_CAN_DNLOAD              = (1<<4),
	DFU_GUARD_BIT_MANIFESTATION_TOLERANT  = (1<<5),
	DFU_GUARD_BIT_CAN_UPLOAD              = (1<<6),
	DFU_GUARD_DEV_DISAGREES_DNLOAD_END    = (1<<7),
	DFU_GUARD_DETACH_TIMER_ELAPSED        = (1<<8),
	DFU_GUARD_FIRMWARE_VALID              = (1<<9)
};
#define dfu_event_guard_flags_count 10

const char *dfu_sm_event_to_string(enum DFU_SM_EVENT event);
const char *dfu_sm_guards_to_string(int guard_flags);
int dfu_sm_state_has_event(dfu_handle *handle, enum DFU_SM_EVENT event);

int dfu_sm_get_next_state(dfu_handle *handle, enum DFU_SM_EVENT event, unsigned int guardflags);

int dfu_sm_get_state(dfu_handle *handle);
int dfu_sm_set_state_checked(dfu_handle *handle, enum dfu_state state);
void dfu_sm_set_state_unchecked(dfu_handle *handle, enum dfu_state state);
