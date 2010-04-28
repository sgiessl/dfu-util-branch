#ifndef _SAM7DFU_H
#define _SAM7DFU_H

int sam7dfu_do_upload(dfu_handle *handle, 
		      int xfer_size, const char *fname);
int sam7dfu_do_dnload(dfu_handle *handle,
		      int xfer_size, const char *fname);

int sam7dfu_do_suffix(const char *fname);

#endif
