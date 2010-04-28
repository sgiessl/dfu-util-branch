/* This is supposed to be a "real" DFU implementation, just as specified in the
 * USB DFU 1.0 Spec.  Not overloaded like the Atmel one...
 *
 * (C) 2007-2008 by Harald Welte <laforge@gnumonks.org>
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

#include "config.h"
#include "dfu.h"
#include "dfu_sm.h"
#include "crc32.h"
#include "usb_dfu.h"
#include "dfu_quirks.h"
#include "sam7dfu.h"

/* ugly hack for Win32 */
#ifndef O_BINARY
#define O_BINARY 0
#endif

int sam7dfu_do_upload(dfu_handle *handle, 
		      int xfer_size, const char *fname)
{
	int i = 0;
	int ret, fd, total_bytes = 0;
	char *buf = malloc(xfer_size);
	struct dfu_file_suffix suffix;
	struct dfu_status dst;
	uint32_t crc = 0;

	if (!buf)
		return -ENOMEM;

	fd = creat(fname, 0644);
	if (fd < 0) {
		perror(fname);
		ret = fd;
		goto out_free;
	}
	
	printf("bytes_per_hash=%u\n", xfer_size);
	printf("Starting upload: [");
	fflush(stdout);

	crc = crc32_init();

	while (1) {
		int rc, write_rc;

		ret = dfu_get_status(handle, &dst);
		if (ret < 0) {
			fprintf(stderr, "Error during upload get_status\n");
			goto out_close;
		}

		if (dst.bStatus != DFU_STATUS_OK) {
			printf("\rFirmware upload ... aborting (status %d state %d)\n",
			       dst.bStatus, dst.bState);
			goto out_close;
		}

		rc = dfu_upload(handle, xfer_size, buf);
		if (rc < 0) {
			ret = rc;
			goto out_close;
		}
		write_rc = write(fd, buf, rc);
		if (write_rc < rc) {
			fprintf(stderr, "Short file write: %s\n",
				strerror(errno));
			ret = total_bytes;
			goto out_close;
		}
		total_bytes += rc;

		for (i = 0; i < rc; i++)
			crc = crc32_byte(crc, buf[i]);

		if (rc < xfer_size) {
			/* last block, return */
			break;
		}
		putchar('#');
		fflush(stdout);
	}
	ret = 0;

	printf("] finished! read %d bytes.\n", total_bytes);
	fflush(stdout);

	/* suffix */
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
		printf("Appended suffix block to image (firmware checksum: %08x)\n", crc);


 out_close:
	close(fd);
 out_free:
	free(buf);
	
	return ret;
}

#define PROGRESS_BAR_WIDTH 50

#define MIN(a, b) (((a)<(b))?(a):(b))
#define MAX(a, b) (((a)>(b))?(a):(b))

/**
 * Assuming the buffer @p buf contains the dfu file image including a
 * DFU suffix, do a CRC checksum and suffix check of the image.
 *
 * @return 0 if the file is valid, or -1 if the image is invalid.
 */
static int dfu_file_suffix_check(const char *fname, struct dfu_file_suffix *suffix, uint32_t *calculated_crc)
{
	struct dfu_file_suffix suffix_dummy, suffix_tmp;
	uint32_t dummy_calc_crc;
	int i = 0;
	int bytes_read = 0;
	int ret;
	int fd = -1;
	struct stat st;
	char buf[3];

	if(!calculated_crc)
		calculated_crc = &dummy_calc_crc;
	if(!suffix)
		suffix = &suffix_dummy;


	fd = open(fname, O_RDONLY|O_BINARY);
	if (fd < 0) {
		perror(fname);
		ret = fd;
		goto out_error;
	}

	ret = fstat(fd, &st);
	if (ret < 0) {
		perror(fname);
		goto out_error;
	}

	if (st.st_size <= 0 /* + DFU_HDR */) {
		fprintf(stderr, "File seems a bit too small...\n");
		ret = -EINVAL;
		goto out_error;
	}

	if(st.st_size <= DFU_FILE_SUFFIX_SIZE)
	{
		fprintf(stderr, "firmware image too small. it needs to be at least dfu suffix size\n");
		goto out_error;
	}

	*calculated_crc = crc32_init();

	while((st.st_size-bytes_read) > 0 &&
	      (ret = read(fd, buf, MIN(sizeof(buf), (st.st_size-bytes_read)))) != 0)
	{
		if(ret < 0)
		{
			perror("Can't read firmware file %s");
			goto out_error;
		}

		for (i = 0; i < MAX(0,
				    MIN(ret, (st.st_size-4-bytes_read))); i++)
			*calculated_crc = crc32_byte(*calculated_crc, buf[i]);

		/* copy over suffix block */
		int dfu_suffix_offset = (st.st_size-DFU_FILE_SUFFIX_SIZE);
		if(bytes_read >= dfu_suffix_offset) {
			memcpy( ((char*)&suffix_tmp) + (bytes_read - dfu_suffix_offset),
			       buf, ret);
		}
		else if (bytes_read+ret > (st.st_size-DFU_FILE_SUFFIX_SIZE))
		{
			const int len = (bytes_read+ret - (st.st_size-DFU_FILE_SUFFIX_SIZE));
			memcpy(&suffix_tmp,
			       buf+ret-len, len);
		}

		bytes_read += ret;
	}

	*suffix = suffix_tmp;
	/* take care of endianness */
	suffix->dwCRC = le32_to_cpu(suffix_tmp.dwCRC);

	return *calculated_crc == suffix->dwCRC;

 out_error:
	if(fd >= 0)
	{
		close(fd);
		fd = -1;
	}
	return 0;
}

int sam7dfu_do_dnload(dfu_handle *handle,
		      int xfer_size, const char *fname)
{
	int ret = -1, fd = -1, bytes_sent = 0;
	unsigned int bytes_per_hash, hashes = 0;
	char *buf = malloc(xfer_size);
	struct stat st;
	struct dfu_status dst;

	if (!buf)
		return -ENOMEM;

        /* validate DFU suffix */
        int validate_image = 1;
        if(validate_image)
        {
		uint32_t calculated_crc = 0;
		struct dfu_file_suffix suffix = {};

		int valid = dfu_file_suffix_check(fname, &suffix, &calculated_crc);

		printf("Firmware Checksum\t%08x ", calculated_crc);
		if(valid)
			printf("(%s)\n", "valid");
		else
			printf("(%s, expected %08x)\n", "corrupt", suffix.dwCRC);

		/* TODO: warn if idVendor, idDevice etc. don't match -> s. section B */

		if (!valid) {
			ret = -1;
			goto out_error;
		}
        }

	/* open */
	fd = open(fname, O_RDONLY|O_BINARY);
	if (fd < 0) {
		perror(fname);
		ret = fd;
		goto out_error;
	}

	ret = fstat(fd, &st);
	if (ret < 0) {
		perror(fname);
		goto out_error;
	}

	if (st.st_size <= 0 /* + DFU_HDR */) {
		fprintf(stderr, "File seems a bit too small...\n");
		ret = -EINVAL;
		goto out_error;
	}

	if(st.st_size <= DFU_FILE_SUFFIX_SIZE)
	{
		fprintf(stderr, "firmware image too small. it needs to be at least dfu suffix size\n");
		goto out_error;
	}

        /* upload, with progress bar */
	bytes_per_hash = st.st_size / PROGRESS_BAR_WIDTH;
	if (bytes_per_hash == 0)
		bytes_per_hash = 1;
	printf("bytes_per_hash=%u\n", bytes_per_hash);
#if 0
	read(fd, DFU_HDR);
#endif
	printf("Starting download: [");
	fflush(stdout);
	while (bytes_sent < st.st_size - DFU_FILE_SUFFIX_SIZE) {
		int hashes_todo;

		ret = read(fd, buf, MIN(xfer_size, st.st_size - DFU_FILE_SUFFIX_SIZE - bytes_sent) );
		if (ret < 0) {
			perror(fname);
			goto out_error;
		}

		if (ret == 0)
		{
			perror("premature end of file");
			goto out_error;
		}

		ret = dfu_download(handle, ret, ret ? buf : NULL);
		if (ret < 0) {
			fprintf(stderr, "Error during download\n");
			goto out_error;
		}
		bytes_sent += ret;

		do {
			ret = dfu_get_status(handle, &dst);
			if (ret < 0) {
				fprintf(stderr, "Error during download get_status\n");
				goto out_error;
			}

			if(dfu_sm_get_state(handle) == DFU_STATE_dfuDNBUSY)
			{
				int timeout = dst.bwPollTimeout;
				if(dfu_quirk_is_set(&handle->quirk_flags, QUIRK_OPENMOKO_DNLOAD_STATUS_POLL_TIMEOUT))
				{
					timeout = 5;
				}
				if(dfu_status_poll_timeout(handle,
							   timeout) < 0)
					goto out_error;
			}

		} while (dst.bState != DFU_STATE_dfuDNLOAD_IDLE);
		if (dst.bStatus != DFU_STATUS_OK) {
			printf(" failed!\n");
			printf("state(%u) = %s, status(%u) = %s\n", dst.bState,
			       dfu_state_to_string(dst.bState), dst.bStatus,
			       dfu_status_to_string(dst.bStatus));
			ret = -1;
			goto out_error;
		}

		hashes_todo = (bytes_sent / bytes_per_hash) - hashes;
		hashes += hashes_todo;
		while (hashes_todo--)
			putchar('#');
		fflush(stdout);
	}

	/* send one zero sized download request to signalize end */
	ret = dfu_download(handle, 0, NULL);
	if (ret >= 0)
		ret = bytes_sent;
	
	printf("] finished!\n");
	fflush(stdout);

get_status:
	/* We are now in MANIFEST_SYNC state */

	ret = dfu_get_status(handle, &dst);
	if (ret < 0) {
		fprintf(stderr, "unable to read DFU status\n");
		goto out_error;
	}
	printf("state(%u) = %s, status(%u) = %s\n", dst.bState, 
	       dfu_state_to_string(dst.bState), dst.bStatus,
	       dfu_status_to_string(dst.bStatus));

	if(dfu_sm_get_state(handle) == DFU_STATE_dfuMANIFEST) {
		unsigned int timeout = dst.bwPollTimeout;

		if(dfu_quirk_is_set(&handle->quirk_flags, QUIRK_OPENMOKO_MANIFEST_STATUS_POLL_TIMEOUT))
		{
			printf("Overwriting dfuMANIFEST_SYNC status poll timeout to 1 second (QUIRK_OPENMOKO_MANIFEST_STATUS_POLL_TIMEOUT)\n");

			/* 1 second */
			timeout = 1*1000*1000;
		}

		/* dfu_status_poll_timeout() does an internal
		   statemachine transition based on bitManifestationTolerant */
		if(dfu_status_poll_timeout(handle,
					   timeout) < 0)
			goto out_error;

		if(dfu_sm_get_state(handle) == DFU_STATE_dfuMANIFEST_SYNC)
		{
			/* repeat dfu_get_status() */
			goto get_status;
		}
	}

	switch(dfu_sm_get_state(handle)) {
	case DFU_STATE_dfuIDLE:
		/* the device isn't able to do any USB communication
		   anymore; the host must reset it now. */
		if(handle->func_dfu.bmAttributes & USB_DFU_MANIFEST_TOL)
			printf("Manifestation complete, device state is dfuIDLE now (bitManifestationTolerant=1)\n");
		else
			printf("WARNING: expected state dfuMANIFEST_WAIT_RESET but new state is dfuIDLE (Manifestation complete, bitManifestationTolerant=0)\n");

		break;

	case DFU_STATE_dfuMANIFEST_WAIT_RESET:
		/* the device isn't able to do any USB communication
		   anymore; the host must reset it now. */
		if(handle->func_dfu.bmAttributes & USB_DFU_MANIFEST_TOL)
			printf("WARNING: expected state dfuIDLE but new state is dfuMANIFEST_WAIT_RESET (Manifestation complete, bitManifestationTolerant=1). Still attempting to do USB device reset.\n");
		else
			printf("Resetting USB device (bitManifestationTolerant=0)\n");

		if(dfu_usb_reset(handle) < 0)
			goto out_error;
		break;

	default:
		/* unexpected! */
		printf("Unexpected device state %s while doing manifestation.\n",
		       dfu_state_to_string(dfu_sm_get_state(handle)) );
		break;
	}

	printf("Done!\n");

 out_error:
	if(fd >= 0)
	{
		close(fd);
		fd = -1;
	}
	free(buf);
	buf = NULL;

	return ret;
}

