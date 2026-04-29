#include "sd_time.h"

#include "rtc.h"

#include <stdio.h>

static bool sd_time_read(RTC_TimeTypeDef *t, RTC_DateTypeDef *d)
{
    if (!t || !d) {
        return false;
    }
    if (HAL_RTC_GetTime(&hrtc, t, RTC_FORMAT_BIN) != HAL_OK) {
        return false;
    }
    if (HAL_RTC_GetDate(&hrtc, d, RTC_FORMAT_BIN) != HAL_OK) {
        return false;
    }
    return true;
}

static bool sd_is_leap(int year)
{
    return ((year % 4) == 0 && (year % 100) != 0) || ((year % 400) == 0);
}

static uint32_t sd_days_before_year(int year)
{
    uint32_t days = 0;
    for (int y = 1970; y < year; ++y) {
        days += sd_is_leap(y) ? 366u : 365u;
    }
    return days;
}

static uint32_t sd_days_before_month(int year, int month)
{
    static const uint16_t k_days_before_month[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    uint32_t days = k_days_before_month[month - 1];
    if (month > 2 && sd_is_leap(year)) {
        days += 1u;
    }
    return days;
}

bool SD_Time_GetDate(char *buf, size_t len)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    if (!buf || len < 11) {
        return false;
    }
    if (!sd_time_read(&t, &d)) {
        return false;
    }
    int year = 2000 + d.Year;
    (void)snprintf(buf, len, "%04d-%02u-%02u", year, d.Month, d.Date);
    return true;
}

bool SD_Time_GetTime(char *buf, size_t len)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    if (!buf || len < 9) {
        return false;
    }
    if (!sd_time_read(&t, &d)) {
        return false;
    }
    (void)snprintf(buf, len, "%02u-%02u-%02u", t.Hours, t.Minutes, t.Seconds);
    return true;
}

bool SD_Time_GetTimestamp(char *buf, size_t len)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    if (!buf || len < 20) {
        return false;
    }
    if (!sd_time_read(&t, &d)) {
        return false;
    }
    int year = 2000 + d.Year;
    (void)snprintf(buf, len, "%04d-%02u-%02u_%02u-%02u-%02u",
                   year, d.Month, d.Date, t.Hours, t.Minutes, t.Seconds);
    return true;
}

bool SD_Time_GetDatePath(char *buf, size_t len, const char *base_dir)
{
    char date[16];
    if (!buf || !base_dir) {
        return false;
    }
    if (!SD_Time_GetDate(date, sizeof(date))) {
        return false;
    }
    if (snprintf(buf, len, "%s/%s", base_dir, date) <= 0) {
        return false;
    }
    return true;
}

bool SD_Time_GetMonthTag(char *buf, size_t len)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    if (!buf || len < 8) {
        return false;
    }
    if (!sd_time_read(&t, &d)) {
        return false;
    }
    int year = 2000 + d.Year;
    (void)snprintf(buf, len, "%04d-%02u", year, d.Month);
    return true;
}

uint32_t SD_Time_GetUnix(void)
{
    RTC_TimeTypeDef t;
    RTC_DateTypeDef d;
    if (!sd_time_read(&t, &d)) {
        return 0;
    }
    int year = 2000 + d.Year;
    int month = d.Month;
    int day = d.Date;
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return 0;
    }
    uint32_t days = sd_days_before_year(year) + sd_days_before_month(year, month) + (uint32_t)(day - 1);
    uint32_t seconds = (uint32_t)t.Hours * 3600u + (uint32_t)t.Minutes * 60u + (uint32_t)t.Seconds;
    return days * 86400u + seconds;
}
