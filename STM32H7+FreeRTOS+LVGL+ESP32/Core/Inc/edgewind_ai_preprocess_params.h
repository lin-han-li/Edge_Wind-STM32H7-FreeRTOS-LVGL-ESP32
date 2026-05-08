#ifndef EDGEWIND_AI_PREPROCESS_PARAMS_H
#define EDGEWIND_AI_PREPROCESS_PARAMS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EDGEWIND_AI_FEAT_SIZE (116U)
#define EDGEWIND_AI_DWT_SIZE  (104U)
#define EDGEWIND_AI_SPEC_BINS (512U)
#define EDGEWIND_AI_CHANNELS  (4U)
#define EDGEWIND_AI_SPEC_SIZE (EDGEWIND_AI_SPEC_BINS * EDGEWIND_AI_CHANNELS)

extern const float g_edgewind_ai_feat_mean[EDGEWIND_AI_FEAT_SIZE];
extern const float g_edgewind_ai_feat_scale[EDGEWIND_AI_FEAT_SIZE];
extern const float g_edgewind_ai_dwt_mean[EDGEWIND_AI_DWT_SIZE];
extern const float g_edgewind_ai_dwt_scale[EDGEWIND_AI_DWT_SIZE];
extern const float g_edgewind_ai_spec_mean[EDGEWIND_AI_SPEC_SIZE];
extern const float g_edgewind_ai_spec_scale[EDGEWIND_AI_SPEC_SIZE];

#ifdef __cplusplus
}
#endif

#endif /* EDGEWIND_AI_PREPROCESS_PARAMS_H */
