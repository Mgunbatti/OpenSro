#pragma once

#include <windows.h>

// 0x4F9D45: set once the crash dump filter ran.
extern BYTE g_bCrashDumpWritten;

// CrashDump_TopLevelExceptionFilter (0x452fd0): writes
// <module dir>\<service>_[YYYY-MM-DD hh-mm-ss].dmp with MiniDumpWriteDump (DBGHELP.DLL next to
// the exe first, then the system one). Returns EXCEPTION_EXECUTE_HANDLER.
LONG WINAPI CrashDump_TopLevelExceptionFilter(EXCEPTION_POINTERS* pExceptionInfo);

#ifdef JMX_FRAMEWORK_2011
// AgentServer: the filter (0x44f8c0) returns EXCEPTION_EXECUTE_HANDLER for NULL, allocates
// {thread ID, pExceptionInfo} and passes it to CrashDump_WriteDump (0x44f2a0, the 2008 body of
// the filter), on a new thread with a 0x19000-byte stack after a stack overflow. 1.
DWORD WINAPI CrashDump_WriteDump(LPVOID pRequest);
#endif

// SetUnhandledExceptionFilter_wrap (0x453530)
void CrashDump_InstallFilter();
