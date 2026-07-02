#include "edgewind_buzzer.h"

#include "cmsis_os.h"
#include "stm32h7xx_hal.h"
#include <string.h>

#ifndef EDGEWIND_BUZZER_QUEUE_DEPTH
#define EDGEWIND_BUZZER_QUEUE_DEPTH 12U
#endif

#ifndef EDGEWIND_BUZZER_REPEAT_LIMIT_MS
#define EDGEWIND_BUZZER_REPEAT_LIMIT_MS 15000U
#endif

#ifndef EDGEWIND_BUZZER_UI_CLICK_DEBOUNCE_MS
#define EDGEWIND_BUZZER_UI_CLICK_DEBOUNCE_MS 80U
#endif

#define EDGEWIND_BUZZER_ON_STATE  GPIO_PIN_RESET
#define EDGEWIND_BUZZER_OFF_STATE GPIO_PIN_SET

typedef struct
{
    uint16_t on_ms;
    uint16_t off_ms;
} EdgeWind_BuzzerSegment_t;

static osMessageQueueId_t s_buzzer_queue;
static uint8_t s_buzzer_muted;
static uint32_t s_last_ui_click_tick;

static void EdgeWind_Buzzer_SetOutput(uint8_t on)
{
    HAL_GPIO_WritePin(EDGEWIND_BUZZER_GPIO_Port,
                      EDGEWIND_BUZZER_Pin,
                      on ? EDGEWIND_BUZZER_ON_STATE : EDGEWIND_BUZZER_OFF_STATE);
}

void EdgeWind_Buzzer_GpioInit(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    HAL_GPIO_WritePin(EDGEWIND_BUZZER_GPIO_Port, EDGEWIND_BUZZER_Pin, EDGEWIND_BUZZER_OFF_STATE);

    GPIO_InitStruct.Pin = EDGEWIND_BUZZER_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(EDGEWIND_BUZZER_GPIO_Port, &GPIO_InitStruct);

    HAL_GPIO_WritePin(EDGEWIND_BUZZER_GPIO_Port, EDGEWIND_BUZZER_Pin, EDGEWIND_BUZZER_OFF_STATE);
}

void EdgeWind_Buzzer_Init(void)
{
    EdgeWind_Buzzer_SetOutput(0U);

    if (s_buzzer_queue == NULL)
    {
        s_buzzer_queue = osMessageQueueNew(EDGEWIND_BUZZER_QUEUE_DEPTH,
                                           sizeof(EdgeWind_BuzzerEvent_t),
                                           NULL);
    }
}

void EdgeWind_Buzzer_SetMuted(uint8_t muted)
{
    s_buzzer_muted = muted ? 1U : 0U;
    if (s_buzzer_muted != 0U)
    {
        EdgeWind_Buzzer_SetOutput(0U);
    }
}

uint8_t EdgeWind_Buzzer_IsMuted(void)
{
    return s_buzzer_muted;
}

static uint8_t EdgeWind_Buzzer_EventPriority(EdgeWind_BuzzerEvent_t event)
{
    switch (event)
    {
    case EDGEWIND_BUZZER_EVT_UI_CLICK:
        return 0U;
    case EDGEWIND_BUZZER_EVT_BOOT_READY:
    case EDGEWIND_BUZZER_EVT_SUCCESS:
    case EDGEWIND_BUZZER_EVT_RECOVER:
        return 1U;
    case EDGEWIND_BUZZER_EVT_ERROR:
        return 2U;
    default:
        return 3U;
    }
}

void EdgeWind_Buzzer_Play(EdgeWind_BuzzerEvent_t event)
{
    if (event >= EDGEWIND_BUZZER_EVT_COUNT)
    {
        return;
    }
    if (s_buzzer_muted != 0U)
    {
        return;
    }
    if (s_buzzer_queue == NULL)
    {
        return;
    }
    if (event == EDGEWIND_BUZZER_EVT_UI_CLICK)
    {
        uint32_t now = HAL_GetTick();
        if ((s_last_ui_click_tick != 0U) &&
            ((uint32_t)(now - s_last_ui_click_tick) < EDGEWIND_BUZZER_UI_CLICK_DEBOUNCE_MS))
        {
            return;
        }
        s_last_ui_click_tick = now;
    }

    osStatus_t status = osMessageQueuePut(s_buzzer_queue, &event,
                                          EdgeWind_Buzzer_EventPriority(event),
                                          0U);
    if ((status != osOK) && (EdgeWind_Buzzer_EventPriority(event) >= 2U))
    {
        (void)osMessageQueueReset(s_buzzer_queue);
        (void)osMessageQueuePut(s_buzzer_queue, &event,
                                EdgeWind_Buzzer_EventPriority(event),
                                0U);
    }
}

static void EdgeWind_Buzzer_PlaySegments(const EdgeWind_BuzzerSegment_t *segments,
                                         uint8_t count)
{
    for (uint8_t i = 0U; i < count; i++)
    {
        if (s_buzzer_muted != 0U)
        {
            break;
        }

        if (segments[i].on_ms > 0U)
        {
            EdgeWind_Buzzer_SetOutput(1U);
            osDelay(segments[i].on_ms);
        }

        EdgeWind_Buzzer_SetOutput(0U);
        if (segments[i].off_ms > 0U)
        {
            osDelay(segments[i].off_ms);
        }
    }

    EdgeWind_Buzzer_SetOutput(0U);
}

static void EdgeWind_Buzzer_PlayPattern(EdgeWind_BuzzerEvent_t event)
{
    static const EdgeWind_BuzzerSegment_t ui_click[] = {{30U, 0U}};
    static const EdgeWind_BuzzerSegment_t boot_ready[] = {{60U, 80U}, {60U, 0U}};
    static const EdgeWind_BuzzerSegment_t success[] = {{40U, 60U}, {40U, 0U}};
    static const EdgeWind_BuzzerSegment_t error[] = {{180U, 0U}};
    static const EdgeWind_BuzzerSegment_t fault_e01[] = {{80U, 80U}, {80U, 80U}, {80U, 0U}};
    static const EdgeWind_BuzzerSegment_t fault_e02[] = {{180U, 120U}, {180U, 0U}};
    static const EdgeWind_BuzzerSegment_t fault_e03[] = {{120U, 120U}, {120U, 0U}};
    static const EdgeWind_BuzzerSegment_t fault_e04[] = {{80U, 80U}, {80U, 80U}, {80U, 80U}, {80U, 0U}};
    static const EdgeWind_BuzzerSegment_t fault_e05[] = {{120U, 100U}, {120U, 100U}, {120U, 0U}};
    static const EdgeWind_BuzzerSegment_t fault_e06[] = {{80U, 70U}, {80U, 120U}, {220U, 0U}};
    static const EdgeWind_BuzzerSegment_t recover[] = {{40U, 80U}, {40U, 0U}};

    switch (event)
    {
    case EDGEWIND_BUZZER_EVT_UI_CLICK:
        EdgeWind_Buzzer_PlaySegments(ui_click, (uint8_t)(sizeof(ui_click) / sizeof(ui_click[0])));
        break;
    case EDGEWIND_BUZZER_EVT_BOOT_READY:
        EdgeWind_Buzzer_PlaySegments(boot_ready, (uint8_t)(sizeof(boot_ready) / sizeof(boot_ready[0])));
        break;
    case EDGEWIND_BUZZER_EVT_SUCCESS:
        EdgeWind_Buzzer_PlaySegments(success, (uint8_t)(sizeof(success) / sizeof(success[0])));
        break;
    case EDGEWIND_BUZZER_EVT_ERROR:
        EdgeWind_Buzzer_PlaySegments(error, (uint8_t)(sizeof(error) / sizeof(error[0])));
        break;
    case EDGEWIND_BUZZER_EVT_FAULT_E01:
        EdgeWind_Buzzer_PlaySegments(fault_e01, (uint8_t)(sizeof(fault_e01) / sizeof(fault_e01[0])));
        break;
    case EDGEWIND_BUZZER_EVT_FAULT_E02:
        EdgeWind_Buzzer_PlaySegments(fault_e02, (uint8_t)(sizeof(fault_e02) / sizeof(fault_e02[0])));
        break;
    case EDGEWIND_BUZZER_EVT_FAULT_E03:
        EdgeWind_Buzzer_PlaySegments(fault_e03, (uint8_t)(sizeof(fault_e03) / sizeof(fault_e03[0])));
        break;
    case EDGEWIND_BUZZER_EVT_FAULT_E04:
        EdgeWind_Buzzer_PlaySegments(fault_e04, (uint8_t)(sizeof(fault_e04) / sizeof(fault_e04[0])));
        break;
    case EDGEWIND_BUZZER_EVT_FAULT_E05:
        EdgeWind_Buzzer_PlaySegments(fault_e05, (uint8_t)(sizeof(fault_e05) / sizeof(fault_e05[0])));
        break;
    case EDGEWIND_BUZZER_EVT_FAULT_E06:
        EdgeWind_Buzzer_PlaySegments(fault_e06, (uint8_t)(sizeof(fault_e06) / sizeof(fault_e06[0])));
        break;
    case EDGEWIND_BUZZER_EVT_RECOVER:
        EdgeWind_Buzzer_PlaySegments(recover, (uint8_t)(sizeof(recover) / sizeof(recover[0])));
        break;
    default:
        break;
    }
}

static EdgeWind_BuzzerEvent_t EdgeWind_Buzzer_EventForFault(const char *fault_code)
{
    if (fault_code == NULL)
    {
        return EDGEWIND_BUZZER_EVT_ERROR;
    }
    if (strncmp(fault_code, "E01", 3U) == 0)
    {
        return EDGEWIND_BUZZER_EVT_FAULT_E01;
    }
    if (strncmp(fault_code, "E02", 3U) == 0)
    {
        return EDGEWIND_BUZZER_EVT_FAULT_E02;
    }
    if (strncmp(fault_code, "E03", 3U) == 0)
    {
        return EDGEWIND_BUZZER_EVT_FAULT_E03;
    }
    if (strncmp(fault_code, "E04", 3U) == 0)
    {
        return EDGEWIND_BUZZER_EVT_FAULT_E04;
    }
    if (strncmp(fault_code, "E05", 3U) == 0)
    {
        return EDGEWIND_BUZZER_EVT_FAULT_E05;
    }
    if (strncmp(fault_code, "E06", 3U) == 0)
    {
        return EDGEWIND_BUZZER_EVT_FAULT_E06;
    }
    return EDGEWIND_BUZZER_EVT_ERROR;
}

void EdgeWind_Buzzer_OnFaultCode(const char *fault_code)
{
    static char s_active_fault[4] = "E00";
    static uint8_t s_fault_active = 0U;
    static uint8_t s_normal_count = 0U;
    static uint32_t s_last_fault_beep_tick = 0U;

    if (fault_code == NULL)
    {
        return;
    }

    if (strncmp(fault_code, "E00", 3U) == 0)
    {
        if (s_fault_active != 0U)
        {
            if (s_normal_count < 3U)
            {
                s_normal_count++;
            }
            if (s_normal_count >= 3U)
            {
                s_fault_active = 0U;
                strncpy(s_active_fault, "E00", sizeof(s_active_fault));
                EdgeWind_Buzzer_Play(EDGEWIND_BUZZER_EVT_RECOVER);
            }
        }
        return;
    }

    uint32_t now = HAL_GetTick();
    uint8_t changed = (s_fault_active == 0U) ||
                      (strncmp(s_active_fault, fault_code, 3U) != 0);
    uint8_t due = ((uint32_t)(now - s_last_fault_beep_tick) >= EDGEWIND_BUZZER_REPEAT_LIMIT_MS);

    s_fault_active = 1U;
    s_normal_count = 0U;
    strncpy(s_active_fault, fault_code, sizeof(s_active_fault) - 1U);
    s_active_fault[sizeof(s_active_fault) - 1U] = '\0';

    if ((changed != 0U) || (due != 0U))
    {
        s_last_fault_beep_tick = now;
        EdgeWind_Buzzer_Play(EdgeWind_Buzzer_EventForFault(fault_code));
    }
}

void EdgeWind_Buzzer_Task(void *argument)
{
    (void)argument;
    EdgeWind_Buzzer_SetOutput(0U);

    for (;;)
    {
        EdgeWind_BuzzerEvent_t event;
        if ((s_buzzer_queue != NULL) &&
            (osMessageQueueGet(s_buzzer_queue, &event, NULL, osWaitForever) == osOK))
        {
            EdgeWind_Buzzer_PlayPattern(event);
        }
        else
        {
            osDelay(50U);
        }
    }
}
