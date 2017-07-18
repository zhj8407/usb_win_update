#pragma once

#ifndef _PLCM_TIME_UTILS_H_
#define _PLCM_TIME_UTILS_H_

#if defined(_MSC_VER)
#include <BaseTsd.h>
#endif

#if defined(_WIN32)
#include<windows.h>
#include<winbase.h>
#pragma pack (1)
struct wup_calendar_time {
    UINT16 wYear;
    UINT16 wMonth;
    UINT16 wDay;
    UINT16 wHour;
    UINT16 wMinute;
    UINT16 wSecond;
    INT32 dwTimeZone;
    INT32 dwDaylight;
};
#pragma pack ()
#else
#include <stdint.h>
#include <time.h>
struct wup_calendar_time {
    uint16_t wYear;
    uint16_t wMonth;
    uint16_t wDay;
    uint16_t wHour;
    uint16_t wMinute;
    uint16_t wSecond;
    int32_t dwTimeZone;
    int32_t dwDaylight;
} __attribute__((packed));
#endif

int wup_get_calendar_time(struct wup_calendar_time *c_time);

#endif /* _PLCM_TIME_UTILS_H_ */
