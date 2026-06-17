#ifndef AD_ACQ_BUFFERS_H
#define AD_ACQ_BUFFERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 采样双缓冲（4通道×4096点）与填充状态。
 * 说明：为尽量少改动现有 FFT/UI/上报链路，继续沿用历史变量名。 */

#ifndef AD_ACQ_POINTS
#define AD_ACQ_POINTS 4096
#endif

#ifndef AD_AUX4_COUNT
#define AD_AUX4_COUNT 4U
#endif

/* ADSA_B/ADSA_B2 store AD7606 analog input voltage in V. Convert at consumers. */
extern float ADSA_B[4][AD_ACQ_POINTS];
extern float ADSA_B2[4][AD_ACQ_POINTS];
extern float ADSA_AUX4[AD_AUX4_COUNT];
extern float ADSA_AUX4_2[AD_AUX4_COUNT];
extern float ADS131A04_Buf[4];

extern volatile uint16_t AD7606_DebugRaw[8];
extern volatile float AD7606_DebugVolts[8];
extern volatile float AD7606_Aux4Values[AD_AUX4_COUNT];
extern volatile uint8_t AD7606_Aux4ValidMask;
extern volatile uint8_t AD7606_DebugBusy;
extern volatile uint8_t AD7606_DebugDb7;

extern uint8_t ADSA_AUX4_valid_mask;
extern uint8_t ADSA_AUX4_2_valid_mask;

extern int ADS131A04_flag;
extern int ADS131A04_flag2;
extern int number;
extern int number2;

#ifdef __cplusplus
}
#endif

#endif /* AD_ACQ_BUFFERS_H */
