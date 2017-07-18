#include "stdafx.h"
#include "time_utils.h"

int wup_get_calendar_time(struct wup_calendar_time *c_time)
{
#if defined(_WIN32)
    SYSTEMTIME time;
    TIME_ZONE_INFORMATION time_zone;

    GetLocalTime(&time);
    GetSystemTime(&time_zone.StandardDate);
    GetTimeZoneInformation(&time_zone);

    c_time->wYear = time.wYear;
    c_time->wMonth = time.wMonth;
    c_time->wDay = time.wDay;
    c_time->wHour = time.wHour;
    c_time->wMinute = time.wMinute;
    c_time->wSecond = time.wSecond;

    c_time->dwTimeZone = time_zone.Bias;
    c_time->dwDaylight = time_zone.DaylightBias;
#else
    time_t t = time(NULL);
    struct tm local_tm;

    localtime_r(&t, &local_tm);

    c_time->wYear = local_tm.tm_year + 1900;
    c_time->wMonth = local_tm.tm_mon + 1;
    c_time->wDay = local_tm.tm_mday;
    c_time->wHour = local_tm.tm_hour;
    c_time->wMinute = local_tm.tm_min;
    c_time->wSecond = local_tm.tm_sec;

    c_time->dwTimeZone = local_tm.tm_gmtoff / 60;
    c_time->dwDaylight = local_tm.tm_isdst;
#endif
    return 0;
}
