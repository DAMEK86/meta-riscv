// SPDX-License-Identifier: MIT
/*
 * cviruntime-test.c — Minimal cviruntime DRM/accel validation
 *
 * Tests the low-level DRM/GEM path that cviruntime uses internally:
 * open accel device, allocate a GEM buffer object, mmap it, write/read
 * a pattern, cache-flush, and clean up. This validates our DRM/accel
 * port works end-to-end without needing a .cvimodel file.
 *
 * Build: ${CC} -o cviruntime-test cviruntime-test.c
 * Run:   ./cviruntime-test [/dev/accel/accel0]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdint.h>
#include <errno.h>

#include <drm/drm.h>

/* Inline Sophgo TPU UAPI — matches sophgo-tpu-accel driver */
#define DRM_SOPHGO_TPU_CREATE_BO    0x00
#define DRM_SOPHGO_TPU_SYNC         0x01

#define DRM_IOCTL_SOPHGO_TPU_CREATE_BO \
    DRM_IOWR(DRM_COMMAND_BASE + DRM_SOPHGO_TPU_CREATE_BO, \
             struct drm_sophgo_tpu_create_bo)
#define DRM_IOCTL_SOPHGO_TPU_SYNC \
    DRM_IOW(DRM_COMMAND_BASE + DRM_SOPHGO_TPU_SYNC, \
            struct drm_sophgo_tpu_sync)

struct drm_sophgo_tpu_create_bo {
    uint64_t size;
    uint32_t flags;
    uint32_t handle;
    uint64_t offset;
    uint64_t paddr;
};

#define SOPHGO_TPU_SYNC_FLUSH       (1 << 0)
#define SOPHGO_TPU_SYNC_INVALIDATE  (1 << 1)

struct drm_sophgo_tpu_sync {
    uint32_t handle;
    uint32_t flags;
    uint64_t offset;
    uint64_t size;
};

/* ------------------------------------------------------------------ */

static int tests_run    = 0;
static int tests_passed = 0;

#define TEST(name, cond) do { \
    tests_run++; \
    if (cond) { tests_passed++; printf("PASS: %s\n", name); } \
    else { printf("FAIL: %s (%s)\n", name, strerror(errno)); } \
} while (0)

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    const char *dev = argc > 1 ? argv[1] : "/dev/accel/accel0";
    int fd, ret;

    printf("=== cviruntime DRM/accel Validation ===\n");
    printf("Device: %s\n\n", dev);

    /* Test 1: Open accel device */
    fd = open(dev, O_RDWR);
    TEST("open accel device", fd >= 0);
    if (fd < 0)
        return 1;

    /* Test 2: Allocate 4 KB GEM buffer (mirrors CVI_RT_Malloc) */
    struct drm_sophgo_tpu_create_bo bo = { .size = 4096, .flags = 0 };
    ret = ioctl(fd, DRM_IOCTL_SOPHGO_TPU_CREATE_BO, &bo);
    TEST("allocate GEM BO (4 KB)", ret == 0);
    if (ret != 0) {
        close(fd);
        return 1;
    }
    printf("  handle=%u  paddr=0x%llx  offset=0x%llx  size=%llu\n",
           bo.handle,
           (unsigned long long)bo.paddr,
           (unsigned long long)bo.offset,
           (unsigned long long)bo.size);

    /* Test 3: mmap — get virtual address (mirrors CVI_RT_MemGetVirtualAddr) */
    void *vaddr = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, (off_t)bo.offset);
    TEST("mmap buffer (get vaddr)", vaddr != MAP_FAILED);

    /* Test 4: Write pattern into device memory */
    if (vaddr != MAP_FAILED) {
        memset(vaddr, 0xA5, 4096);
        TEST("write pattern 0xA5", ((uint8_t *)vaddr)[0] == 0xA5);
        TEST("read back last byte", ((uint8_t *)vaddr)[4095] == 0xA5);
    }

    /* Test 5: Cache flush (mirrors CVI_RT_MemFlush) */
    struct drm_sophgo_tpu_sync sync_flush = {
        .handle = bo.handle,
        .flags  = SOPHGO_TPU_SYNC_FLUSH,
        .offset = 0,
        .size   = 0,
    };
    ret = ioctl(fd, DRM_IOCTL_SOPHGO_TPU_SYNC, &sync_flush);
    TEST("cache flush", ret == 0);

    /* Test 6: Cache invalidate (mirrors CVI_RT_MemInvCache) */
    struct drm_sophgo_tpu_sync sync_inv = {
        .handle = bo.handle,
        .flags  = SOPHGO_TPU_SYNC_INVALIDATE,
        .offset = 0,
        .size   = 0,
    };
    ret = ioctl(fd, DRM_IOCTL_SOPHGO_TPU_SYNC, &sync_inv);
    TEST("cache invalidate", ret == 0);

    /* Test 7: Unmap (mirrors CVI_RT_MemFree vaddr release) */
    if (vaddr != MAP_FAILED) {
        ret = munmap(vaddr, 4096);
        TEST("munmap buffer", ret == 0);
    }

    /* Test 8: Free GEM handle (mirrors CVI_RT_MemFree handle release) */
    struct drm_gem_close gem_close = { .handle = bo.handle };
    ret = ioctl(fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
    TEST("free GEM handle", ret == 0);

    /* Cleanup */
    close(fd);

    /* Summary */
    printf("\n=== Results: %d/%d tests passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
