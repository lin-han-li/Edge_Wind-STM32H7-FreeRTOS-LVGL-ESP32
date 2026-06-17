#ifndef EDGEWIND_AI_H
#define EDGEWIND_AI_H

#include "ad_acq_buffers.h"
#include "edgewind_ai_preprocess_params.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EDGEWIND_AI_CLASS_COUNT       (7U)
#define EDGEWIND_AI_CONFIDENCE_MIN    (0.80f)

#if (EDGEWIND_AI_AUX_SIZE != AD_AUX4_COUNT)
#error "EdgeWind aux4 size must match the AD7606 ch4..ch7 window count"
#endif

typedef struct
{
    uint8_t class_id;
    char fault_code[4];
    float confidence;
    float probabilities[EDGEWIND_AI_CLASS_COUNT];
    float aux4[EDGEWIND_AI_AUX_SIZE];
    uint8_t aux4_valid_mask;
    uint32_t feature_ms;
    uint32_t inference_ms;
    uint32_t total_ms;
} EdgeWind_AI_Result_t;

int EdgeWind_AI_Init(void);
int EdgeWind_AI_RunOnAnalogWindow(const float analog_v[4][AD_ACQ_POINTS],
                                  const float aux4[EDGEWIND_AI_AUX_SIZE],
                                  uint8_t aux4_valid_mask,
                                  EdgeWind_AI_Result_t *result);
int EdgeWind_AI_DebugExtractInputs(const float analog_v[4][AD_ACQ_POINTS],
                                   const float aux4[EDGEWIND_AI_AUX_SIZE],
                                   float *dwt_norm,
                                   float *feat_norm,
                                   float *rawlite_norm,
                                   float *spec_norm,
                                   float *aux_norm);
int EdgeWind_AI_DebugRunNormalizedInputs(const float *dwt_norm,
                                         const float *feat_norm,
                                         const float *rawlite_norm,
                                         const float *spec_norm,
                                         const float *aux_norm,
                                         EdgeWind_AI_Result_t *result);
const char *EdgeWind_AI_ClassCode(uint8_t class_id);
const char *EdgeWind_AI_ClassName(uint8_t class_id);
const char *EdgeWind_AI_ModelVersion(void);
uint8_t EdgeWind_AI_UsesAuxInput(void);

#ifdef __cplusplus
}
#endif

#endif /* EDGEWIND_AI_H */
