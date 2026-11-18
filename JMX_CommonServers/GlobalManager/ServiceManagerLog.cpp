// The SMC log of CServiceManagerProcess (0x450B40..0x450F10): every line becomes an SMLog job
// (_SMCLog: account, type, text).

#include "ServiceManagerProcess.h"

#include <stdarg.h>
#include <string.h>
#include <strsafe.h>

#include "GlobalManagerSvcFwd.h"
#include "ServiceManagerJobs.h"
#include "BSLib/BSDebug.h"
#include "ServerFramework/ServerProcess.h"

using namespace ServerFramework;

static void PostSMLog(CTask* pTask, std::string* pstrUserID, BYTE byType, const char* pszText)
{
    SM_JOB* pJob = OverlapJob_AllocSMLog();
    OverlapJob_SetupSMLog(pTask, pJob, pstrUserID, byType, pszText);
    ((CServerProcessBase*)pTask->GetJob())->SendOverlapJob(pJob, NULL);
}

void CServiceManagerProcess::WriteUserLog(std::string* pstrText, std::string* pstrUserID)
{
    PostSMLog(m_pTask, pstrUserID, 1, pstrText->c_str());
}

void CServiceManagerProcess::WriteAnswerLog(std::string* pstrText, DWORD dwSessionID,
                                            BYTE byType)
{
    sServiceManagerUser* pUser = FindUser(dwSessionID);
    if (!pUser)
        return;
    SM_JOB* pJob = OverlapJob_AllocSMLog();
    std::string strUserID;
    strUserID.assign(pUser->strID, 0, std::string::npos);
    OverlapJob_SetupSMLog(m_pTask, pJob, &strUserID, byType, pstrText->c_str());
    ((CServerProcessBase*)m_pTask->GetJob())->SendOverlapJob(pJob, NULL);
}

void CServiceManagerProcess::WriteLog(DWORD dwSessionID, BYTE byType, const char* pszFormat,
                                      ...)
{
    char szText[MAX_PATH];
    va_list args;
    va_start(args, pszFormat);
    if (FAILED(StringCchVPrintfA(szText, MAX_PATH, pszFormat, args)))
        BSAssertFail();
    va_end(args);
    std::string strText(szText);
    WriteAnswerLog(&strText, dwSessionID, byType);
}

void CServiceManagerProcess::WriteQueryLog(DWORD dwSessionID, const char* pszFormat, ...)
{
    char szText[MAX_PATH];
    va_list args;
    va_start(args, pszFormat);
    if (FAILED(StringCchVPrintfA(szText, MAX_PATH, pszFormat, args)))
        BSAssertFail();
    va_end(args);
    std::string strText(szText);
    WriteAnswerLog(&strText, dwSessionID, 0xFF);
}

BYTE SMLog_GetAnswerType(WORD wOpcode)
{
    switch (wOpcode)
    {
    case 0x8004:
        return 0x14;
    case 0x8001:
    case 0x800E:
        return 0x17;
    case 0x8002:
    case 0x8005:
    case 0x8008:
    case 0x800C:
    case 0x8010:
        return 0x15;
    case 0x8007:
        return 0x16;
    case 0x8009:
    case 0x800A:
        return 0x10;
    }
    return 0xFF;
}
