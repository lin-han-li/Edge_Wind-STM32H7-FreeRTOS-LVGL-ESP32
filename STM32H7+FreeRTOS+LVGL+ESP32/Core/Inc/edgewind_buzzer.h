#ifndef EDGEWIND_BUZZER_H
#define EDGEWIND_BUZZER_H

#include <stdint.h>
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EDGEWIND_BUZZER_GPIO_Port GPIOH
#define EDGEWIND_BUZZER_Pin       GPIO_PIN_7

typedef enum
{
    EDGEWIND_BUZZER_EVT_UI_CLICK = 0,
    EDGEWIND_BUZZER_EVT_BOOT_READY,
    EDGEWIND_BUZZER_EVT_SUCCESS,
    EDGEWIND_BUZZER_EVT_ERROR,
    EDGEWIND_BUZZER_EVT_FAULT_E01,
    EDGEWIND_BUZZER_EVT_FAULT_E02,
    EDGEWIND_BUZZER_EVT_FAULT_E03,
    EDGEWIND_BUZZER_EVT_FAULT_E04,
    EDGEWIND_BUZZER_EVT_FAULT_E05,
    EDGEWIND_BUZZER_EVT_FAULT_E06,
    EDGEWIND_BUZZER_EVT_RECOVER,
    EDGEWIND_BUZZER_EVT_COUNT
} EdgeWind_BuzzerEvent_t;

void EdgeWind_Buzzer_GpioInit(void);
void EdgeWind_Buzzer_Init(void);
void EdgeWind_Buzzer_Task(void *argument);
void EdgeWind_Buzzer_Play(EdgeWind_BuzzerEvent_t event);
void EdgeWind_Buzzer_OnFaultCode(const char *fault_code);
void EdgeWind_Buzzer_SetMuted(uint8_t muted);
uint8_t EdgeWind_Buzzer_IsMuted(void);

#ifdef __cplusplus
}
#endif

#endif /* EDGEWIND_BUZZER_H */
