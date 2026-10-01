#pragma once
#include <stdint.h>
#include <string.h>
/* Host declarations only. Implementations are the project's unmodified
 * CMSIS DSP matrix .c files, compiled with the portable (no DSP/FPU) path. */
typedef float float32_t;

typedef enum
{
    ARM_MATH_SUCCESS = 0,
    ARM_MATH_ARGUMENT_ERROR = -1,
    ARM_MATH_LENGTH_ERROR = -2,
    ARM_MATH_SIZE_MISMATCH = -3,
    ARM_MATH_NANINF = -4,
    ARM_MATH_SINGULAR = -5,
    ARM_MATH_TEST_FAILURE = -6
} arm_status;

typedef struct
{
    uint16_t numRows, numCols;
    float32_t *pData;
} arm_matrix_instance_f32;
#ifdef __cplusplus
extern "C"
{
#endif
    void arm_mat_init_f32(arm_matrix_instance_f32 *, uint16_t, uint16_t, float32_t *);
    arm_status arm_mat_add_f32(const arm_matrix_instance_f32 *, const arm_matrix_instance_f32 *,
                               arm_matrix_instance_f32 *);
    arm_status arm_mat_sub_f32(const arm_matrix_instance_f32 *, const arm_matrix_instance_f32 *,
                               arm_matrix_instance_f32 *);
    arm_status arm_mat_mult_f32(const arm_matrix_instance_f32 *, const arm_matrix_instance_f32 *,
                                arm_matrix_instance_f32 *);
    arm_status arm_mat_scale_f32(const arm_matrix_instance_f32 *, float32_t,
                                 arm_matrix_instance_f32 *);
    arm_status arm_mat_trans_f32(const arm_matrix_instance_f32 *, arm_matrix_instance_f32 *);
    arm_status arm_mat_inverse_f32(const arm_matrix_instance_f32 *, arm_matrix_instance_f32 *);
#ifdef __cplusplus
}
#endif
