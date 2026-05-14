#include "edgewind_ai.h"

#include "edgewind_ai_preprocess_params.h"
#include "edgewind_units.h"
#include "network.h"
#include "network_data_params.h"
#include "stm32h7xx_hal.h"

#include "arm_math.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * SDRAM map:
 * 0xC0000000..0xC0232800 LCD framebuffer + LVGL display buffers
 * 0xC0400000..0xC0600000 LVGL heap
 * 0xC0600000..            AI activation arena
 * 0xC0680000..            ESP upload snapshots
 */
#define EDGEWIND_AI_ACTIVATION_ADDR       (0xC0600000UL)
#define EDGEWIND_UPLOAD_SNAPSHOT_ADDR     (0xC0680000UL)
#define EDGEWIND_AI_ACTIVATION_END        (EDGEWIND_AI_ACTIVATION_ADDR + AI_NETWORK_DATA_ACTIVATIONS_SIZE)

#define EDGEWIND_AI_SAMPLE_RATE_HZ        (25600U)
#define EDGEWIND_AI_WINDOW_POINTS         (AD_ACQ_POINTS)
#define EDGEWIND_AI_RFFT_BINS             ((EDGEWIND_AI_WINDOW_POINTS / 2U) + 1U)
#define EDGEWIND_AI_WAVELET_BLOCK_POINTS  (1024U)
#define EDGEWIND_AI_WAVELET_BLOCKS        (EDGEWIND_AI_WINDOW_POINTS / EDGEWIND_AI_WAVELET_BLOCK_POINTS)
#define EDGEWIND_AI_WAVELET_FEATURES_CH   (26U)
#define EDGEWIND_AI_EPS                   (1.0e-6f)
#define EDGEWIND_AI_ZERO_AB_MAX_V         (0.18f)
#define EDGEWIND_AI_ZERO_C_MAX_V          (0.16f)
#define EDGEWIND_AI_ZERO_D_MAX_V          (0.08f)
#define EDGEWIND_AI_ZERO_AB_MEAN_V        (0.075f)
#define EDGEWIND_AI_ZERO_CD_MEAN_V        (0.045f)

#ifndef AXI_SRAM_SECTION
#define AXI_SRAM_SECTION __attribute__((section(".axi_sram")))
#endif

#ifndef DMA_ALIGN32
#define DMA_ALIGN32 __attribute__((aligned(32)))
#endif

#if ((EDGEWIND_AI_ACTIVATION_ADDR % AI_NETWORK_ACTIVATIONS_ALIGNMENT) != 0U)
#error "AI activation buffer is not properly aligned"
#endif

#if (EDGEWIND_AI_ACTIVATION_END > EDGEWIND_UPLOAD_SNAPSHOT_ADDR)
#error "AI activation buffer overlaps ESP upload snapshots"
#endif

static const char * const s_class_codes[EDGEWIND_AI_CLASS_COUNT] =
{
    "E00", "E01", "E02", "E03", "E04", "E05", "E06"
};

static const char * const s_class_names[EDGEWIND_AI_CLASS_COUNT] =
{
    "normal",
    "ac_coupling",
    "insulation",
    "cap_aging",
    "igbt_fault",
    "bus_ground",
    "pwm_abnormal",
};

static const float s_db3_lo[6] =
{
    0.0352262919f, -0.0854412739f, -0.1350110200f,
    0.4598775021f, 0.8068915093f, 0.3326705530f
};

static const float s_db3_hi[6] =
{
    -0.3326705530f, 0.8068915093f, -0.4598775021f,
    -0.1350110200f, 0.0854412739f, 0.0352262919f
};

static const uint16_t s_feature_freq_bins[11] =
{
    8U, 16U, 24U, 48U, 160U, 320U, 640U, 960U, 1280U, 1600U, 1920U
};

static const uint16_t s_feature_band_ranges[5][2] =
{
    {0U, 32U},
    {32U, 160U},
    {160U, 640U},
    {640U, 1280U},
    {1280U, 2000U},
};

static ai_handle s_network = AI_HANDLE_NULL;
static ai_buffer *s_input = NULL;
static ai_buffer *s_output = NULL;
static uint8_t s_ai_ready = 0U;
static uint8_t s_rfft_ready = 0U;
static arm_rfft_fast_instance_f32 s_rfft;

static float s_fft_input[EDGEWIND_AI_WINDOW_POINTS] AXI_SRAM_SECTION DMA_ALIGN32;
static float s_fft_output[EDGEWIND_AI_WINDOW_POINTS] AXI_SRAM_SECTION DMA_ALIGN32;
static float s_dwt_a[EDGEWIND_AI_WAVELET_BLOCK_POINTS] AXI_SRAM_SECTION DMA_ALIGN32;
static float s_dwt_next[520] AXI_SRAM_SECTION DMA_ALIGN32;
static float s_dwt_detail[520] AXI_SRAM_SECTION DMA_ALIGN32;

static float EdgeWind_AI_SafeFloat(float value)
{
    if ((value != value) || (value > FLT_MAX) || (value < -FLT_MAX))
    {
        return 0.0f;
    }
    return value;
}

static float EdgeWind_AI_Normalize(float value, float mean, float scale)
{
    if ((scale < 1.0e-12f) && (scale > -1.0e-12f))
    {
        scale = 1.0f;
    }
    return EdgeWind_AI_SafeFloat((value - mean) / scale);
}

static float EdgeWind_AI_AnalogToTrainMv(float analog_v)
{
    return EW_AnalogVToAIInputMv(analog_v);
}

const char *EdgeWind_AI_ClassCode(uint8_t class_id)
{
    if (class_id >= EDGEWIND_AI_CLASS_COUNT)
    {
        class_id = 0U;
    }
    return s_class_codes[class_id];
}

const char *EdgeWind_AI_ClassName(uint8_t class_id)
{
    if (class_id >= EDGEWIND_AI_CLASS_COUNT)
    {
        class_id = 0U;
    }
    return s_class_names[class_id];
}

static uint8_t EdgeWind_AI_IsPowerOnZeroWindow(const float analog_v[4][AD_ACQ_POINTS])
{
    float max_abs[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float sum_abs[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    for (uint32_t i = 0U; i < AD_ACQ_POINTS; ++i)
    {
        for (uint32_t ch = 0U; ch < 4U; ++ch)
        {
            float v = fabsf(analog_v[ch][i]);
            if (v > max_abs[ch])
            {
                max_abs[ch] = v;
            }
            sum_abs[ch] += v;
        }
    }

    if ((max_abs[0] <= EDGEWIND_AI_ZERO_AB_MAX_V) &&
        (max_abs[1] <= EDGEWIND_AI_ZERO_AB_MAX_V) &&
        (max_abs[2] <= EDGEWIND_AI_ZERO_C_MAX_V) &&
        (max_abs[3] <= EDGEWIND_AI_ZERO_D_MAX_V) &&
        ((sum_abs[0] / (float)AD_ACQ_POINTS) <= EDGEWIND_AI_ZERO_AB_MEAN_V) &&
        ((sum_abs[1] / (float)AD_ACQ_POINTS) <= EDGEWIND_AI_ZERO_AB_MEAN_V) &&
        ((sum_abs[2] / (float)AD_ACQ_POINTS) <= EDGEWIND_AI_ZERO_CD_MEAN_V) &&
        ((sum_abs[3] / (float)AD_ACQ_POINTS) <= EDGEWIND_AI_ZERO_CD_MEAN_V))
    {
        return 1U;
    }
    return 0U;
}

static void EdgeWind_AI_SetNormalResult(EdgeWind_AI_Result_t *result, uint32_t elapsed_ms)
{
    memset(result, 0, sizeof(*result));
    result->class_id = 0U;
    result->confidence = 1.0f;
    result->probabilities[0] = 1.0f;
    result->total_ms = elapsed_ms;
    strncpy(result->fault_code, EdgeWind_AI_ClassCode(0U), sizeof(result->fault_code) - 1U);
    result->fault_code[sizeof(result->fault_code) - 1U] = '\0';
}

static void EdgeWind_AI_PrintError(const char *stage, ai_error err)
{
    printf("[AI] %s failed: type=0x%02X code=0x%02X\r\n", stage, err.type, err.code);
}

static void EdgeWind_AI_EnableCycleCounter(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

int EdgeWind_AI_Init(void)
{
    ai_handle activations[] = { (ai_handle)EDGEWIND_AI_ACTIVATION_ADDR };
    ai_error err;

    if (s_ai_ready != 0U)
    {
        return 0;
    }

    if (s_rfft_ready == 0U)
    {
        if (arm_rfft_fast_init_f32(&s_rfft, EDGEWIND_AI_WINDOW_POINTS) != ARM_MATH_SUCCESS)
        {
            printf("[AI] rfft init failed\r\n");
            return -1;
        }
        s_rfft_ready = 1U;
    }

    err = ai_network_create_and_init(&s_network, activations, NULL);
    if (err.type != AI_ERROR_NONE)
    {
        EdgeWind_AI_PrintError("create", err);
        return -2;
    }

    s_input = ai_network_inputs_get(s_network, NULL);
    s_output = ai_network_outputs_get(s_network, NULL);
    if ((s_input == NULL) || (s_output == NULL))
    {
        printf("[AI] get io buffers failed\r\n");
        return -3;
    }

    EdgeWind_AI_EnableCycleCounter();
    s_ai_ready = 1U;
    printf("[AI] runtime ready act=0x%08lX size=%lu input=dwt/feat/spec unit=train_mV\r\n",
           (unsigned long)EDGEWIND_AI_ACTIVATION_ADDR,
           (unsigned long)AI_NETWORK_DATA_ACTIVATIONS_SIZE);
    return 0;
}

static float EdgeWind_AI_FftMagAt(uint16_t bin)
{
    const float scale = 2.0f / (float)EDGEWIND_AI_WINDOW_POINTS;
    float re;
    float im;

    if (bin == 0U)
    {
        return fabsf(s_fft_output[0]) * scale;
    }
    if (bin >= (EDGEWIND_AI_WINDOW_POINTS / 2U))
    {
        return fabsf(s_fft_output[1]) * scale;
    }

    re = s_fft_output[(uint32_t)bin * 2U];
    im = s_fft_output[((uint32_t)bin * 2U) + 1U];
    return sqrtf((re * re) + (im * im)) * scale;
}

static void EdgeWind_AI_ExtractFeatureInput(const float analog_v[4][AD_ACQ_POINTS], float *feat_norm)
{
    float raw[EDGEWIND_AI_FEAT_SIZE];
    float mean[4];
    float rms[4];
    float ac_rms[4];
    float stdv[4];
    float minv[4];
    float maxv[4];
    float ptp[4];
    float crest[4];
    float slope_max[4];
    float pulse_count[4];
    float sat_count[4];
    float fft_freq[11][4];
    float band_power[5][4];
    uint32_t idx = 0U;

    for (uint32_t ch = 0U; ch < 4U; ++ch)
    {
        double sum = 0.0;
        double sum_sq = 0.0;
        float abs_max = 0.0f;
        float prev = EdgeWind_AI_AnalogToTrainMv(analog_v[ch][0]);
        minv[ch] = prev;
        maxv[ch] = prev;
        slope_max[ch] = 0.0f;
        pulse_count[ch] = 0.0f;
        sat_count[ch] = (fabsf(prev) >= 4900.0f) ? 1.0f : 0.0f;

        for (uint32_t i = 0U; i < EDGEWIND_AI_WINDOW_POINTS; ++i)
        {
            float x = EdgeWind_AI_AnalogToTrainMv(analog_v[ch][i]);
            float ax = fabsf(x);
            sum += (double)x;
            sum_sq += (double)x * (double)x;
            if (x < minv[ch]) minv[ch] = x;
            if (x > maxv[ch]) maxv[ch] = x;
            if (ax > abs_max) abs_max = ax;
            if (i > 0U)
            {
                float diff = fabsf(x - prev);
                if (diff > slope_max[ch]) slope_max[ch] = diff;
                if (diff > 250.0f) pulse_count[ch] += 1.0f;
            }
            prev = x;
            if ((i > 0U) && (ax >= 4900.0f))
            {
                sat_count[ch] += 1.0f;
            }
        }

        mean[ch] = (float)(sum / (double)EDGEWIND_AI_WINDOW_POINTS);
        rms[ch] = sqrtf((float)(sum_sq / (double)EDGEWIND_AI_WINDOW_POINTS) + EDGEWIND_AI_EPS);

        {
            double var_sum = 0.0;
            for (uint32_t i = 0U; i < EDGEWIND_AI_WINDOW_POINTS; ++i)
            {
                float centered = EdgeWind_AI_AnalogToTrainMv(analog_v[ch][i]) - mean[ch];
                var_sum += (double)centered * (double)centered;
            }
            {
                float var = (float)(var_sum / (double)EDGEWIND_AI_WINDOW_POINTS);
                if (var < 0.0f) var = 0.0f;
                stdv[ch] = sqrtf(var);
                ac_rms[ch] = sqrtf(var + EDGEWIND_AI_EPS);
            }
        }

        ptp[ch] = maxv[ch] - minv[ch];
        crest[ch] = abs_max / (rms[ch] + EDGEWIND_AI_EPS);
    }

    for (uint32_t ch = 0U; ch < 4U; ++ch)
    {
        for (uint32_t i = 0U; i < EDGEWIND_AI_WINDOW_POINTS; ++i)
        {
            s_fft_input[i] = EdgeWind_AI_AnalogToTrainMv(analog_v[ch][i]) - mean[ch];
        }
        arm_rfft_fast_f32(&s_rfft, s_fft_input, s_fft_output, 0);

        for (uint32_t f = 0U; f < 11U; ++f)
        {
            fft_freq[f][ch] = EdgeWind_AI_FftMagAt(s_feature_freq_bins[f]);
        }

        for (uint32_t b = 0U; b < 5U; ++b)
        {
            uint16_t lo = s_feature_band_ranges[b][0];
            uint16_t hi = s_feature_band_ranges[b][1];
            double sum_power = 0.0;
            uint32_t count = 0U;
            for (uint16_t k = lo; k <= hi; ++k)
            {
                float mag = EdgeWind_AI_FftMagAt(k);
                sum_power += (double)mag * (double)mag;
                count++;
            }
            band_power[b][ch] = (count > 0U) ? (float)(sum_power / (double)count) : 0.0f;
        }
    }

    for (uint32_t ch = 0U; ch < 4U; ++ch) raw[idx++] = mean[ch];
    for (uint32_t ch = 0U; ch < 4U; ++ch) raw[idx++] = rms[ch];
    for (uint32_t ch = 0U; ch < 4U; ++ch) raw[idx++] = ac_rms[ch];
    for (uint32_t ch = 0U; ch < 4U; ++ch) raw[idx++] = stdv[ch];
    for (uint32_t ch = 0U; ch < 4U; ++ch) raw[idx++] = minv[ch];
    for (uint32_t ch = 0U; ch < 4U; ++ch) raw[idx++] = maxv[ch];
    for (uint32_t ch = 0U; ch < 4U; ++ch) raw[idx++] = ptp[ch];
    for (uint32_t ch = 0U; ch < 4U; ++ch) raw[idx++] = crest[ch];
    for (uint32_t ch = 0U; ch < 4U; ++ch) raw[idx++] = slope_max[ch];
    for (uint32_t ch = 0U; ch < 4U; ++ch) raw[idx++] = pulse_count[ch];
    for (uint32_t ch = 0U; ch < 4U; ++ch) raw[idx++] = sat_count[ch];
    for (uint32_t f = 0U; f < 11U; ++f)
    {
        for (uint32_t ch = 0U; ch < 4U; ++ch) raw[idx++] = fft_freq[f][ch];
    }
    for (uint32_t b = 0U; b < 5U; ++b)
    {
        for (uint32_t ch = 0U; ch < 4U; ++ch) raw[idx++] = band_power[b][ch];
    }

    {
        double ab_common_sum = 0.0;
        double ab_common_sq = 0.0;
        double ab_diff_sq = 0.0;
        double cd_num = 0.0;
        double ad_num = 0.0;
        double bd_num = 0.0;
        double a_den = 0.0;
        double b_den = 0.0;
        double c_den = 0.0;
        double d_den = 0.0;

        for (uint32_t i = 0U; i < EDGEWIND_AI_WINDOW_POINTS; ++i)
        {
            float a = EdgeWind_AI_AnalogToTrainMv(analog_v[0][i]);
            float b = EdgeWind_AI_AnalogToTrainMv(analog_v[1][i]);
            float c = EdgeWind_AI_AnalogToTrainMv(analog_v[2][i]);
            float d = EdgeWind_AI_AnalogToTrainMv(analog_v[3][i]);
            float ac = a - mean[0];
            float bc = b - mean[1];
            float cc = c - mean[2];
            float dc = d - mean[3];
            float ab_common = a + b;
            float ab_diff = a - b;

            ab_common_sum += (double)ab_common;
            ab_common_sq += (double)ab_common * (double)ab_common;
            ab_diff_sq += (double)ab_diff * (double)ab_diff;
            cd_num += (double)cc * (double)dc;
            ad_num += (double)ac * (double)dc;
            bd_num += (double)bc * (double)dc;
            a_den += (double)ac * (double)ac;
            b_den += (double)bc * (double)bc;
            c_den += (double)cc * (double)cc;
            d_den += (double)dc * (double)dc;
        }

        raw[idx++] = (float)(ab_common_sum / (double)EDGEWIND_AI_WINDOW_POINTS);
        raw[idx++] = sqrtf((float)(ab_common_sq / (double)EDGEWIND_AI_WINDOW_POINTS) + EDGEWIND_AI_EPS);
        raw[idx++] = sqrtf((float)(ab_diff_sq / (double)EDGEWIND_AI_WINDOW_POINTS) + EDGEWIND_AI_EPS);
        raw[idx++] = (float)(cd_num / (sqrt(c_den * d_den) + 1.0e-6));
        raw[idx++] = (float)(ad_num / (sqrt(a_den * d_den) + 1.0e-6));
        raw[idx++] = (float)(bd_num / (sqrt(b_den * d_den) + 1.0e-6));
        raw[idx++] = maxv[3] / (((mean[3] > 1.0f) ? mean[3] : 1.0f));
        raw[idx++] = maxv[2] / (rms[2] + EDGEWIND_AI_EPS);
    }

    for (uint32_t i = 0U; i < EDGEWIND_AI_FEAT_SIZE; ++i)
    {
        feat_norm[i] = EdgeWind_AI_Normalize(raw[i], g_edgewind_ai_feat_mean[i], g_edgewind_ai_feat_scale[i]);
    }
}

static float EdgeWind_AI_DwtRead(const float *x, uint32_t length, int32_t p)
{
    const int32_t filt_margin = 5;

    if ((p <= -1) && (p >= -filt_margin))
    {
        return x[(uint32_t)(-p - 1)];
    }
    if ((p >= (int32_t)length) && (p <= ((int32_t)length + filt_margin - 1)))
    {
        return x[(uint32_t)(((int32_t)length * 2) - p - 1)];
    }
    if ((p >= 0) && (p < (int32_t)length))
    {
        return x[(uint32_t)p];
    }
    return 0.0f;
}

static uint32_t EdgeWind_AI_DwtStep(const float *x, uint32_t length, float *approx, float *detail)
{
    uint32_t dec_len = (length + 5U) >> 1U;

    for (uint32_t n = 0U; n < dec_len; ++n)
    {
        float a = 0.0f;
        float d = 0.0f;
        for (uint32_t k = 0U; k < 6U; ++k)
        {
            int32_t p = (int32_t)(2U * n) - (int32_t)k + 1;
            float sample = EdgeWind_AI_DwtRead(x, length, p);
            a += sample * s_db3_lo[k];
            d += sample * s_db3_hi[k];
        }
        approx[n] = a;
        detail[n] = d;
    }

    return dec_len;
}

static void EdgeWind_AI_DwtStats(const float *x, uint32_t length, float *log_energy, float *max_abs, float *crest)
{
    double sum_sq = 0.0;
    float max_v = 0.0f;

    for (uint32_t i = 0U; i < length; ++i)
    {
        float ax = fabsf(x[i]);
        sum_sq += (double)x[i] * (double)x[i];
        if (ax > max_v)
        {
            max_v = ax;
        }
    }

    {
        float mean_sq = (length > 0U) ? (float)(sum_sq / (double)length) : 0.0f;
        float rms = sqrtf(mean_sq + EDGEWIND_AI_EPS);
        *log_energy = logf(1.0f + mean_sq);
        *max_abs = max_v;
        *crest = max_v / (rms + EDGEWIND_AI_EPS);
    }
}

static void EdgeWind_AI_ExtractDwtInput(const float analog_v[4][AD_ACQ_POINTS], float *dwt_norm)
{
    for (uint32_t ch = 0U; ch < 4U; ++ch)
    {
        float energy_sum[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        float energy_max[5] = {-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX};
        float max_abs_sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float max_abs_max[4] = {-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX};
        float crest_sum[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        float crest_max[4] = {-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX};
        float raw[EDGEWIND_AI_WAVELET_FEATURES_CH];
        uint32_t raw_idx = 0U;

        for (uint32_t block = 0U; block < EDGEWIND_AI_WAVELET_BLOCKS; ++block)
        {
            uint32_t length = EDGEWIND_AI_WAVELET_BLOCK_POINTS;
            float detail_energy[4];
            float detail_max_abs[4];
            float detail_crest[4];
            float approx_energy;
            float unused_max_abs;
            float unused_crest;

            for (uint32_t i = 0U; i < EDGEWIND_AI_WAVELET_BLOCK_POINTS; ++i)
            {
                uint32_t src_idx = (block * EDGEWIND_AI_WAVELET_BLOCK_POINTS) + i;
                s_dwt_a[i] = EdgeWind_AI_AnalogToTrainMv(analog_v[ch][src_idx]);
            }

            for (uint32_t level = 0U; level < 4U; ++level)
            {
                uint32_t dec_len = EdgeWind_AI_DwtStep(s_dwt_a, length, s_dwt_next, s_dwt_detail);
                EdgeWind_AI_DwtStats(s_dwt_detail, dec_len,
                                     &detail_energy[level],
                                     &detail_max_abs[level],
                                     &detail_crest[level]);
                memcpy(s_dwt_a, s_dwt_next, dec_len * sizeof(float));
                length = dec_len;
            }

            EdgeWind_AI_DwtStats(s_dwt_a, length, &approx_energy, &unused_max_abs, &unused_crest);

            for (uint32_t level = 0U; level < 4U; ++level)
            {
                energy_sum[level] += detail_energy[level];
                if (detail_energy[level] > energy_max[level]) energy_max[level] = detail_energy[level];
                max_abs_sum[level] += detail_max_abs[level];
                if (detail_max_abs[level] > max_abs_max[level]) max_abs_max[level] = detail_max_abs[level];
                crest_sum[level] += detail_crest[level];
                if (detail_crest[level] > crest_max[level]) crest_max[level] = detail_crest[level];
            }
            energy_sum[4] += approx_energy;
            if (approx_energy > energy_max[4]) energy_max[4] = approx_energy;
        }

        for (uint32_t i = 0U; i < 5U; ++i) raw[raw_idx++] = energy_sum[i] / (float)EDGEWIND_AI_WAVELET_BLOCKS;
        for (uint32_t i = 0U; i < 5U; ++i) raw[raw_idx++] = energy_max[i];
        for (uint32_t i = 0U; i < 4U; ++i) raw[raw_idx++] = max_abs_sum[i] / (float)EDGEWIND_AI_WAVELET_BLOCKS;
        for (uint32_t i = 0U; i < 4U; ++i) raw[raw_idx++] = max_abs_max[i];
        for (uint32_t i = 0U; i < 4U; ++i) raw[raw_idx++] = crest_sum[i] / (float)EDGEWIND_AI_WAVELET_BLOCKS;
        for (uint32_t i = 0U; i < 4U; ++i) raw[raw_idx++] = crest_max[i];

        for (uint32_t i = 0U; i < EDGEWIND_AI_WAVELET_FEATURES_CH; ++i)
        {
            uint32_t out_idx = (ch * EDGEWIND_AI_WAVELET_FEATURES_CH) + i;
            dwt_norm[out_idx] = EdgeWind_AI_Normalize(raw[i],
                                                      g_edgewind_ai_dwt_mean[out_idx],
                                                      g_edgewind_ai_dwt_scale[out_idx]);
        }
    }
}

static void EdgeWind_AI_ExtractSpecInput(const float analog_v[4][AD_ACQ_POINTS], float *spec_norm)
{
    for (uint32_t ch = 0U; ch < 4U; ++ch)
    {
        double sum = 0.0;
        float mean;

        for (uint32_t i = 0U; i < EDGEWIND_AI_WINDOW_POINTS; ++i)
        {
            sum += (double)EdgeWind_AI_AnalogToTrainMv(analog_v[ch][i]);
        }
        mean = (float)(sum / (double)EDGEWIND_AI_WINDOW_POINTS);

        for (uint32_t i = 0U; i < EDGEWIND_AI_WINDOW_POINTS; ++i)
        {
            s_fft_input[i] = EdgeWind_AI_AnalogToTrainMv(analog_v[ch][i]) - mean;
        }
        arm_rfft_fast_f32(&s_rfft, s_fft_input, s_fft_output, 0);

        for (uint32_t bin = 0U; bin < EDGEWIND_AI_SPEC_BINS; ++bin)
        {
            uint32_t lo = (bin * EDGEWIND_AI_RFFT_BINS) / EDGEWIND_AI_SPEC_BINS;
            uint32_t hi = ((bin + 1U) * EDGEWIND_AI_RFFT_BINS) / EDGEWIND_AI_SPEC_BINS;
            double log_sum = 0.0;
            uint32_t count = 0U;
            uint32_t spec_idx;
            float raw;

            if (hi <= lo)
            {
                hi = lo + 1U;
            }
            if (hi > EDGEWIND_AI_RFFT_BINS)
            {
                hi = EDGEWIND_AI_RFFT_BINS;
            }

            for (uint32_t k = lo; k < hi; ++k)
            {
                log_sum += (double)logf(1.0f + EdgeWind_AI_FftMagAt((uint16_t)k));
                count++;
            }

            raw = (count > 0U) ? (float)(log_sum / (double)count) : 0.0f;
            spec_idx = (bin * 4U) + ch;
            spec_norm[spec_idx] = EdgeWind_AI_Normalize(raw,
                                                        g_edgewind_ai_spec_mean[spec_idx],
                                                        g_edgewind_ai_spec_scale[spec_idx]);
        }
    }
}

int EdgeWind_AI_RunOnAnalogWindow(const float analog_v[4][AD_ACQ_POINTS], EdgeWind_AI_Result_t *result)
{
    ai_i32 batch;
    const float *prob;
    float *input_dwt;
    float *input_feat;
    float *input_spec;
    uint32_t total_start;
    uint32_t feature_start;
    uint32_t inference_start;
    uint8_t best_id = 0U;
    float best_conf = -FLT_MAX;

    if ((analog_v == NULL) || (result == NULL))
    {
        return -1;
    }

    total_start = HAL_GetTick();
    if (EdgeWind_AI_IsPowerOnZeroWindow(analog_v) != 0U)
    {
        EdgeWind_AI_SetNormalResult(result, HAL_GetTick() - total_start);
        return 0;
    }

    if (EdgeWind_AI_Init() != 0)
    {
        return -2;
    }

    memset(result, 0, sizeof(*result));
    total_start = HAL_GetTick();
    feature_start = total_start;

    input_dwt = (float *)s_input[0].data;
    input_feat = (float *)s_input[1].data;
    input_spec = (float *)s_input[2].data;

    EdgeWind_AI_ExtractDwtInput(analog_v, input_dwt);
    EdgeWind_AI_ExtractFeatureInput(analog_v, input_feat);
    EdgeWind_AI_ExtractSpecInput(analog_v, input_spec);
    result->feature_ms = HAL_GetTick() - feature_start;

    inference_start = HAL_GetTick();
    batch = ai_network_run(s_network, s_input, s_output);
    result->inference_ms = HAL_GetTick() - inference_start;
    result->total_ms = HAL_GetTick() - total_start;

    if (batch != 1)
    {
        EdgeWind_AI_PrintError("run", ai_network_get_error(s_network));
        return -3;
    }

    prob = (const float *)s_output[0].data;
    for (uint32_t i = 0U; i < EDGEWIND_AI_CLASS_COUNT; ++i)
    {
        float p = EdgeWind_AI_SafeFloat(prob[i]);
        result->probabilities[i] = p;
        if (p > best_conf)
        {
            best_conf = p;
            best_id = (uint8_t)i;
        }
    }

    result->class_id = best_id;
    result->confidence = best_conf;
    strncpy(result->fault_code, EdgeWind_AI_ClassCode(best_id), sizeof(result->fault_code) - 1U);
    result->fault_code[sizeof(result->fault_code) - 1U] = '\0';
    return 0;
}
