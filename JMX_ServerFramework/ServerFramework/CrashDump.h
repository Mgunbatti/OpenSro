#pragma once

#include <windows.h>

// 0x4F9D45: set once the crash dump filter ran.
extern BYTE g_bCrashDumpWritten;

// CrashDump_TopLevelExceptionFilter (0x452fd0): writes
// <module dir>\<service>_[YYYY-MM-DD hh-mm-ss].dmp with MiniDumpWriteDump (DBGHELP.DLL next to
// the exe first, then the system one). Returns EXCEPTION_EXECUTE_HANDLER.
LONG WINAPI CrashDump_TopLevelExceptionFilter(EXCEPTION_POINTERS* pExceptionInfo);

// SetUnhandledExceptionFilter_wrap (0x453530)
void CrashDump_InstallFilter();
