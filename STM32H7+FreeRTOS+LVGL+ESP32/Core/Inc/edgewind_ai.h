#ifndef EDGEWIND_AI_H
#define EDGEWIND_AI_H

#include "ad_acq_buffers.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EDGEWIND_AI_CLASS_COUNT       (7U)
#define EDGEWIND_AI_CONFIDENCE_MIN    (0.80f)

typedef struct
{
    uint8_t class_id;
    char fault_code[4];
    float confidence;
    float probabilities[EDGEWIND_AI_CLASS_COUNT];
    uint32_t feature_ms;
    uint32_t inference_ms;
    uint32_t total_ms;
} EdgeWind_AI_Result_t;

int EdgeWind_AI_Init(void);
int EdgeWind_AI_RunOnAnalogWindow(const float analog_v[4][AD_ACQ_POINTS], EdgeWind_AI_Result_t *result);
const char *EdgeWind_AI_ClassCode(uint8_t class_id);
const char *EdgeWind_AI_ClassName(uint8_t class_id);

#ifdef __cplusplus
}
#endif

#endif /* EDGEWIND_AI_H */
