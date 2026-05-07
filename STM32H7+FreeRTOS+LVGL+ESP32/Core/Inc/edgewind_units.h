#ifndef EDGEWIND_UNITS_H
#define EDGEWIND_UNITS_H

#include <stdint.h>

/*
 * Unit contract:
 * - AD7606_RawToVoltsF() returns the low-voltage ADC input in analog volts.
 * - AI preprocessing uses the training-domain analog equivalent in mV.
 * - Cloud/UI reporting uses engineering physical units per channel.
 */

#define EW_BUS_ANALOG_TO_PHYS_V        (100.0f)
#define EW_AI_ANALOG_V_TO_TRAIN_MV     (1000.0f)

/* Pending sensor calibration confirmation. Defaults keep 1 V analog = 100 units. */
#define EW_CH2_ANALOG_TO_PHYS_A        (100.0f)
#define EW_CH3_ANALOG_TO_PHYS_MA       (100.0f)

/*
 * Full-frame binary transport scale for engineering quantities.
 * +/-500 V/A/mA becomes +/-5000 counts, safely inside int16_t.
 */
#define EW_UPLOAD_ENGINEERING_VALUE_SCALE (10.0f)
#define EW_UPLOAD_WAVEFORM_SCALE          (10.0f)
#define EW_UPLOAD_FFT_SCALE               (10.0f)

static inline float EW_AnalogVToAIInputMv(float adc_voltage_v)
{
    return adc_voltage_v * EW_AI_ANALOG_V_TO_TRAIN_MV;
}

static inline float EW_AnalogVToPhysicalEngineering(uint8_t channel_id, float adc_voltage_v)
{
    switch (channel_id)
    {
    case 0U:
    case 1U:
        return adc_voltage_v * EW_BUS_ANALOG_TO_PHYS_V;
    case 2U:
        return adc_voltage_v * EW_CH2_ANALOG_TO_PHYS_A;
    case 3U:
        return adc_voltage_v * EW_CH3_ANALOG_TO_PHYS_MA;
    default:
        return adc_voltage_v;
    }
}

#endif /* EDGEWIND_UNITS_H */
