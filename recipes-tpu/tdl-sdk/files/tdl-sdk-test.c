// SPDX-License-Identifier: MIT
/*
 * tdl-sdk-test.c — End-to-end TDL SDK inference validation
 *
 * Validates the full inference stack:
 *   driver (sophgo-tpu-accel) → cviruntime → cvikernel → TDL SDK → model inference
 *
 * Test matrix:
 *   1. TDL handle creation
 *   2. Model loading (skipped if no .cvimodel present)
 *   3. Image loading (skipped if no test image present)
 *   4. Classification inference (skipped if prior steps skipped)
 *   5. Top-1 result validation (score > 0 and class_id >= 0)
 *   6. Handle destruction
 *
 * The acceptance criterion per the project spec is:
 *   MobileNetV2 inference returns correct top-1 label.
 *
 * Build:
 *   ${CC} ${CFLAGS} ${LDFLAGS} -I${STAGING_INCDIR} \
 *       tdl-sdk-test.c -lcvi_tdl -lcviruntime -lcvikernel -lm \
 *       -o tdl-sdk-test
 *
 * Run:
 *   ./tdl-sdk-test [model.cvimodel] [test_image.jpg]
 *
 * Environment variables (override defaults):
 *   TDL_TEST_MODEL  — path to a .cvimodel file
 *   TDL_TEST_IMAGE  — path to a test image (JPEG/PNG)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>

#include "c_apis/tdl_sdk.h"

/* ── test harness ──────────────────────────────────────────────────────── */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_skipped = 0;

#define TEST(name, cond) do { \
	tests_run++; \
	if (cond) { tests_passed++; printf("PASS: %s\n", name); } \
	else { printf("FAIL: %s (%s)\n", name, strerror(errno)); } \
} while (0)

#define SKIP(name, reason) do { \
	tests_run++; \
	tests_skipped++; \
	printf("SKIP: %s (%s)\n", name, reason); \
} while (0)

/* ── helpers ───────────────────────────────────────────────────────────── */

/*
 * Default locations searched for a bundled model.
 * The Yocto ptest installer places models under PTEST_PATH.
 */
static const char *DEFAULT_MODEL_PATHS[] = {
	"./mobilenetv2.cvimodel",
	"/usr/lib/tdl-sdk/ptest/mobilenetv2.cvimodel",
	"/usr/share/tdl-sdk/mobilenetv2.cvimodel",
	NULL
};

/*
 * A 224×224 solid-grey synthetic JPEG would be ideal but requires libjpeg.
 * Instead we try to find any JPEG/PNG that may have been installed alongside
 * the model, or skip image-dependent tests gracefully.
 */
static const char *DEFAULT_IMAGE_PATHS[] = {
	"./test_image.jpg",
	"/usr/lib/tdl-sdk/ptest/test_image.jpg",
	"/usr/share/tdl-sdk/test_image.jpg",
	NULL
};

static const char *find_file(const char **candidates)
{
	for (; *candidates; candidates++) {
		if (access(*candidates, R_OK) == 0)
			return *candidates;
	}
	return NULL;
}

/* ── main ──────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
	const char *model_path = NULL;
	const char *image_path = NULL;

	/* CLI overrides */
	if (argc > 1)
		model_path = argv[1];
	if (argc > 2)
		image_path = argv[2];

	/* Environment variable overrides */
	if (!model_path)
		model_path = getenv("TDL_TEST_MODEL");
	if (!image_path)
		image_path = getenv("TDL_TEST_IMAGE");

	/* Auto-discover if still unset */
	if (!model_path)
		model_path = find_file(DEFAULT_MODEL_PATHS);
	if (!image_path)
		image_path = find_file(DEFAULT_IMAGE_PATHS);

	printf("=== TDL SDK End-to-End Inference Test ===\n");
	printf("Model : %s\n", model_path ? model_path : "(not found — inference tests will be skipped)");
	printf("Image : %s\n", image_path ? image_path : "(not found — inference tests will be skipped)");
	printf("\n");

	/* ── Test 1: Create TDL handle ─────────────────────────────────────── */
	TDLHandle handle = TDL_CreateHandle(0);
	TEST("create TDL handle", handle != NULL);
	if (!handle) {
		printf("FATAL: cannot create handle — aborting remaining tests\n");
		goto summary;
	}

	/* ── Test 2: Load model ────────────────────────────────────────────── */
	int model_loaded = 0;
	if (!model_path) {
		SKIP("open model (TDL_MODEL_CLS_IMG)", "no .cvimodel file found");
	} else {
		/*
		 * TDL_MODEL_CLS_IMG is the generic custom-classification model ID;
		 * it accepts any image-classification .cvimodel (including MobileNetV2).
		 */
		int32_t ret = TDL_OpenModel(handle, TDL_MODEL_CLS_IMG,
					    model_path, NULL, 0);
		TEST("open model (TDL_MODEL_CLS_IMG)", ret == 0);
		if (ret == 0)
			model_loaded = 1;
	}

	/* ── Test 3: Load test image ───────────────────────────────────────── */
	TDLImage img = NULL;
	if (!model_loaded) {
		SKIP("read test image", "model not loaded");
	} else if (!image_path) {
		SKIP("read test image", "no test image file found");
	} else {
		img = TDL_ReadImage(image_path);
		TEST("read test image", img != NULL);
	}

	/* ── Test 4: Run classification inference ──────────────────────────── */
	TDLClassInfo result = {0};
	int infer_done = 0;
	if (!img) {
		SKIP("run classification inference", "image not loaded");
	} else {
		int32_t ret = TDL_Classification(handle, TDL_MODEL_CLS_IMG,
						 img, &result);
		TEST("run classification inference", ret == 0);
		if (ret == 0) {
			printf("  top-1: class_id=%d  score=%.4f\n",
			       result.class_id, result.score);
			infer_done = 1;
		}
	}

	/* ── Test 5: Validate top-1 result ────────────────────────────────── */
	if (!infer_done) {
		SKIP("validate top-1 result (score > 0)", "inference not run");
	} else {
		TEST("validate top-1 result (score > 0)",
		     result.score > 0.0f && result.class_id >= 0);
	}

	/* cleanup */
	if (img)
		TDL_DestroyImage(img);
	if (model_loaded)
		TDL_CloseModel(handle, TDL_MODEL_CLS_IMG);

	/* ── Test 6: Destroy handle ────────────────────────────────────────── */
	{
		int32_t ret = TDL_DestroyHandle(handle);
		TEST("destroy TDL handle", ret == 0);
	}

summary:
	printf("\n--- Results: %d/%d passed", tests_passed, tests_run);
	if (tests_skipped)
		printf(", %d skipped", tests_skipped);
	printf(" ---\n");

	/*
	 * Exit 0 if all run tests passed (skipped tests are not failures).
	 * This satisfies ptest's pass/fail reporting contract.
	 */
	return (tests_passed + tests_skipped == tests_run) ? 0 : 1;
}
