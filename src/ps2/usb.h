/* USB drive access through the ps2sdk BDM stack (FAT and exFAT). The drivers
 * are embedded and only loaded when the backup mode is first used. */
#ifndef PS2_USB_H
#define PS2_USB_H

#include <stdint.h>

#ifdef USB_HOSTFS
/* `make hostfs`: PCSX2's host file system stands in for the drive */
#define USB_ROOT "host:"
#else
#define USB_ROOT "mass0:/"
#endif

/* Loads the drivers on a background thread, once. Module loads cannot time
 * out, so the UI polls usb_init_state() and gives up on its own. */
void usb_init_start(void);

#define USB_INIT_IDLE    0
#define USB_INIT_PENDING 2
#define USB_INIT_OK      1
#define USB_INIT_FAILED  (-1)
#define USB_INIT_BUSY    (-2)   /* another block-device stack is loaded (OPL) */
int usb_init_state(void);

/* The module that failed and its codes, or the step a load is stuck in. */
const char *usb_init_detail(void);

/* 1 if a drive is mounted. */
int usb_ready(void);

/* File size, or <0 if missing. */
int usb_size(const char *path);

/* Whole-file read. Returns the size, USB_TOO_BIG, or <0. */
#define USB_TOO_BIG (-100)
int usb_read(const char *path, void *buf, int max);

/* Whole-file write, truncating. Returns len, or <0. */
int usb_write(const char *path, const void *buf, int len);

int usb_remove(const char *path);
int usb_rename(const char *from, const char *to);
int usb_mkdir(const char *path);                  /* 0 if it exists afterwards */

#endif
