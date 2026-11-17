#include "DateTimeUtil.h"

#include <commctrl.h>
#include <string.h>
#include <time.h>
#include <strsafe.h>

#include "BSLib/BSDebug.h"

std::string DateTimePicker_FormatDateTimeString(HWND hDate, HWND hTime)
{
    SYSTEMTIME stDate;
    SYSTEMTIME stTime;
    memset(&stDate, 0, sizeof(stDate));
    memset(&stTime, 0, sizeof(stTime));
    SendMessageA(hDate, DTM_GETSYSTEMTIME, 0, (LPARAM)&stDate);
    SendMessageA(hTime, DTM_GETSYSTEMTIME, 0, (LPARAM)&stTime);
    stDate.wSecond = stTime.wSecond;
    stDate.wMinute = stTime.wMinute;
    stDate.wHour = stTime.wHour;

    char szText[MAX_PATH];
    if (StringCchPrintfA(szText, MAX_PATH, "%d-%d-%d %02d:%02d:%02d", stDate.wYear,
                         stDate.wMonth, stDate.wDay, stDate.wHour, stDate.wMinute,
                         stDate.wSecond) != S_OK)
    {
        BSAssertFail();
    }
    return std::string(szText);
}

LRESULT Dlg_SetDateTimePickers(HWND hDlg, int nDateID, int nTimeID, const SYSTEMTIME* pst)
{
    SendMessageA(GetDlgItem(hDlg, nDateID), DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)pst);
    return SendMessageA(GetDlgItem(hDlg, nTimeID), DTM_SETSYSTEMTIME, GDT_VALID, (LPARAM)pst);
}

SYSTEMTIME* Dlg_GetDateTimeFromPickers(HWND hDlg, int nDateID, int nTimeID, SYSTEMTIME* pst)
{
    SendMessageA(GetDlgItem(hDlg, nDateID), DTM_GETSYSTEMTIME, 0, (LPARAM)pst);
    SYSTEMTIME stTime;
    SendMessageA(GetDlgItem(hDlg, nTimeID), DTM_GETSYSTEMTIME, 0, (LPARAM)&stTime);
    pst->wHour = stTime.wHour;
    pst->wMinute = stTime.wMinute;
    pst->wSecond = stTime.wSecond;
    return pst;
}

std::string Time_FormatLocalDateTime(const SYSTEMTIME* pst)
{
    char szDateFormat[MAX_PATH];
    char szTimeFormat[MAX_PATH];
    char szTime[MAX_PATH];
    char szDate[MAX_PATH];

    GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP | LOCALE_SLONGDATE, szDateFormat,
                   MAX_PATH);
    GetLocaleInfoA(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP | LOCALE_STIMEFORMAT, szTimeFormat,
                   MAX_PATH);
    GetTimeFormatA(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP, pst, szTimeFormat, szTime, MAX_PATH);
    GetDateFormatA(LOCALE_USER_DEFAULT, LOCALE_USE_CP_ACP, pst, szDateFormat, szDate, MAX_PATH);

    if (StringCchCatA(szDate, MAX_PATH, " ") != S_OK)
        BSAssertFail();
    if (StringCchCatA(szDate, MAX_PATH, szTime) != S_OK)
        BSAssertFail();
    return std::string(szDate);
}

DWORD GetLocalTimeAsSmallDateTime()
{
    __time32_t tNow = 0;
    _time32(&tNow);
    long lBias = 0;
    _get_timezone(&lBias);
    // BUG(FWUTIL-007): only the standard time bias is applied, daylight saving time is ignored.
    // 36816480 = minutes from 1900-01-01 to 1970-01-01.
    DWORD dwMinutes = (DWORD)((tNow - lBias) / 60 + 36816480);
    DWORD dwDays = dwMinutes / 1440;
    DWORD dwMinuteOfDay = dwMinutes - dwDays * 1440;
    return ((DWORD)(WORD)dwDays << 16) | (WORD)dwMinuteOfDay;
}
