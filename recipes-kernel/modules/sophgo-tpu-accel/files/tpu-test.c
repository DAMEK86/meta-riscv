// SPDX-License-Identifier: MIT
/*
 * tpu-test.c — Minimal Sophgo TPU accel driver verification
 *
 * Tests: device open, GEM buffer creation, mmap, read/write, cache sync.
 * Does NOT require cviruntime or model files.
 *
 * Build: ${CC} -o tpu-test tpu-test.c
 * Run:   ./tpu-test [/dev/accel/accel0]
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

/* Inline the UAPI definitions to avoid header dependency */
#include <drm/drm.h>

#define DRM_SOPHGO_TPU_CREATE_BO	0x00
#define DRM_SOPHGO_TPU_SYNC		0x04
#define SOPHGO_TPU_SYNC_FLUSH		(1 << 0)
#define SOPHGO_TPU_SYNC_INVALIDATE	(1 << 1)

struct drm_sophgo_tpu_create_bo {
	uint64_t size;
	uint32_t flags;
	uint32_t handle;
	uint64_t offset;
	uint64_t paddr;
};

struct drm_sophgo_tpu_sync {
	uint32_t handle;
	uint32_t flags;
	uint64_t offset;
	uint64_t size;
};

#define DRM_IOCTL_SOPHGO_TPU_CREATE_BO \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_SOPHGO_TPU_CREATE_BO, \
		  struct drm_sophgo_tpu_create_bo)
#define DRM_IOCTL_SOPHGO_TPU_SYNC \
	DRM_IOW(DRM_COMMAND_BASE + DRM_SOPHGO_TPU_SYNC, \
		 struct drm_sophgo_tpu_sync)

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name, cond) do { \
	tests_run++; \
	if (cond) { tests_passed++; printf("PASS: %s\n", name); } \
	else { printf("FAIL: %s (%s)\n", name, strerror(errno)); } \
} while (0)

int main(int argc, char *argv[])
{
	const char *dev = argc > 1 ? argv[1] : "/dev/accel/accel0";
	int fd, ret;

	printf("=== Sophgo TPU Accel Driver Test ===\n");
	printf("Device: %s\n\n", dev);

	/* Test 1: Open device */
	fd = open(dev, O_RDWR);
	TEST("open device", fd >= 0);
	if (fd < 0)
		return 1;

	/* Test 2: Create GEM buffer */
	struct drm_sophgo_tpu_create_bo create = { .size = 4096, .flags = 0 };
	ret = ioctl(fd, DRM_IOCTL_SOPHGO_TPU_CREATE_BO, &create);
	TEST("create GEM BO", ret == 0);
	if (ret == 0)
		printf("  handle=%u offset=0x%llx paddr=0x%llx size=%llu\n",
		       create.handle, (unsigned long long)create.offset,
		       (unsigned long long)create.paddr,
		       (unsigned long long)create.size);

	/* Test 3: mmap the buffer */
	void *buf = NULL;
	if (ret == 0) {
		buf = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED,
			   fd, create.offset);
		TEST("mmap buffer", buf != MAP_FAILED);
	}

	/* Test 4: Write and read back */
	if (buf && buf != MAP_FAILED) {
		memset(buf, 0xAB, 4096);
		TEST("write pattern", ((uint8_t *)buf)[0] == 0xAB);
		TEST("read back", ((uint8_t *)buf)[4095] == 0xAB);
	}

	/* Test 5: Cache sync */
	if (ret == 0) {
		struct drm_sophgo_tpu_sync sync = {
			.handle = create.handle,
			.flags = SOPHGO_TPU_SYNC_FLUSH,
			.offset = 0,
			.size = 0,
		};
		int sync_ret = ioctl(fd, DRM_IOCTL_SOPHGO_TPU_SYNC, &sync);
		TEST("cache flush", sync_ret == 0);

		sync.flags = SOPHGO_TPU_SYNC_INVALIDATE;
		sync_ret = ioctl(fd, DRM_IOCTL_SOPHGO_TPU_SYNC, &sync);
		TEST("cache invalidate", sync_ret == 0);
	}

	/* Test 6: Close handle (GEM cleanup) */
	if (ret == 0) {
		struct drm_gem_close gem_close = { .handle = create.handle };
		int close_ret = ioctl(fd, DRM_IOCTL_GEM_CLOSE, &gem_close);
		TEST("GEM close", close_ret == 0);
	}

	/* Cleanup */
	if (buf && buf != MAP_FAILED)
		munmap(buf, 4096);
	close(fd);

	printf("\n%d/%d tests passed\n", tests_passed, tests_run);
	return tests_passed == tests_run ? 0 : 1;
}
