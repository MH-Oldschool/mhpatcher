/* fileXio descriptors never leave this file, so using it directly is safe. */
#define NEWLIB_PORT_AWARE
#include "usb.h"
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <fileXio_rpc.h>
#include <io_common.h>
#include <sbv_patches.h>
#include <string.h>
#include <stdio.h>

/* embedded by the Makefile from $PS2SDK/iop/irx */
#define IRX(n) extern unsigned char n##_irx[]; extern unsigned int size_##n##_irx;
IRX(iomanX) IRX(fileXio) IRX(usbd) IRX(bdm) IRX(bdmfs_fatfs) IRX(usbmass_bd)

#define USB_CHUNK 0x10000

static volatile int usb_state = USB_INIT_IDLE;

/* current step, or what failed */
static char diag[64] = "";

/* id < 0: not loaded (-200: missing import). res 1: did not stay resident,
 * usually because its library is already registered. */
static int load(const char *name, void *irx, unsigned int size) {
    int res = 1;
    snprintf(diag, sizeof diag, "loading %s", name);
    int id = SifExecModuleBuffer(irx, size, 0, NULL, &res);
    if (id >= 0 && (res & 3) != 1) return 1;
    snprintf(diag, sizeof diag, "%s: id %d res %d", name, id, res);
    return 0;
}

static void init_thread(void *arg) {
    (void)arg;
    /* without these, the BIOS LOADFILE never answers SifExecModuleBuffer */
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    /* either may already be resident; fileXioInit() is the real check */
    load("iomanX", iomanX_irx, size_iomanX_irx);
    load("fileXio", fileXio_irx, size_fileXio_irx);
    int ok = 1;
#ifndef USB_HOSTFS
    /* OPL's in-game cdvdman exports a reduced "bdm" (one device, no file
     * systems) when the game runs from USB/iLink/MX4SIO/exFAT HDD. */
    if (!load("bdm", bdm_irx, size_bdm_irx)) {
        usb_state = USB_INIT_BUSY;
        ExitDeleteThread();
    }
    /* OPL may already have usbd loaded for pad emulation; that one works */
    char usbd_note[40] = "";
    if (!load("usbd", usbd_irx, size_usbd_irx))
        snprintf(usbd_note, sizeof usbd_note, "%.36s; ", diag);
    ok = load("bdmfs_fatfs", bdmfs_fatfs_irx, size_bdmfs_fatfs_irx)
      && load("usbmass_bd", usbmass_bd_irx, size_usbmass_bd_irx);
    if (!ok && usbd_note[0]) {
        char last[64];
        snprintf(last, sizeof last, "%s", diag);
        snprintf(diag, sizeof diag, "%s%s", usbd_note, last);
    }
#endif
    if (ok) {
        snprintf(diag, sizeof diag, "fileXioInit");
        int r = fileXioInit();
        if (r < 0) { snprintf(diag, sizeof diag, "fileXioInit: %d", r); ok = 0; }
    }
    if (ok) diag[0] = 0;
    usb_state = ok ? USB_INIT_OK : USB_INIT_FAILED;
    ExitDeleteThread();
}

const char *usb_init_detail(void) { return diag; }

extern void *_gp;
static u8 init_stack[0x4000] __attribute__((aligned(16)));

void usb_init_start(void) {
    if (usb_state != USB_INIT_IDLE) return;
    usb_state = USB_INIT_PENDING;
    /* the loader outranks the UI and sleeps in every RPC */
    ChangeThreadPriority(GetThreadId(), 64);
    ee_thread_t t;
    memset(&t, 0, sizeof t);
    t.func = (void *)init_thread;
    t.stack = init_stack;
    t.stack_size = sizeof init_stack;
    t.gp_reg = &_gp;
    t.initial_priority = 32;
    int id = CreateThread(&t);
    if (id < 0 || StartThread(id, NULL) < 0) usb_state = USB_INIT_FAILED;
}

int usb_init_state(void) { return usb_state; }

int usb_ready(void) {
    if (usb_state != USB_INIT_OK) return 0;
    int fd = fileXioDopen(USB_ROOT);
    if (fd < 0) return 0;
    fileXioDclose(fd);
    return 1;
}

int usb_size(const char *path) {
    int fd = fileXioOpen(path, FIO_O_RDONLY);
    if (fd < 0) return -1;
    int size = fileXioLseek(fd, 0, FIO_SEEK_END);
    fileXioClose(fd);
    return size;
}

int usb_read(const char *path, void *buf, int max) {
    int fd = fileXioOpen(path, FIO_O_RDONLY);
    if (fd < 0) return -1;
    int size = fileXioLseek(fd, 0, FIO_SEEK_END);
    if (size < 0 || fileXioLseek(fd, 0, FIO_SEEK_SET) != 0) { fileXioClose(fd); return -1; }
    if (size > max) { fileXioClose(fd); return USB_TOO_BIG; }
    int off = 0;
    while (off < size) {
        int want = size - off > USB_CHUNK ? USB_CHUNK : size - off;
        int got = fileXioRead(fd, (uint8_t *)buf + off, want);
        if (got != want) { fileXioClose(fd); return -1; }
        off += got;
    }
    fileXioClose(fd);
    return size;
}

int usb_write(const char *path, const void *buf, int len) {
    int fd = fileXioOpen(path, FIO_O_WRONLY | FIO_O_CREAT | FIO_O_TRUNC, 0666);
    if (fd < 0) return -1;
    int off = 0, ok = 1;
    while (ok && off < len) {
        int want = len - off > USB_CHUNK ? USB_CHUNK : len - off;
        int put = fileXioWrite(fd, (const uint8_t *)buf + off, want);
        if (put != want) ok = 0;         /* short write: drive full */
        off += want;
    }
    if (fileXioClose(fd) < 0) ok = 0;    /* close flushes the FAT */
    return ok ? len : -1;
}

int usb_remove(const char *path) {
    return fileXioRemove(path) < 0 ? -1 : 0;
}

int usb_rename(const char *from, const char *to) {
    return fileXioRename(from, to) < 0 ? -1 : 0;
}

int usb_mkdir(const char *path) {
    if (fileXioMkdir(path, 0777) >= 0) return 0;
    int fd = fileXioDopen(path);         /* already there is fine */
    if (fd < 0) return -1;
    fileXioDclose(fd);
    return 0;
}
