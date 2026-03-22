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
#define DRM_SOPHGO_TPU_SUBMIT		0x02
#define DRM_SOPHGO_TPU_WAIT		0x03
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

struct drm_sophgo_tpu_submit {
	uint32_t cmdbuf_handle;
	uint32_t cmdbuf_size;
	uint32_t seq_no;
	uint32_t pad;
};

struct drm_sophgo_tpu_wait {
	uint32_t seq_no;
	uint32_t timeout_ms;
	int32_t status;
	uint32_t pad;
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
#define DRM_IOCTL_SOPHGO_TPU_SUBMIT \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_SOPHGO_TPU_SUBMIT, \
		  struct drm_sophgo_tpu_submit)
#define DRM_IOCTL_SOPHGO_TPU_WAIT \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_SOPHGO_TPU_WAIT, \
		  struct drm_sophgo_tpu_wait)
#define DRM_IOCTL_SOPHGO_TPU_SYNC \
	DRM_IOW(DRM_COMMAND_BASE + DRM_SOPHGO_TPU_SYNC, \
		 struct drm_sophgo_tpu_sync)

/*
 * TPU DMA buffer header — must match the kernel driver's struct dma_hdr.
 * This is the format expected by SOPHGO_TPU_SUBMIT.
 */
#define TPU_DMABUF_HEADER_M	0xB5B5

struct __attribute__((packed)) tpu_dma_hdr {
	uint16_t magic_m;
	uint16_t magic_s;
	uint32_t size;
	uint32_t cpu_desc_count;
	uint32_t bd_desc_count;
	uint32_t tdma_desc_count;
	uint32_t tpu_clk_rate;
	uint32_t pmubuf_size;
	uint32_t pmubuf_offset;
	uint32_t arraybase[16];
	uint32_t reserved[8];
};

/* TDMA descriptor bits */
#define TDMA_ACCPI0_EOD_BIT		2
#define TDMA_ACCPI0_INTERRUPT_BIT	3
#define TDMA_ACCPI0_BARRIER_ENABLE_BIT	4

/*
 * Build a minimal TPU command buffer that contains:
 * - dma_hdr with magic + 0 TIU descriptors + 1 TDMA descriptor
 * - One TDMA descriptor with EOD=1 + INTERRUPT=1 (NOP — no actual transfer)
 *
 * This triggers the TDMA engine to process the descriptor, fire the
 * completion IRQ, and return — proving the full HW path works.
 */
static int build_nop_cmdbuf(void *buf, size_t bufsize, uint64_t paddr)
{
	/* We need: 128-byte header + 64-byte cpu_desc + 128-byte TDMA desc */
	if (bufsize < 512)
		return -1;

	memset(buf, 0, 512);

	struct tpu_dma_hdr *hdr = (struct tpu_dma_hdr *)buf;
	hdr->magic_m = TPU_DMABUF_HEADER_M;
	hdr->magic_s = 0x1822;
	hdr->size = 512;
	hdr->cpu_desc_count = 1;
	hdr->bd_desc_count = 0;
	hdr->tdma_desc_count = 1;

	/*
	 * cpu_desc sits right after the header (offset 128).
	 * It tells the driver: 0 TIU ops, 1 TDMA op, TDMA descs at offset X.
	 */
	uint32_t *cpu_desc = (uint32_t *)((uint8_t *)buf + 128);
	/* cpu_desc layout: num_tiu(u16)|flags(u16), num_tdma(u16)|flags(u16),
	 *                  offset_tiu, offset_tdma, ... */
	cpu_desc[0] = 0;        /* num_tiu = 0 */
	cpu_desc[1] = 1;        /* num_tdma = 1 */
	cpu_desc[2] = 0;        /* offset_tiu (unused) */
	cpu_desc[3] = 256;      /* offset_tdma = byte offset 256 from buf start */

	/*
	 * TDMA descriptor at offset 256 — a single NOP with EOD + interrupt.
	 * The first u32 of a TDMA descriptor contains control bits.
	 */
	uint32_t *tdma_desc = (uint32_t *)((uint8_t *)buf + 256);
	tdma_desc[0] = (1 << TDMA_ACCPI0_EOD_BIT) |
		       (1 << TDMA_ACCPI0_INTERRUPT_BIT) |
		       (1 << TDMA_ACCPI0_BARRIER_ENABLE_BIT);
	/* All other fields zero = NOP (no src, no dst, zero length) */

	return 512;
}

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

	/* Cleanup first buffer */
	if (buf && buf != MAP_FAILED)
		munmap(buf, 4096);

	/*
	 * Test 7-9: Hardware smoke test — submit NOP to TPU
	 *
	 * Allocate a command buffer, fill it with a minimal NOP TDMA
	 * descriptor, submit to the TPU, and wait for the completion IRQ.
	 * This proves the full hardware path: driver → TDMA engine → IRQ.
	 */
	printf("\n--- Hardware Smoke Test ---\n");

	struct drm_sophgo_tpu_create_bo cmdbuf_bo = { .size = 4096, .flags = 0 };
	ret = ioctl(fd, DRM_IOCTL_SOPHGO_TPU_CREATE_BO, &cmdbuf_bo);
	TEST("create cmdbuf BO", ret == 0);

	if (ret == 0) {
		void *cmdbuf = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
				    MAP_SHARED, fd, cmdbuf_bo.offset);
		if (cmdbuf != MAP_FAILED) {
			int cmdsz = build_nop_cmdbuf(cmdbuf, 4096, cmdbuf_bo.paddr);
			TEST("build NOP cmdbuf", cmdsz > 0);

			/* Flush cmdbuf to device */
			struct drm_sophgo_tpu_sync csync = {
				.handle = cmdbuf_bo.handle,
				.flags = SOPHGO_TPU_SYNC_FLUSH,
			};
			ioctl(fd, DRM_IOCTL_SOPHGO_TPU_SYNC, &csync);

			/* Submit to TPU */
			struct drm_sophgo_tpu_submit submit = {
				.cmdbuf_handle = cmdbuf_bo.handle,
				.cmdbuf_size = cmdsz,
			};
			int submit_ret = ioctl(fd, DRM_IOCTL_SOPHGO_TPU_SUBMIT, &submit);
			TEST("submit NOP to TPU", submit_ret == 0);

			if (submit_ret == 0) {
				/* Wait for completion (5 second timeout) */
				struct drm_sophgo_tpu_wait wait = {
					.seq_no = submit.seq_no,
					.timeout_ms = 5000,
				};
				int wait_ret = ioctl(fd, DRM_IOCTL_SOPHGO_TPU_WAIT, &wait);
				TEST("wait for TPU completion (IRQ)", wait_ret == 0 && wait.status == 0);
				if (wait_ret != 0)
					printf("  wait returned %d, status=%d\n", wait_ret, wait.status);
			}

			munmap(cmdbuf, 4096);
		}

		struct drm_gem_close gc = { .handle = cmdbuf_bo.handle };
		ioctl(fd, DRM_IOCTL_GEM_CLOSE, &gc);
	}

	close(fd);

	printf("\n%d/%d tests passed\n", tests_passed, tests_run);
	return tests_passed == tests_run ? 0 : 1;
}
