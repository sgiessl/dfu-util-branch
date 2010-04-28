/*
 * dfu-util - DFU CRC / file suffix
 *
 * Copyright (C) 2010      by Sandro Giessl <s.giessl@niftylight.de>
 *
 * Based on code from dfutool (BlueZ):
 * Copyright (C) 2003-2008  Marcel Holtmann <marcel@holtmann.org>
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

/**
 * add dfu firmware suffix to file @p fname
 */
int add_file_suffix(const char *fname)
{
	struct dfu_file_suffix suffix;
	char buf[2048];
	uint32_t crc;
	int fd, i, len;
	int ret = 0;

	fd = open(fname, O_RDWR | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (fd < 0) {
		fprintf(stderr, "Can't open firmware file: %s (%d)\n", strerror(errno), errno);
		ret = -1;
		goto done;
	}
	crc = crc32_init();
	while (1) {
		len = read(fd, buf, 2048);
		if (len == 0) break;
		for (i = 0; i < len; i++)
			crc = crc32_byte(crc, buf[i]);
	}

	suffix.bcdDFU = cpu_to_le16(0x0100);
	suffix.ucDfuSignature[0] = 'U';
	suffix.ucDfuSignature[1] = 'F';
	suffix.ucDfuSignature[2] = 'D';
	suffix.bLength = DFU_FILE_SUFFIX_SIZE;

	memcpy(buf, &suffix, DFU_FILE_SUFFIX_SIZE);
	for (i = 0; i < DFU_FILE_SUFFIX_SIZE - 4; i++)
		crc = crc32_byte(crc, buf[i]);

	suffix.dwCRC = cpu_to_le32(crc);

	if (write(fd, &suffix, DFU_FILE_SUFFIX_SIZE) < 0)
		printf("Can't write suffix block: %s (%d)\n", strerror(errno), errno);
	else
		printf("Successfully wrote suffix block (checksum: %08x)\n", crc);

 done:
	close(fd);
	return ret;
}

