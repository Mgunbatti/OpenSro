#pragma once

#include <windows.h>
#include <string>

// Date and time helpers for dialogs (date and time picker pairs) and logs.

// DateTimePicker_FormatDateTimeString (0x466dc0): the date of hDate and the time of hTime
// (DTM_GETSYSTEMTIME) as "Y-M-D hh:mm:ss".
std::string DateTimePicker_FormatDateTimeString(HWND hDate, HWND hTime);

// Dlg_SetDateTimePickers (0x466f10): sends the same SYSTEMTIME to the date and the time picker
// (DTM_SETSYSTEMTIME, GDT_VALID). Returns the time picker's result.
LRESULT Dlg_SetDateTimePickers(HWND hDlg, int nDateID, int nTimeID, const SYSTEMTIME* pst);

// Dlg_GetDateTimeFromPickers (0x466f50): date from the date picker, hour / minute / second from
// the time picker.
SYSTEMTIME* Dlg_GetDateTimeFromPickers(HWND hDlg, int nDateID, int nTimeID, SYSTEMTIME* pst);

// Time_FormatLocalDateTime (0x466fb0): long date and time in the user's locale format,
// separated by a space.
std::string Time_FormatLocalDateTime(const SYSTEMTIME* pst);

// GetLocalTimeAsSmallDateTime (0x47b660): the current local time as an SQL smalldatetime
// (days since 1900-01-01 in the high word, minute of the day in the low word).
DWORD GetLocalTimeAsSmallDateTime();
