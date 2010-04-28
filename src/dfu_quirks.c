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


#include <stdio.h>
#include "dfu_quirks.h"

typedef struct _dfu_quirk_descriptor {
	unsigned int id;
	const char *name;
	const char *description;
} dfu_quirk_descriptor;

#define QUIRK_DESC_ENTRY($enum_item, $description)	\
{							\
	.id = $enum_item,				\
		.name = #$enum_item,			\
		.description = $description		\
		}

dfu_quirk_descriptor _quirks[] = {
	{
	},
	QUIRK_DESC_ENTRY(QUIRK_OPENMOKO_DNLOAD_STATUS_POLL_TIMEOUT,
			 "openmoko: u-boot not being able to provide bwPollTimeout excepts it to be 5  msec during DOWNLOAD"),
	QUIRK_DESC_ENTRY(QUIRK_OPENMOKO_MANIFEST_STATUS_POLL_TIMEOUT,
			 "openmoko: some devices (e.g. TAS1020b) need some time before we can obtain the status during MANIFEST (overwrite bwPollTimeout with 1 sec)"),
	QUIRK_DESC_ENTRY(QUIRK_OPENMOKO_DETACH_BEFORE_FINAL_RESET,
			 "openmoko: before issuing the final reset, a non-standard DFU_DETACH is needed"),
	QUIRK_DESC_ENTRY(QUIRK_IGNORE_INVALID_FUNCTIONAL_DESCRIPTOR,
			 "if DFU functional descriptor can't be ignored, continue with permissive DFU flags and manual settings such as --transfer-size"),
	QUIRK_DESC_ENTRY(QUIRK_FORCE_DFU_VERSION_1_0,
			 "ignore device's DFU version, and assume DFU 1.0"),
	QUIRK_DESC_ENTRY(QUIRK_FORCE_DFU_VERSION_1_1,
			 "ignore device's DFU version, and assume DFU 1.1"),
};

void dfu_quirk_set(dfu_quirks *quirks,
		   enum DFU_QUIRK quirk)
{
	if(!quirks)
		return;
	(quirks->q1) |= (1<<quirk);
}
void dfu_quirk_clear(dfu_quirks *quirks,
		     enum DFU_QUIRK quirk)
{
	if(!quirks)
		return;
	(quirks->q1) &= ~ (1<<quirk);
}
int dfu_quirk_is_set(dfu_quirks *quirks,
		     enum DFU_QUIRK quirk)
{
	if(!quirks)
		return 0;
	return 	(quirks->q1) & (1<<quirk);
}

void dfu_quirks_clear(dfu_quirks *quirks)
{
	if(!quirks)
		return;
	quirks->q1 = 0;
}

dfu_quirks dfu_quirks_detect(uint16_t bcdDFU, uint16_t idVendor, uint16_t idProduct, uint16_t bcdDevice)
{
	dfu_quirks q;
	dfu_quirks_clear(&q);

	/* http://wiki.openmoko.org/wiki/USB_Product_IDs, 2010-04-09 */
	switch(idVendor)
	{
	case 0x1d50:
		switch(idProduct)
		{
		case 0x1db5: 	/* IDBG in DFU mode */
		case 0x1db6:    /* IDBG in normal mode */
			break;
		}
		break;

	case 0x1457:		/* FIC, Inc */
	case 0x5117:		/* Openmoko, Inc */
		switch(idProduct)
		{
		case 0x5117:	/* Neo1973/FreeRunner kernel usbnet (g_ether, CDC Ethernet) Mode */

		case 0x5118: 	/* Debug Board (FT2232D) for Neo1973/FreeRunner  */
		case 0x5119: 	/* Neo1973/FreeRunner u-boot usbtty CDC ACM Mode  */
		case 0x511a: 	/* HXD8 u-boot usbtty CDC ACM Mode  */
		case 0x511b: 	/* SMDK2440 u-boot usbtty CDC ACM mode  */
		case 0x511c: 	/* SMDK2443 u-boot usbtty CDC ACM mode  */
		case 0x511d: 	/* QT2410 u-boot usbtty CDC ACM mode  */
		case 0x511e: 	/* Reserved  */
		case 0x511f: 	/* Reserved  */
		case 0x5120: 	/* Neo1973/FreeRunner u-boot generic serial Mode  */
		case 0x5121: 	/* Neo1973/FreeRunner kernel mass storage (g_storage) Mode  */
		case 0x5122: 	/* Neo1973/FreeRunner kernel usbnet (g_ether, RNDIS) Mode  */
		case 0x5123: 	/* Neo1973/FreeRunner internal USB Bluetooth CSR4 module  */
		case 0x5124: 	/* Neo1973/FreeRunner Bluetooth Device ID service  */
		case 0x5125: 	/* TBD  */
		case 0x5126: 	/* TBD */
			/* todo limit bcdDevice version. */
			dfu_quirk_set(&q, QUIRK_OPENMOKO_DNLOAD_STATUS_POLL_TIMEOUT);
			break;
		}
		break;
	}

	return q;
}

void dfu_quirks_print()
{
	int i = 1;
	for(i = 1; i < DFU_QUIRK_COUNT; ++i)
	{
		printf("%.2d: %s\n    %s\n", i, _quirks[i].name, _quirks[i].description);
	}
}

void dfu_quirks_insert(dfu_quirks *quirks_dest,
		       dfu_quirks *quirks_src)
{
	int i = 1;
	for(i = 1; i < DFU_QUIRK_COUNT; ++i)
	{
		if(dfu_quirk_is_set(quirks_src, i))
			dfu_quirk_set(quirks_dest, i);
	}
}

void dfu_quirks_print_set(dfu_quirks *quirks)
{
	if(!quirks)
		return;

	int first = 1;
	int i = 1;
	for(i = 1; i < DFU_QUIRK_COUNT; ++i)
	{
		if(dfu_quirk_is_set(quirks, i))
		{
			if(!first)
				printf("|");
			printf("%s", _quirks[i].name);
			first = 0;
		}
	}
}

int dfu_quirks_is_empty(dfu_quirks *quirks)
{
	if(!quirks)
		return 1;
	return quirks->q1 == 0;
}


