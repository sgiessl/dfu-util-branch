/*
 * dfu-util - device quirk utilities & documented list of quirks
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

#ifndef _DFU_QUIRKS_H
#define _DFU_QUIRKS_H

#include <stdint.h>

/**
 * the list of documented divergence from the currently selected DFU
 * version. see dfu_quirk_descriptor array.
 */
enum DFU_QUIRK {
	QUIRK_OPENMOKO_DNLOAD_STATUS_POLL_TIMEOUT = 1,
	QUIRK_OPENMOKO_MANIFEST_STATUS_POLL_TIMEOUT,
	QUIRK_OPENMOKO_DETACH_BEFORE_FINAL_RESET,
	QUIRK_IGNORE_INVALID_FUNCTIONAL_DESCRIPTOR,
	QUIRK_FORCE_DFU_VERSION_1_0,
	QUIRK_FORCE_DFU_VERSION_1_1,
	DFU_QUIRK_COUNT
};

typedef struct _dfu_quirk_apply_entry {
	unsigned int id;		/*< quirk ID */
	const char *name;	/* name of the device(s) this quirk applies to */
	uint16_t apply_bcdDevice;   /* Device Revision, or 0xffff for any revision */
	uint16_t apply_idProduct;   /* ProductID, or 0xffff for any product */
	uint16_t apply_idVendor;    /* VendorID, or 0xffff for any vendor */
	uint16_t apply_bcdDFU;	    /* Version, or 0xffff for any DFU version */
} dfu_quirk_apply_entry;

/* internal storage of a set of quirks */
typedef struct _dfu_quirks {
	unsigned int q1;
} dfu_quirks;

void dfu_quirks_print();

void dfu_quirks_print_set(dfu_quirks *quirks);

void dfu_quirks_clear(dfu_quirks *quirks);
void dfu_quirks_insert(dfu_quirks *quirks_dest,
		       dfu_quirks *quirks_src);
void dfu_quirk_set(dfu_quirks *quirks,
		   enum DFU_QUIRK quirk);
void dfu_quirk_clear(dfu_quirks *quirks,
		     enum DFU_QUIRK quirk);
int dfu_quirk_is_set(dfu_quirks *quirks,
		     enum DFU_QUIRK quirk);
int dfu_quirks_is_empty(dfu_quirks *quirks);

dfu_quirks dfu_quirks_detect(uint16_t bcdDFU, uint16_t idVendor, uint16_t idProduct, uint16_t bcdDevice);

#endif
