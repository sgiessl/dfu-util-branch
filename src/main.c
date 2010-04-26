/*
 * dfu-util
 *
 * (C) 2007-2008 by OpenMoko, Inc.
 * Written by Harald Welte <laforge@openmoko.org>
 *
 * Based on existing code of dfu-programmer-0.4
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

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <usb.h>
#include <errno.h>

#include "dfu.h"
#include "usb_dfu.h"
#include "sam7dfu.h"
#include "dfu-version.h"
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_USBPATH_H
#include <usbpath.h>
#endif

/* define a portable function for reading a 16bit little-endian word */
unsigned short get_int16_le(const void *p)
{
    const unsigned char *cp = p;

    return ( cp[0] ) | ( ((unsigned short)cp[1]) << 8 );
}

int debug;
static int verbose = 0;

#define DFU_IFF_DFU		0x0001	/* DFU Mode, (not Runtime) */
#define DFU_IFF_VENDOR		0x0100
#define DFU_IFF_PRODUCT		0x0200
#define DFU_IFF_CONFIG		0x0400
#define DFU_IFF_IFACE		0x0800
#define DFU_IFF_ALT		0x1000
#define DFU_IFF_DEVNUM		0x2000
#define DFU_IFF_PATH		0x4000

struct usb_vendprod {
	u_int16_t vendor;
	u_int16_t product;
};

struct dfu_if {
	u_int16_t vendor;
	u_int16_t product;
	u_int8_t configuration;
	u_int8_t interface;
	u_int8_t altsetting;
	int bus;
	u_int8_t devnum;
	const char *path;
	unsigned int flags;
	struct usb_device *dev;

	struct usb_dev_handle *dev_handle;
};

static int _get_first_cb(struct dfu_if *dif, void *v)
{
	struct dfu_if *v_dif = v;

	memcpy(v_dif, dif, sizeof(*v_dif)-sizeof(struct usb_dev_handle *));

	/* return a value that makes find_dfu_if return immediately */
	return 1;
}

/* Find a DFU interface (and altsetting) in a given device */
static int find_dfu_if(struct usb_device *dev, int (*handler)(struct dfu_if *, void *), void *v)
{
	struct usb_config_descriptor *cfg;
	struct usb_interface_descriptor *intf;
	struct usb_interface *uif;
	struct dfu_if _dif, *dfu_if = &_dif;
	int cfg_idx, intf_idx, alt_idx;
	int rc;

	memset(dfu_if, 0, sizeof(*dfu_if));
	
	for (cfg_idx = 0; cfg_idx < dev->descriptor.bNumConfigurations;
	     cfg_idx++) {
		cfg = &dev->config[cfg_idx];
		/* in some cases, noticably FreeBSD if uid != 0,
		 * the configuration descriptors are empty */
		if (!cfg)
			return 0;
		for (intf_idx = 0; intf_idx < cfg->bNumInterfaces;
		     intf_idx++) {
			uif = &cfg->interface[intf_idx];
			if (!uif)
				return 0;
			for (alt_idx = 0;
			     alt_idx < uif->num_altsetting; alt_idx++) {
				intf = &uif->altsetting[alt_idx];
				if (!intf)
					return 0;
				if (intf->bInterfaceClass == 0xfe &&
				    intf->bInterfaceSubClass == 1) {
					dfu_if->dev = dev;
					dfu_if->vendor =
						dev->descriptor.idVendor;
					dfu_if->product =
						dev->descriptor.idProduct;
					dfu_if->configuration = cfg_idx;
					dfu_if->interface = 
						intf->bInterfaceNumber;
					dfu_if->altsetting = 
						intf->bAlternateSetting;
					if (intf->bInterfaceProtocol == 2)
						dfu_if->flags |= 
							DFU_IFF_DFU;
					else
						dfu_if->flags &=
							~DFU_IFF_DFU;
					if (!handler)
						return 1;
					rc = handler(dfu_if, v);
					if (rc != 0)
						return rc;
				}
			}
		}
	}

	return 0;
}

static int get_first_dfu_if(struct dfu_if *dif)
{
	return find_dfu_if(dif->dev, &_get_first_cb, (void *) dif);
}

#define MAX_STR_LEN 64

static int print_dfu_if(struct dfu_if *dfu_if, void *v)
{
	struct usb_device *dev = dfu_if->dev;
	int if_name_str_idx;
	char name[MAX_STR_LEN+1] = "UNDEFINED";

	if_name_str_idx = dev->config[dfu_if->configuration]
				.interface[dfu_if->interface]
				.altsetting[dfu_if->altsetting].iInterface;
	if (if_name_str_idx) {
		if (!dfu_if->dev_handle)
			dfu_if->dev_handle = usb_open(dfu_if->dev);
		if (dfu_if->dev_handle)
			usb_get_string_simple(dfu_if->dev_handle,
					      if_name_str_idx, name,
					      MAX_STR_LEN);
	}

	printf("Found %s: [0x%04x:0x%04x] devnum=%u, cfg=%u, intf=%u, "
	       "alt=%u, name=\"%s\"\n", 
	       dfu_if->flags & DFU_IFF_DFU ? "DFU" : "Runtime",
	       dev->descriptor.idVendor, dev->descriptor.idProduct,
	       dev->devnum, dfu_if->configuration, dfu_if->interface,
	       dfu_if->altsetting, name);

	return 0;
}

static int alt_by_name(struct dfu_if *dfu_if, void *v)
{
	struct usb_device *dev = dfu_if->dev;
	int if_name_str_idx;
	char name[MAX_STR_LEN+1] = "UNDEFINED";

	if_name_str_idx =
	    dev->config[dfu_if->configuration].interface[dfu_if->interface].
	    altsetting[dfu_if->altsetting].iInterface;
	if (!if_name_str_idx)
		return 0;
	if (!dfu_if->dev_handle)
		dfu_if->dev_handle = usb_open(dfu_if->dev);
	if (!dfu_if->dev_handle)
		return 0;
	if (usb_get_string_simple(dfu_if->dev_handle, if_name_str_idx, name,
	     MAX_STR_LEN) < 0)
		return 0; /* should we return an error here ? */
	if (strcmp(name, v))
		return 0;
	/*
	 * Return altsetting+1 so that we can use return value 0 to indicate
	 * "not found".
	 */
	return dfu_if->altsetting+1;
}

static int _count_cb(struct dfu_if *dif, void *v)
{
	int *count = v;

	(*count)++;

	return 0;
}

/* Count DFU interfaces within a single device */
static int count_dfu_interfaces(struct usb_device *dev)
{
	int num_found = 0;

	find_dfu_if(dev, &_count_cb, (void *) &num_found);

	return num_found;
}


/* Iterate over all matching DFU capable devices within system */
static int iterate_dfu_devices(struct dfu_if *dif,
    int (*action)(struct usb_device *dev, void *user), void *user)
{
	struct usb_bus *usb_bus;
	struct usb_device *dev;

	/* Walk the tree and find our device. */
	for (usb_bus = usb_get_busses(); NULL != usb_bus;
	     usb_bus = usb_bus->next) {
		for (dev = usb_bus->devices; NULL != dev; dev = dev->next) {
			int retval;

			if (dif && (dif->flags &
			    (DFU_IFF_VENDOR|DFU_IFF_PRODUCT)) &&
		    	    (dev->descriptor.idVendor != dif->vendor ||
		     	    dev->descriptor.idProduct != dif->product))
				continue;
			if (dif && (dif->flags & DFU_IFF_DEVNUM) &&
		    	    (atoi(usb_bus->dirname) != dif->bus ||
		     	    dev->devnum != dif->devnum))
				continue;
			if (!count_dfu_interfaces(dev))
				continue;

			retval = action(dev, user);
			if (retval)
				return retval;
		}
	}
	return 0;
}


static int found_dfu_device(struct usb_device *dev, void *user)
{
	struct dfu_if *dif = user;

	dif->dev = dev;
	return 1;
}


/* Find the first DFU-capable device, save it in dfu_if->dev */
static int get_first_dfu_device(struct dfu_if *dif)
{
	return iterate_dfu_devices(dif, found_dfu_device, dif);
}


static int count_one_dfu_device(struct usb_device *dev, void *user)
{
	int *num = user;

	(*num)++;
	return 0;
}


/* Count DFU capable devices within system */
static int count_dfu_devices(struct dfu_if *dif)
{
	int num_found = 0;

	iterate_dfu_devices(dif, count_one_dfu_device, &num_found);
	return num_found;
}


static int list_dfu_interfaces(void)
{
	struct usb_bus *usb_bus;
	struct usb_device *dev;

	/* Walk the tree and find our device. */
	for (usb_bus = usb_get_busses(); NULL != usb_bus;
	     usb_bus = usb_bus->next ) {
		for (dev = usb_bus->devices; NULL != dev; dev = dev->next) {
			find_dfu_if(dev, &print_dfu_if, NULL);
		}
	}
	return 0;
}

static int parse_vendprod(struct usb_vendprod *vp, const char *str)
{
	unsigned long vend, prod;
	const char *colon;

	colon = strchr(str, ':');
	if (!colon || strlen(colon) < 2)
		return -EINVAL;

	vend = strtoul(str, NULL, 16);
	prod = strtoul(colon+1, NULL, 16);

	if (vend > 0xffff || prod > 0xffff)
		return -EINVAL;

	vp->vendor = vend;
	vp->product = prod;

	return 0;
}


#ifdef HAVE_USBPATH_H

static int resolve_device_path(struct dfu_if *dif)
{
	int res;

	res = usb_path2devnum(dif->path);
	if (res < 0)
		return -EINVAL;
	if (!res)
		return 0;

	dif->bus = atoi(dif->path);
	dif->devnum = res;
	dif->flags |= DFU_IFF_DEVNUM;
	return res;
}

#else /* HAVE_USBPATH_H */

static int resolve_device_path(struct dfu_if *dif)
{
	fprintf(stderr,
	    "USB device paths are not supported by this dfu-util.\n");
	exit(1);
}

#endif /* !HAVE_USBPATH_H */


static void help(void)
{
	printf("Usage: dfu-util [options] ...\n"
		"  -h --help\t\t\tPrint this help message\n"
		"  -V --version\t\t\tPrint the version number\n"
		"  -l --list\t\t\tList the currently attached DFU capable USB devices\n"
		"  -d --device vendor:product\tSpecify Vendor/Product ID of DFU device\n"
		"  -p --path bus-port. ... .port\tSpecify path to DFU device\n"
		"  -c --cfg config_nr\t\tSpecify the Configuration of DFU device\n"
		"  -i --intf intf_nr\t\tSpecify the DFU Interface number\n"
		"  -a --alt alt\t\t\tSpecify the Altsetting of the DFU Interface\n"
		"\t\t\t\tby name or by number\n"
		"  -t --transfer-size\t\tSpecify the number of bytes per USB Transfer\n"
		"  -U --upload file\t\tRead firmware from device into <file>\n"
		"  -D --download file\t\tWrite firmware from <file> into device\n"
		"  -R --reset\t\t\tIssue USB Reset signalling once we're finished\n"
		);
}

static void print_version(void)
{
	printf("dfu-util version %s\n", VERSION "+svn" DFU_UTIL_VERSION);
}

static struct option opts[] = {
	{ "help", 0, 0, 'h' },
	{ "version", 0, 0, 'V' },
	{ "verbose", 0, 0, 'v' },
	{ "list", 0, 0, 'l' },
	{ "device", 1, 0, 'd' },
	{ "path", 1, 0, 'p' },
	{ "configuration", 1, 0, 'c' },
	{ "cfg", 1, 0, 'c' },
	{ "interface", 1, 0, 'i' },
	{ "intf", 1, 0, 'i' },
	{ "altsetting", 1, 0, 'a' },
	{ "alt", 1, 0, 'a' },
	{ "transfer-size", 1, 0, 't' },
	{ "upload", 1, 0, 'U' },
	{ "download", 1, 0, 'D' },
	{ "reset", 0, 0, 'R' },
};

enum mode {
	MODE_NONE,
	MODE_UPLOAD,
	MODE_DOWNLOAD,
};

int main(int argc, char **argv)
{
	struct usb_vendprod vendprod;
	struct dfu_if _rt_dif, _dif, *dif = &_dif;
	int num_devs;
	int num_ifs;
	unsigned int transfer_size = 0;
	enum mode mode = MODE_NONE;
	struct dfu_status status;
	struct usb_dfu_func_descriptor func_dfu;
	char *filename = NULL;
	char *alt_name = NULL; /* query alt name if non-NULL */
	char *end;
	int final_reset = 0;
	int page_size = getpagesize();
	int ret;
	
	printf("dfu-util - (C) 2007-2008 by OpenMoko Inc.\n"
	       "This program is Free Software and has ABSOLUTELY NO WARRANTY\n\n");

	memset(dif, 0, sizeof(*dif));

	usb_init();
	//usb_set_debug(255);
	usb_find_busses();
	usb_find_devices();

	while (1) {
		int c, option_index = 0;
		c = getopt_long(argc, argv, "hVvld:p:c:i:a:t:U:D:R", opts,
				&option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			help();
			exit(0);
			break;
		case 'V':
			print_version();
			exit(0);
			break;
		case 'v':
			verbose = 1;
			break;
		case 'l':
			list_dfu_interfaces();
			exit(0);
			break;
		case 'd':
			/* Parse device */
			if (parse_vendprod(&vendprod, optarg) < 0) {
				fprintf(stderr, "unable to parse `%s'\n", optarg);
				exit(2);
			}
			dif->vendor = vendprod.vendor;
			dif->product = vendprod.product;
			dif->flags |= (DFU_IFF_VENDOR | DFU_IFF_PRODUCT);
			break;
		case 'p':
			/* Parse device path */
			dif->path = optarg;
			dif->flags |= DFU_IFF_PATH;
			ret = resolve_device_path(dif);
			if (ret < 0) {
				fprintf(stderr, "unable to parse `%s'\n",
				    optarg);
				exit(2);
			}
			if (!ret) {
				fprintf(stderr, "cannot find `%s'\n", optarg);
				exit(1);
			}
			break;
		case 'c':
			/* Configuration */
			dif->configuration = atoi(optarg);
			dif->flags |= DFU_IFF_CONFIG;
			break;
		case 'i':
			/* Interface */
			dif->interface = atoi(optarg);
			dif->flags |= DFU_IFF_IFACE;
			break;
		case 'a':
			/* Interface Alternate Setting */
			dif->altsetting = strtoul(optarg, &end, 0);
			if (*end)
				alt_name = optarg;
			dif->flags |= DFU_IFF_ALT;
			break;
		case 't':
			transfer_size = atoi(optarg);
			break;
		case 'U':
			mode = MODE_UPLOAD;
			filename = optarg;
			break;
		case 'D':
			mode = MODE_DOWNLOAD;
			filename = optarg;
			break;
		case 'R':
			final_reset = 1;
			break;
		default:
			help();
			exit(2);
		}
	}

	if (mode == MODE_NONE) {
		fprintf(stderr, "You need to specify one of -D or -U\n");
		help();
		exit(2);
	}

	if (!filename) {
		fprintf(stderr, "You need to specify a filename to -D -r -U\n");
		help();
		exit(2);
	}

	dfu_init(5000);

	num_devs = count_dfu_devices(dif);
	if (num_devs == 0) {
		fprintf(stderr, "No DFU capable USB device found\n");
		exit(1);
	} else if (num_devs > 1) {
		/* We cannot safely support more than one DFU capable device
		 * with same vendor/product ID, since during DFU we need to do
		 * a USB bus reset, after which the target device will get a
		 * new address */
		fprintf(stderr, "More than one DFU capable USB device found, "
		       "you might try `--list' and then disconnect all but one "
		       "device\n");
		exit(3);
	}
	if (!get_first_dfu_device(dif))
		exit(3);

	/* We have exactly one device. It's usb_device is now in dif->dev */

	printf("Opening USB Device 0x%04x:0x%04x...\n", dif->vendor, dif->product);
	dif->dev_handle = usb_open(dif->dev);
	if (!dif->dev_handle) {
		fprintf(stderr, "Cannot open device: %s\n", usb_strerror());
		exit(1);
	}

	/* try to find first DFU interface of device */
	memcpy(&_rt_dif, dif, sizeof(_rt_dif));
	if (!get_first_dfu_if(&_rt_dif))
		exit(1);

	if (!_rt_dif.flags & DFU_IFF_DFU) {
		/* In the 'first round' during runtime mode, there can only be one
	 	* DFU Interface descriptor according to the DFU Spec. */

		/* FIXME: check if the selected device really has only one */

		printf("Claiming USB DFU Runtime Interface...\n");
		if (usb_claim_interface(_rt_dif.dev_handle, _rt_dif.interface) < 0) {
			fprintf(stderr, "Cannot claim interface: %s\n",
				usb_strerror());
			exit(1);
		}

		if (usb_set_altinterface(_rt_dif.dev_handle, 0) < 0) {
			fprintf(stderr, "Cannot set alt interface: %s\n",
				usb_strerror());
			exit(1);
		}

		printf("Determining device status: ");
		if (dfu_get_status(_rt_dif.dev_handle, _rt_dif.interface, &status ) < 0) {
			fprintf(stderr, "error get_status: %s\n",
				usb_strerror());
			exit(1);
		}
		printf("state = %s, status = %d\n", 
		       dfu_state_to_string(status.bState), status.bStatus);

		switch (status.bState) {
		case DFU_STATE_appIDLE:
		case DFU_STATE_appDETACH:
			printf("Device really in Runtime Mode, send DFU "
			       "detach request...\n");
			if (dfu_detach(_rt_dif.dev_handle, 
				       _rt_dif.interface, 1000) < 0) {
				fprintf(stderr, "error detaching: %s\n",
					usb_strerror());
				exit(1);
				break;
			}
			printf("Resetting USB...\n");
			ret = usb_reset(_rt_dif.dev_handle);
			if (ret < 0 && ret != -ENODEV)
				fprintf(stderr,
					"error resetting after detach: %s\n", 
					usb_strerror());
			sleep(2);
			break;
		case DFU_STATE_dfuERROR:
			printf("dfuERROR, clearing status\n");
			if (dfu_clear_status(_rt_dif.dev_handle,
					     _rt_dif.interface) < 0) {
				fprintf(stderr, "error clear_status: %s\n",
					usb_strerror());
				exit(1);
				break;
			}
			break;
		default:
			fprintf(stderr, "WARNING: Runtime device already "
				"in DFU state ?!?\n");
			goto dfustate;
			break;
		}

		/* now we need to re-scan the bus and locate our device */
		if (usb_find_devices() < 2)
			printf("not at least 2 device changes found ?!?\n");

		if (dif->flags & DFU_IFF_PATH) {
			ret = resolve_device_path(dif);
			if (ret < 0) {
				fprintf(stderr,
				    "internal error: cannot re-parse `%s'\n",
				    dif->path);
				abort();
			}
			if (!ret) {
				fprintf(stderr,
				    "Can't resolve path after RESET?\n");
				exit(1);
			}
		}

		num_devs = count_dfu_devices(dif);
		if (num_devs == 0) {
			fprintf(stderr, "Lost device after RESET?\n");
			exit(1);
		} else if (num_devs > 1) {
			fprintf(stderr, "More than one DFU capable USB "
				"device found, you might try `--list' and "
				"then disconnect all but one device\n");
			exit(1);
		}
		if (!get_first_dfu_device(dif))
			exit(3);

		printf("Opening USB Device...\n");
		dif->dev_handle = usb_open(dif->dev);
		if (!dif->dev_handle) {
			fprintf(stderr, "Cannot open device: %s\n",
				usb_strerror());
			exit(1);
		}
	} else {
		/* we're already in DFU mode, so we can skip the detach/reset
		 * procedure */
	}

dfustate:
	if (alt_name) {
		int n;

		n = find_dfu_if(dif->dev, &alt_by_name, alt_name);
		if (!n) {
			fprintf(stderr, "No such Alternate Setting: \"%s\"\n",
			    alt_name);
			exit(1);
		}
		if (n < 0) {
			fprintf(stderr, "Error %d in name lookup\n", n);
			exit(1);
		}
		dif->altsetting = n-1;
	}

	print_dfu_if(dif, NULL);

	num_ifs = count_dfu_interfaces(dif->dev);
	if (num_ifs < 0) {
		fprintf(stderr, "No DFU Interface after RESET?!?\n");
		exit(1);
	} else if (num_ifs == 1) {
		if (!get_first_dfu_if(dif)) {
			fprintf(stderr, "Can't find the single available "
				"DFU IF\n");
			exit(1);
		}
	} else if (num_ifs > 1 && !(dif->flags & (DFU_IFF_IFACE|DFU_IFF_ALT))) {
		fprintf(stderr, "We have %u DFU Interfaces/Altsettings, "
			"you have to specify one via --intf / --alt options\n",
			num_ifs);
		exit(1);
	}

#if 0
	printf("Setting Configuration %u...\n", dif->configuration);
	if (usb_set_configuration(dif->dev_handle, dif->configuration) < 0) {
		fprintf(stderr, "Cannot set configuration: %s\n",
			usb_strerror());
		exit(1);
	}
#endif
	printf("Claiming USB DFU Interface...\n");
	if (usb_claim_interface(dif->dev_handle, dif->interface) < 0) {
		fprintf(stderr, "Cannot claim interface: %s\n",
			usb_strerror());
		exit(1);
	}

	printf("Setting Alternate Setting ...\n");
	if (usb_set_altinterface(dif->dev_handle, dif->altsetting) < 0) {
		fprintf(stderr, "Cannot set alternate interface: %s\n",
			usb_strerror());
		exit(1);
	}

status_again:
	printf("Determining device status: ");
	if (dfu_get_status(dif->dev_handle, dif->interface, &status ) < 0) {
		fprintf(stderr, "error get_status: %s\n", usb_strerror());
		exit(1);
	}
	printf("state = %s, status = %d\n",
	       dfu_state_to_string(status.bState), status.bStatus);

	switch (status.bState) {
	case DFU_STATE_appIDLE:
	case DFU_STATE_appDETACH:
		fprintf(stderr, "Device still in Runtime Mode!\n");
		exit(1);
		break;
	case DFU_STATE_dfuERROR:
		printf("dfuERROR, clearing status\n");
		if (dfu_clear_status(dif->dev_handle, dif->interface) < 0) {
			fprintf(stderr, "error clear_status: %s\n",
				usb_strerror());
			exit(1);
		}
		goto status_again;
		break;
	case DFU_STATE_dfuDNLOAD_IDLE:
	case DFU_STATE_dfuUPLOAD_IDLE:
		printf("aborting previous incomplete transfer\n");
		if (dfu_abort(dif->dev_handle, dif->interface) < 0) {
			fprintf(stderr, "can't send DFU_ABORT: %s\n",
				usb_strerror());
			exit(1);
		}
		goto status_again;
		break;
	case DFU_STATE_dfuIDLE:
		printf("dfuIDLE, continuing\n");
		break;
	}

	if (!transfer_size) {
		/* Obtain DFU functional descriptor */
		ret = usb_get_descriptor(dif->dev_handle, 0x21, dif->interface,
					 &func_dfu, sizeof(func_dfu));
		if (ret < 0) {
			fprintf(stderr, "Error obtaining DFU functional "
				"descriptor: %s\n", usb_strerror());
			transfer_size = page_size;
		} else {
			transfer_size = get_int16_le(&func_dfu.wTransferSize);
		}
	}

	if (transfer_size > page_size)
		transfer_size = page_size;
	
	printf("Transfer Size = 0x%04x\n", transfer_size);

	if (DFU_STATUS_OK != status.bStatus ) {
		printf("WARNING: DFU Status: '%s'\n",
			dfu_status_to_string(status.bStatus));
		/* Clear our status & try again. */
		dfu_clear_status(dif->dev_handle, dif->interface);
		dfu_get_status(dif->dev_handle, dif->interface, &status);

		if (DFU_STATUS_OK != status.bStatus) {
			fprintf(stderr, "Error: %d\n", status.bStatus);
			exit(1);
		}
        }

	switch (mode) {
	case MODE_UPLOAD:
		if (sam7dfu_do_upload(dif->dev_handle, dif->interface,
				  transfer_size, filename) < 0)
			exit(1);
		break;
	case MODE_DOWNLOAD:
		if (sam7dfu_do_dnload(dif->dev_handle, dif->interface,
				  transfer_size, filename) < 0)
			exit(1);
		break;
	default:
		fprintf(stderr, "Unsupported mode: %u\n", mode);
		exit(1);
	}

	if (final_reset) {
		if (dfu_detach(dif->dev_handle, dif->interface, 1000) < 0) {
			fprintf(stderr, "can't detach: %s\n", usb_strerror());
		}
		printf("Resetting USB to switch back to runtime mode\n");
		ret = usb_reset(dif->dev_handle);
		if (ret < 0 && ret != -ENODEV) {
			fprintf(stderr, "error resetting after download: %s\n", 
			usb_strerror());
		}
	}

	exit(0);
}

