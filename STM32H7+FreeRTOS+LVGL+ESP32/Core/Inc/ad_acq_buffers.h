#ifndef AD_ACQ_BUFFERS_H
#define AD_ACQ_BUFFERS_H

#ifdef __cplusplus
extern "C" {
#endif

/* 采样双缓冲（4通道×4096点）与填充状态。
 * 说明：为尽量少改动现有 FFT/UI/上报链路，继续沿用历史变量名。 */

#ifndef AD_ACQ_POINTS
#define AD_ACQ_POINTS 4096
#endif

extern float ADSA_B[4][AD_ACQ_POINTS];
extern float ADSA_B2[4][AD_ACQ_POINTS];
extern float ADS131A04_Buf[4];

extern int ADS131A04_flag;
extern int ADS131A04_flag2;
extern int number;
extern int number2;

#ifdef __cplusplus
}
#endif

#endif /* AD_ACQ_BUFFERS_H */

