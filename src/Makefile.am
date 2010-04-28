AM_CFLAGS = -Wall

dfu-version.h:
	echo -e '#ifndef DFU_UTIL_VERSION' \
	'\n#define DFU_UTIL_VERSION "'`svnversion -n`'"' \
	'\n#endif' > dfu-version.h
BUILT_SOURCES = dfu-version.h

bin_PROGRAMS = dfu-util dfu-util_static
dfu_util_SOURCES = main.c \
               sam7dfu.c \
               dfu.c \
               dfu.h \
               dfu_sm.c \
               dfu_sm.h \
               dfu_suffix.c \
               dfu_quirks.c \
               dfu_quirks.h \
               usb_dfu.c \
               crc32.c \
               crc32.h

dfu_util_static_SOURCES = main.c \
                       sam7dfu.c \
                       dfu.c \
                       dfu.h \
                       dfu_sm.c \
                       dfu_sm.h \
                       dfu_suffix.c \
                       dfu_quirks.c \
                       dfu_quirks.h \
                       usb_dfu.c \
                       crc32.c \
                       crc32.h

dfu_util_static_LDFLAGS = -static

# commands.c commands.h sam7dfu.c

