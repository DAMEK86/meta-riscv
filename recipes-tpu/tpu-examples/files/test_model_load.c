/*
 * test_model_load.c — Verify cviruntime can load a .cvimodel via DRM/accel
 *
 * This proves the full stack: DRM/accel driver -> GEM alloc -> cviruntime -> model parse
 *
 * Build on target:
 *   gcc -o test_model_load test_model_load.c -lcviruntime -lcvikernel -lm
 * Run:
 *   ./test_model_load /path/to/model.cvimodel
 */
#include <stdio.h>
#include <stdlib.h>
#include <cviruntime.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <model.cvimodel>\n", argv[0]);
        return 1;
    }

    const char *model_path = argv[1];
    CVI_MODEL_HANDLE model = NULL;
    CVI_RC ret;
    int pass = 0, total = 0;

    printf("=== cviruntime Model Load Test ===\n");
    printf("Model: %s\n\n", model_path);

    /* Test 1: Register model */
    total++;
    ret = CVI_NN_RegisterModel(model_path, &model);
    if (ret == 0 && model != NULL) {
        printf("PASS: CVI_NN_RegisterModel\n");
        pass++;
    } else {
        printf("FAIL: CVI_NN_RegisterModel (ret=%d)\n", ret);
        return 1;
    }

    /* Test 2: Get input tensor info */
    total++;
    CVI_TENSOR *input_tensors = NULL;
    int32_t input_num = 0;
    ret = CVI_NN_GetInputOutputTensors(model, &input_tensors, &input_num,
                                        NULL, NULL);
    if (ret == 0 && input_num > 0) {
        printf("PASS: CVI_NN_GetInputOutputTensors (inputs=%d)\n", input_num);
        pass++;
        for (int i = 0; i < input_num; i++) {
            printf("  input[%d]: name=%s shape=[", i, input_tensors[i].name);
            for (size_t d = 0; d < input_tensors[i].shape.dim_size; d++) {
                printf("%d%s", input_tensors[i].shape.dim[d],
                       d < input_tensors[i].shape.dim_size - 1 ? "," : "");
            }
            printf("]\n");
        }
    } else {
        printf("FAIL: CVI_NN_GetInputOutputTensors (ret=%d)\n", ret);
    }

    /* Test 3: Get output tensor info */
    total++;
    CVI_TENSOR *output_tensors = NULL;
    int32_t output_num = 0;
    ret = CVI_NN_GetInputOutputTensors(model, NULL, NULL,
                                        &output_tensors, &output_num);
    if (ret == 0 && output_num > 0) {
        printf("PASS: CVI_NN_GetOutputTensors (outputs=%d)\n", output_num);
        pass++;
        for (int i = 0; i < output_num; i++) {
            printf("  output[%d]: name=%s shape=[", i, output_tensors[i].name);
            for (size_t d = 0; d < output_tensors[i].shape.dim_size; d++) {
                printf("%d%s", output_tensors[i].shape.dim[d],
                       d < output_tensors[i].shape.dim_size - 1 ? "," : "");
            }
            printf("]\n");
        }
    } else {
        printf("FAIL: CVI_NN_GetOutputTensors (ret=%d)\n", ret);
    }

    /* Test 4: Run forward pass with zeros (just to prove execution works) */
    total++;
    if (input_tensors && output_tensors) {
        /* Input is already zeroed by runtime — just run it */
        ret = CVI_NN_Forward(model, input_tensors, input_num,
                             output_tensors, output_num);
        if (ret == 0) {
            printf("PASS: CVI_NN_Forward (inference completed!)\n");
            pass++;
        } else {
            printf("FAIL: CVI_NN_Forward (ret=%d)\n", ret);
        }
    }

    /* Cleanup */
    total++;
    ret = CVI_NN_CleanupModel(model);
    if (ret == 0) {
        printf("PASS: CVI_NN_CleanupModel\n");
        pass++;
    } else {
        printf("FAIL: CVI_NN_CleanupModel (ret=%d)\n", ret);
    }

    printf("\n%d/%d tests passed\n", pass, total);
    return pass == total ? 0 : 1;
}
