#include "MachineManagerMainProcess.h"

#include <stdlib.h>
#include <string.h>

#include "MachineManager.h"
#include "PerfCounter.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/ServerApp.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"

#include <strsafe.h>

using namespace ServerFramework;

BS_IMPLEMENT_DYNCREATE(CMachineManagerMainProcess, &CServerProcessMain::classCServerProcessMain)

bool MachineManager_BuildModuleExePath(char* pszPath, const char* pszName)
{
    if (FAILED(StringCchCopyA(pszPath, MAX_PATH, g_ServerGlobals.szModuleDir))
        || FAILED(StringCchCatA(pszPath, MAX_PATH, "\\"))
        || FAILED(StringCchCatA(pszPath, MAX_PATH, pszName))
        || FAILED(StringCchCatA(pszPath, MAX_PATH, ".exe")))
    {
        BSAssertFail();
        return false;
    }
    return true;
}

// BUG(MM-003): without a window WM_CLOSE goes to the calling thread's own queue; such a module
// is always terminated after the timeout.
// BUG(MM-004): without a window any other process whose exe name starts with pszName is taken.
bool MachineManager_CloseOrTerminateProcess(const char* pszName)
{
    DWORD dwProcessID;
    HWND hWnd = FindWindowA(pszName, pszName);
    if (hWnd)
    {
        GetWindowThreadProcessId(hWnd, &dwProcessID);
    }
    else
    {
        dwProcessID = FindOtherProcessByExeName(pszName);
        if (!dwProcessID)
            return false;
    }
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, dwProcessID);
    PostMessageA(hWnd, WM_CLOSE, 0, 0);
    DWORD dwTimeout = 15000;
    if (MachineManager_FindShutdownProcessTimeoutConfig())
        dwTimeout = atol(MachineManager_FindShutdownProcessTimeoutConfig());
    if (WaitForSingleObject(hProcess, dwTimeout) == WAIT_TIMEOUT)
    {
        TerminateProcess(hProcess, (UINT)-1);
        LogPrintf(0x1000000, "terminate server process abnormaly : %s", pszName);
    }
    CloseHandle(hProcess);
    return true;
}

// ---------------------------------------------------------------------------------------------
// CMachineManagerMainProcess

CMachineManagerMainProcess::CMachineManagerMainProcess()
{
}

CMachineManagerMainProcess::~CMachineManagerMainProcess()
{
}

BOOL CMachineManagerMainProcess::Initialize(void* pOwner, void* pParam)
{
    return CServerProcessMain::Initialize(pOwner, pParam) != FALSE;
}

void CMachineManagerMainProcess::InitMsgHandlers()
{
    CServerProcessMain::InitMsgHandlers();
    RegisterHandler(MACHINE_MSG_START_PROCESS_REQ, &CMachineManagerMainProcess::OnMsg_6301);
    RegisterHandler(MACHINE_MSG_STOP_PROCESS_REQ, &CMachineManagerMainProcess::OnMsg_6302);
    RegisterHandler(MACHINE_MSG_PERF_STATS_REQ, &CMachineManagerMainProcess::OnMsg_6304);
}

static void WriteResult(CMsg* pReply, BYTE byResult, BYTE byError)
{
    pReply->WriteBytes(&byResult, 1);
    pReply->WriteBytes(&byError, 1);
}

// {BYTE module ID}: (re)starts <module dir>\<module name>.exe with the module directory as
// current directory. A running instance is stopped first.
// BUG(MM-002): the process and thread handles of the new process are never closed.
void CMachineManagerMainProcess::OnMsg_6301(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    BYTE byModuleID;
    *pMsg >> byModuleID;
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(MACHINE_MSG_START_PROCESS_ACK);
    if (!g_pSelfBody)
    {
        WriteResult(pReply, 2, MACHINE_ERROR_NOT_CERTIFIED);
    }
    else
    {
        CERT_MODULE* pModule = CertModule_FindById(byModuleID);
        if (!pModule)
        {
            WriteResult(pReply, 2, MACHINE_ERROR_UNKNOWN_MODULE);
        }
        else
        {
            const char* pszName = pModule->szName;
            if (SingleInstance_Exists(pszName))
                MachineManager_CloseOrTerminateProcess(pszName);
            char szPath[MAX_PATH];
            // the start is tried with whatever the path holds
            if (!MachineManager_BuildModuleExePath(szPath, pszName))
                LogPrintf(0x1000000, "GetProcessName() Failed!");
            STARTUPINFOA si;
            memset(&si, 0, sizeof(si));
            PROCESS_INFORMATION pi;
            memset(&pi, 0, sizeof(pi));
            si.cb = sizeof(si);
            if (CreateProcessA(szPath, szPath, NULL, NULL, FALSE, 0, NULL,
                               g_ServerGlobals.szModuleDir, &si, &pi))
            {
                BYTE byResult = 1;
                pReply->WriteBytes(&byResult, 1);
            }
            else
            {
                LogPrintf(0x2000000, "invalid path : %s", szPath);
                WriteResult(pReply, 2, MACHINE_ERROR_PROCESS);
            }
        }
    }
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

// {BYTE module ID}: stops the running module.
void CMachineManagerMainProcess::OnMsg_6302(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    BYTE byModuleID;
    *pMsg >> byModuleID;
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(MACHINE_MSG_STOP_PROCESS_ACK);
    if (!g_pSelfBody)
    {
        WriteResult(pReply, 2, MACHINE_ERROR_NOT_CERTIFIED);
    }
    else
    {
        CERT_MODULE* pModule = CertModule_FindById(byModuleID);
        if (!pModule)
        {
            WriteResult(pReply, 2, MACHINE_ERROR_UNKNOWN_MODULE);
        }
        else if (!SingleInstance_Exists(pModule->szName))
        {
            WriteResult(pReply, 2, MACHINE_ERROR_PROCESS);
        }
        else
        {
            MachineManager_CloseOrTerminateProcess(pModule->szName);
            BYTE byResult = 1;
            pReply->WriteBytes(&byResult, 1);
        }
    }
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}

// Collects the counters and answers {1, processor (value, min, max, mean), memory (...)};
// a counter PDH did not accept is sent as zeros.
void CMachineManagerMainProcess::OnMsg_6304(CMsg* pMsg, DWORD dwRelayID, DWORD, CMassiveMsg*)
{
    PerfCounter_CollectAll();
    CMsg* pReply = g_pBSNet->AllocMsg(FALSE);
    pReply->SetOpcode(MACHINE_MSG_PERF_STATS_ACK);
    BYTE byResult = 1;
    pReply->WriteBytes(&byResult, 1);
    for (int nIndex = PERF_COUNTER_PROCESSOR; nIndex <= PERF_COUNTER_MEMORY; ++nIndex)
    {
        LONG lValue = 0;
        LONG lMin = 0;
        LONG lMax = 0;
        LONG lMean = 0;
        PerfCounter_GetValues(nIndex, &lValue, &lMin, &lMax, &lMean);
        pReply->WriteBytes(&lValue, 4);
        pReply->WriteBytes(&lMin, 4);
        pReply->WriteBytes(&lMax, 4);
        pReply->WriteBytes(&lMean, 4);
    }
    SendRelayReply(pMsg->m_dwSessionID, dwRelayID, pReply);
}
