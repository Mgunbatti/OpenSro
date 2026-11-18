#include "GlobalManagerSlave.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "GlobalManagerDB.h"
#include "GlobalManagerJobs.h"
#include "GlobalManagerMainProcess.h"
#include "IPCountryTable.h"
#include "BSLib/BSDebug.h"
#include "BSLib/DB/DBQuery.h"
#include "BSLib/thread/Task.h"
#include "BSNet/IBSNet.h"
#include "BSNet/Msg.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerConfig.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/Trigger.h"

using namespace ServerFramework;

CGlobalManagerSlave* g_pGlobalManagerSlave = NULL;

BS_IMPLEMENT_DYNCREATE(CGlobalManagerSlave, &CServerProcessBase::classCServerProcessBase)

// 0x52D690: the start of the poll, the highest purchase number from m_qwLastBuyNo on (one
// %I64d).
const char CGlobalManagerSlave::s_szEncodedFirstQuery[] =
    "\xe3\xf5\xfc\xf5\xf3\xe4\xb0\xfd\xf1\xe8\xb8\xd2\xe5\xe9\xde\xff\xb9\xb0\xf6\xe2\xff\xfd"
    "\xb0\xe3\xfb\xcf\xe3\xf9\xfc\xfb\xf2\xe5\xe9\xfc\xf9\xe3\xe4\xb0\xe7\xf8\xf5\xe2\xf5\xb0"
    "\xd2\xe5\xe9\xde\xff\xb0\xae\xad\xb0\xb5\xd9\xa6\xa4\xf4\xb0\xf1\xfe\xf4\xb0\xe3\xf9\xfc"
    "\xfb\xcf\xe4\xe9\xe0\xf5\xb0\xad\xb0\xa0\xb0\xf1\xfe\xf4\xb0\xe3\xf9\xfc\xfb\xcf\xe2\xf5"
    "\xf1\xe3\xff\xfe\xb0\xf9\xfe\xb0\xb8\xa0\xbc\xb0\xa1\xb9";

// 0x52D740: the four values of SILK_POLL_ROW for the purchases after m_qwLastBuyNo (four
// %I64d).
const char CGlobalManagerSlave::s_szEncodedPollQuery[] =
    "\xe3\xf5\xfc\xf5\xf3\xe4\xb0\xb8\xe3\xf5\xfc\xf5\xf3\xe4\xb0\xe3\xe5\xfd\xb8\xe3\xf9\xfc"
    "\xfb\xcf\xff\xf6\xf6\xe3\xf5\xe4\xb9\xb0\xf6\xe2\xff\xfd\xb0\xe3\xfb\xcf\xe3\xf9\xfc\xfb"
    "\xf2\xe5\xe9\xfc\xf9\xe3\xe4\xb0\xe7\xf8\xf5\xe2\xf5\xb0\xd2\xe5\xe9\xde\xff\xb0\xae\xb0"
    "\xb5\xd9\xa6\xa4\xf4\xb0\xf1\xfe\xf4\xb0\xe3\xf9\xfc\xfb\xcf\xe4\xe9\xe0\xf5\xb0\xad\xb0"
    "\xa0\xb0\xf1\xfe\xf4\xb0\xe3\xf9\xfc\xfb\xcf\xe2\xf5\xf1\xe3\xff\xfe\xb0\xad\xb0\xa0\xb9"
    "\xbc\xb0\xb8\xb0\xe3\xf5\xfc\xf5\xf3\xe4\xb0\xe3\xe5\xfd\xb8\xe3\xf9\xfc\xfb\xcf\xff\xf6"
    "\xf6\xe3\xf5\xe4\xb9\xb0\xf6\xe2\xff\xfd\xb0\xe3\xfb\xcf\xe3\xf9\xfc\xfb\xf2\xe5\xe9\xfc"
    "\xf9\xe3\xe4\xb0\xe7\xf8\xf5\xe2\xf5\xb0\xd2\xe5\xe9\xde\xff\xb0\xae\xb0\xb5\xd9\xa6\xa4"
    "\xf4\xb0\xf1\xfe\xf4\xb0\xe3\xf9\xfc\xfb\xcf\xe4\xe9\xe0\xf5\xb0\xad\xb0\xa0\xb0\xf1\xfe"
    "\xf4\xb0\xe3\xf9\xfc\xfb\xcf\xe2\xf5\xf1\xe3\xff\xfe\xb0\xad\xb0\xa1\xb0\xb0\xb0\xb9\xbc"
    "\xb0\xb8\xb0\xe3\xf5\xfc\xf5\xf3\xe4\xb0\xfd\xf1\xe8\xb8\xd2\xe5\xe9\xde\xff\xb9\xb0\xf6"
    "\xe2\xff\xfd\xb0\xe3\xfb\xcf\xe3\xf9\xfc\xfb\xf2\xe5\xe9\xfc\xf9\xe3\xe4\xb0\xe7\xf8\xf5"
    "\xe2\xf5\xb0\xd2\xe5\xe9\xde\xff\xb0\xae\xb0\xb5\xd9\xa6\xa4\xf4\xb0\xf1\xfe\xf4\xb0\xe3"
    "\xf9\xfc\xfb\xcf\xe4\xe9\xe0\xf5\xb0\xad\xb0\xa0\xb0\xf1\xfe\xf4\xb0\xe3\xf9\xfc\xfb\xcf"
    "\xe2\xf5\xf1\xe3\xff\xfe\xb0\xf9\xfe\xb0\xb8\xa0\xbc\xb0\xa1\xb9\xb0\xb0\xb0\xb9\xbc\xb0"
    "\xb8\xb0\xe3\xf5\xfc\xf5\xf3\xe4\xb0\xf3\xff\xe5\xfe\xe4\xb8\xd2\xe5\xe9\xde\xff\xb9\xb0"
    "\xf6\xe2\xff\xfd\xb0\xe3\xfb\xcf\xe3\xf9\xfc\xfb\xf2\xe5\xe9\xfc\xf9\xe3\xe4\xb0\xe7\xf8"
    "\xf5\xe2\xf5\xb0\xd2\xe5\xe9\xde\xff\xb0\xae\xb0\xb5\xd9\xa6\xa4\xf4\xb0\xf1\xfe\xf4\xb0"
    "\xe3\xf9\xfc\xfb\xcf\xe4\xe9\xe0\xf5\xb0\xad\xb0\xa0\xb0\xf1\xfe\xf4\xb0\xe3\xf9\xfc\xfb"
    "\xcf\xe2\xf5\xf1\xe3\xff\xfe\xb0\xf9\xfe\xb0\xb8\xa0\xbc\xb0\xa1\xb9\xb0\xb9";

// ---------------------------------------------------------------------------------------------
// Helpers

LARGE_INTEGER* GMSlave_GetPerfFrequency()
{
    static bool s_bQueried = false;
    static LARGE_INTEGER s_liFrequency;
    if (!s_bQueried)
    {
        s_bQueried = true;
        QueryPerformanceFrequency(&s_liFrequency);
    }
    return &s_liFrequency;
}

__int64 GMSlave_GetPerfCounterMs(const LARGE_INTEGER* pliFrequency)
{
    LARGE_INTEGER liCounter;
    QueryPerformanceCounter(&liCounter);
    return (__int64)((double)liCounter.QuadPart / (double)pliFrequency->QuadPart * 1000.0);
}

void GMSlave_PackTimestamp(SILK_HISTORY_TIME* pOut, const TIMESTAMP_STRUCT* pTime)
{
    pOut->dwYear = pTime->year - 2000;
    pOut->dwMonth = pTime->month;
    pOut->dwDay = pTime->day;
    pOut->dwHour = pTime->hour;
    pOut->dwMinute = pTime->minute;
    pOut->dwSecond = pTime->second;
}

void CGlobalManagerSlave::DecodeText(char* pszText)
{
    for (int i = 0; i < lstrlenA(pszText); ++i)
        pszText[i] ^= 0x90;
}

// The main task's queue gets the 0xA319 answers (g_pGlobalManagerProcess->GetOwner()).
static void PostToMainTask(CMsg* pMsg)
{
    CTask* pTask = (CTask*)g_pGlobalManagerProcess->GetOwner();
    pTask->m_pQueue->Push(pMsg);
}

// ---------------------------------------------------------------------------------------------
// Construction

CGlobalManagerSlaveSingleton::CGlobalManagerSlaveSingleton()
{
    if (g_pGlobalManagerSlave)
        BSAssertFail();
    g_pGlobalManagerSlave = static_cast<CGlobalManagerSlave*>(this);
}

CGlobalManagerSlaveSingleton::~CGlobalManagerSlaveSingleton()
{
    g_pGlobalManagerSlave = NULL;
}

CGlobalManagerSlave::CGlobalManagerSlave()
{
    m_pMainProcess = NULL;
    m_bRIRLoaded = FALSE;
    m_bSilkHistoryEnabled = FALSE;
    m_qwLastSilkPoll = 0;
    m_qwLastBuyNo = 0;
    m_mapIPStatistics.clear();
}

CGlobalManagerSlave::~CGlobalManagerSlave()
{
    ClearIPStatistics();
    CIPCountryTable::GetInstance();
    if (g_pIPCountryTable)
        delete g_pIPCountryTable;
}

// ---------------------------------------------------------------------------------------------
// Process

DWORD CGlobalManagerSlave::Run(volatile LONG* pbStop, int nIndex)
{
    if (*pbStop)
        return 0;
    BOOL bClosed;
    do
    {
        CTask* pTask = (CTask*)m_pOwner;
        CMsg* pMsg = NULL;
        bClosed = FALSE;
        if (!pTask->m_pQueue)
            BSAssertFail();
        DWORD dwCount;
        if (pTask->m_pQueue && (dwCount = (DWORD)pTask->m_pQueue->GetSize()) != 0)
        {
            for (DWORD i = 0; i < dwCount; ++i)
            {
                if (((CTask*)m_pOwner)->m_pQueue->Pop(&pMsg, 1, FALSE) != 1)
                {
                    bClosed = TRUE;
                    break;
                }
                if (pMsg)
                {
                    if (!nIndex)
                        BSAssertFail();
                    pMsg->m_pOwner = (void*)(INT_PTR)nIndex;
                    Dispatch(pMsg, 0, 0, NULL);
                    CheckMsgFullyConsumed(pMsg);
                    g_pBSNet->ReleaseMsg(pMsg);
                }
            }
        }
        else
        {
            Sleep(1);
        }
        if (m_bSilkHistoryEnabled)
            PollSilkHistory();
    }
    while (bClosed != TRUE && !*pbStop);
    LogPrintf(0x2000001, "GlobalManagerSlave Thread Closed!");
    return 0;
}

void CGlobalManagerSlave::InitMsgHandlers()
{
    RegisterHandler(0x6319, &CGlobalManagerSlave::OnMsg_6319);
}

void CGlobalManagerSlave::OnUnhandledMsg(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    LogPrintf(0x1000000, "CGlobalManagerSlave::Unhandled Msg: 0x%x", *pMsg->m_pOpcode);
}

void CGlobalManagerSlave::OnMsg_6319(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE byOperation = 0;
    *pMsg >> byOperation;
    switch (byOperation)
    {
    case 0:
        OnDetailJob(pMsg);
        break;
    case 1:
        GetSilkHistory(pMsg);
        break;
    case 2:
        SetSilkHistoryParam(pMsg);
        break;
    default:
        LogPrintf(0x2000001, "Unhandled GlobalManager Slave Job Detected!!! ( OperationType : %X )",
                  byOperation);
        break;
    }
}

void CGlobalManagerSlave::OnDetailJob(CMsg* pMsg)
{
    BYTE byDetail = 0;
    *pMsg >> byDetail;
    if (byDetail == 1)
        LoadRIRFiles(pMsg);
    else if (byDetail == 16)
        IPStatistics(pMsg);
    else
        LogPrintf(0x2000001,
                  "Unhandled GlobalManager Slave Job Detected!!! ( DetailType : %X, %d )",
                  byDetail, 514);
}

// ---------------------------------------------------------------------------------------------
// IP country table and statistics

void CGlobalManagerSlave::LoadRIRFiles(CMsg* pMsg)
{
    DWORD dwRequest = 0;
    *pMsg >> dwRequest;
    CIPCountryTable::GetInstance()->Clear();
    g_ServerGlobals.mapExtra.clear();
    ServerConfig_Load();
    CIPCountryTable::GetInstance()->Reset();
    char szPath[0x400] = { 0 };
    char szDirectory[0x400] = { 0 };
    DWORD dwStart = GetTickCount();
    LogPrintf(0, "Load RIR Files Start");
    GetCurrentDirectoryA(sizeof(szDirectory), szDirectory);
    const char* pszValue = ServerConfig_FindExtraValue("CountryCodeFile");
    if (pszValue)
    {
        sprintf_s(szPath, sizeof(szPath), "%s\\%s", szDirectory, pszValue);
        if (!CIPCountryTable::GetInstance()->LoadCountryCodeFile(szPath))
            LogPrintf(0x2000001, "Loading IP Table is Failed (%s)", szPath);
    }
    pszValue = ServerConfig_FindExtraValue("RIRFileNum");
    int nFiles = pszValue ? atol(pszValue) : 0;
    for (int i = 0; i < nFiles; )
    {
        ++i;
        char szKey[32] = { 0 };
        wsprintfA(szKey, "RIRFile%d", i);
        pszValue = ServerConfig_FindExtraValue(szKey);
        if (!pszValue)
            continue;
        wsprintfA(szPath, "%s\\%s", szDirectory, pszValue);
        if (!CIPCountryTable::GetInstance()->LoadRIRFileTimed(szPath))
            LogPrintf(0x2000001, "Loading IP Table is Failed (%s)", szPath);
    }
    LogPrintf(0, "Load RIR Files End (%d ticks)", GetTickCount() - dwStart);
    CIPCountryTable::GetInstance()->CloseFile();
    CMsg* pAnswer = g_pBSNet->AllocMsg(FALSE);
    pAnswer->SetOpcode(0xA319);
    BYTE byEnd = 3;
    BYTE byDetail = 2;
    pAnswer->WriteBytes(&byDetail, 1);
    pAnswer->WriteBytes(&dwRequest, 4);
    pAnswer->WriteBytes(&byEnd, 1);
    PostToMainTask(pAnswer);
    m_bRIRLoaded = TRUE;
}

void CGlobalManagerSlave::ClearIPStatistics()
{
    for (IPStatisticsMap::iterator it = m_mapIPStatistics.begin(); it != m_mapIPStatistics.end();
         ++it)
    {
        if (it->second)
            operator delete(it->second);
    }
    m_mapIPStatistics.clear();
}

int CGlobalManagerSlave::OnIPStatisticsRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (!pQuery->m_bRowFetched)
    {
        DWORD* pRow = (DWORD*)pQuery->m_dwParam;
        pQuery->m_pBuffer = (BYTE*)pRow;
        pQuery->m_nRowSize = 8;
        pQuery->m_nBufferSize = 8;
        pRow[0] = 0;
        pRow[1] = 0;
        return 1;
    }
    const DWORD* pRow = (const DWORD*)pQuery->m_pBuffer;
    std::string strCode;
    strCode.clear();
    char szCode[4] = { 0 };
    CIPCountryTable::GetInstance()->FindCountryCode(szCode, pRow[1]);
    // BUG(GM-022): the code found is never put into strCode; every row counts for "".
    IPStatisticsMap::iterator it = m_mapIPStatistics.find(strCode);
    if (it == m_mapIPStatistics.end())
    {
        IP_STATISTICS_ENTRY* pEntry = new IP_STATISTICS_ENTRY;
        memset(pEntry, 0, sizeof(*pEntry));
        lstrcpyA(pEntry->szCode, strCode.c_str());
        pEntry->dwCount = 1;
        m_mapIPStatistics.insert(IPStatisticsMap::value_type(strCode, pEntry));
    }
    else
    {
        ++it->second->dwCount;
    }
    return 1;
}

void CGlobalManagerSlave::IPStatistics(CMsg* pMsg)
{
    CClassTrigger<CGlobalManagerSlave> trigger(this);
    trigger.m_pfnTrigger2 = &CGlobalManagerSlave::OnIPStatisticsRow;
    std::string strFrom;
    std::string strTo;
    DWORD dwRequest = 0;
    ClearIPStatistics();
    *pMsg >> strFrom >> strTo;
    *pMsg >> dwRequest;
    // Only these members are set; the trigger sets the buffer and the sizes.
    CDBQuery query;
    DWORD adwRow[2];
    query.m_pSession = g_pDefaultDBSession;
    query.m_dwParam = (DWORD)adwRow;
    adwRow[0] = 0;
    adwRow[1] = 0;
    query.m_pTrigger = &trigger;
    query.m_pUserData = NULL;
    query.m_nSession = 2;
    CMsg* pAnswer = g_pBSNet->AllocMsg(FALSE);
    pAnswer->SetOpcode(0xA319);
    if (query.ExecuteFormat("exec _IPStatistics\t'%s', '%s'", strFrom.c_str(), strTo.c_str())
        < 0)
    {
        LogPrintf(0x2000000, "CGlobalManagerSlave::OnUSER_IPRANGE_STATISTICS_RESULT_REQ (%s)",
                  query.m_szQuery);
        BYTE byError = 0x1B;
        BYTE byDetail = 0x19;
        pAnswer->WriteBytes(&byDetail, 1);
        pAnswer->WriteBytes(&dwRequest, 4);
        pAnswer->WriteBytes(&byError, 1);
        // BUG(GM-023): the failure answer is never sent (nor released).
        return;
    }
    DWORD dwCount = (DWORD)m_mapIPStatistics.size();
    BYTE byDetail = 0x11;
    pAnswer->WriteBytes(&byDetail, 1);
    pAnswer->WriteBytes(&dwRequest, 4);
    pAnswer->WriteBytes(&dwCount, 4);
    for (IPStatisticsMap::iterator it = m_mapIPStatistics.begin(); it != m_mapIPStatistics.end();
         ++it)
    {
        IP_STATISTICS_ENTRY* pEntry = it->second;
        std::string strName;
        std::string strCode(pEntry->szCode, strlen(pEntry->szCode));
        CIPCountryTable::GetInstance()->GetCountryName(strCode, &strName);
        *pAnswer << strName.c_str();
        pAnswer->WriteBytes(&pEntry->dwCount, 4);
    }
    PostToMainTask(pAnswer);
}

// ---------------------------------------------------------------------------------------------
// Silk history

int CGlobalManagerSlave::OnSilkHistoryRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (!pQuery->m_bRowFetched)
    {
        SILK_HISTORY_ROW* pRow = (SILK_HISTORY_ROW*)pQuery->m_dwParam;
        pQuery->m_nRowSize = sizeof(SILK_HISTORY_ROW);
        pQuery->m_nBufferSize = sizeof(SILK_HISTORY_ROW);
        pQuery->m_pBuffer = (BYTE*)pRow;
        memset(pRow, 0, sizeof(*pRow));
        return 1;
    }
    SILK_HISTORY_ROW row;
    memcpy(&row, pQuery->m_pBuffer, pQuery->m_nRowSize);
    SILK_HISTORY_ENTRY entry;
    *(DWORD*)&entry.time = 0;
    entry.dw0 = row.dw0;
    GMSlave_PackTimestamp(&entry.time, &row.ts);
    ((std::vector<SILK_HISTORY_ENTRY>*)pQuery->m_pUserData)->push_back(entry);
    return 1;
}

void CGlobalManagerSlave::GetSilkHistory(CMsg* pMsg)
{
    WORD wTargetBody = 0;
    SILK_HISTORY_REQ req;
    memset(&req, 0, sizeof(req));
    *pMsg >> wTargetBody;
    *pMsg >> req;
    CMsg* pAnswer = g_pBSNet->AllocMsg(FALSE);
    pAnswer->SetOpcode(0xA31A);
    DWORD dwValue = 1;
    pAnswer->WriteBytes(&dwValue, 4);
    pAnswer->WriteBytes(&req, sizeof(req));
    CClassTrigger<CGlobalManagerSlave> trigger(this);
    trigger.m_pfnTrigger2 = &CGlobalManagerSlave::OnSilkHistoryRow;
    SILK_HISTORY_ROW row;
    memset(&row, 0, sizeof(row));
    std::vector<SILK_HISTORY_ENTRY> vecEntry;
    vecEntry.clear();
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pTrigger = &trigger;
    query.m_pSession = g_pSilkDBSession;
    query.m_dwParam = (DWORD)&row;
    query.m_pUserData = &vecEntry;
    query.m_nSession = 2;
    if (query.ExecuteFormat("exec _GetSilkHistory %d, %d, %d", req.dw4, req.byC, req.byD) < 0)
    {
        BYTE byResult = 0xF6;
        pAnswer->WriteBytes(&byResult, 1);
    }
    else
    {
        if (!vecEntry.empty() && vecEntry.size() > 0xFFFF)
            BSAssertFail();
        dwValue = (WORD)vecEntry.size();
        BYTE byResult = 0;
        pAnswer->WriteBytes(&byResult, 1);
        pAnswer->WriteBytes(&dwValue, 2);
        int nCount = (WORD)dwValue;
        for (int i = 0; i < nCount; ++i)
            pAnswer->WriteBytes(&vecEntry[i], sizeof(SILK_HISTORY_ENTRY));
        vecEntry.clear();
    }
    if (m_pMainProcess)
        RelayMsgToBody_6008(m_pMainProcess, pAnswer, wTargetBody, 0, 0, 0);
}

void CGlobalManagerSlave::SetSilkHistoryParam(CMsg* pMsg)
{
    BYTE byFlag = 0;
    std::string strBuyNo;
    *pMsg >> byFlag;
    *pMsg >> strBuyNo;
    m_qwLastBuyNo = _atoi64(strBuyNo.c_str());
    if (byFlag == 16)
        m_bSilkHistoryEnabled = TRUE;
}

BOOL CGlobalManagerSlave::IsSilkHistoryDue()
{
    LARGE_INTEGER* pliFrequency = GMSlave_GetPerfFrequency();
    LARGE_INTEGER liCounter;
    QueryPerformanceCounter(&liCounter);
    unsigned __int64 qwNow =
        (__int64)((double)liCounter.QuadPart / (double)pliFrequency->QuadPart * 1000.0);
    return qwNow - m_qwLastSilkPoll > 300000;
}

void CGlobalManagerSlave::PollSilkHistory()
{
    if (!IsSilkHistoryDue() || !g_pSilkDBSession)
        return;
    SILK_POLL_ROW row;
    memset(&row, 0, sizeof(row));
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pSilkDBSession;
    query.m_pTrigger = NULL;
    query.m_nSession = 2;
    query.m_pBuffer = (BYTE*)&row;
    query.m_nRowSize = sizeof(row);
    query.m_nBufferSize = sizeof(row);
    // BUG(GM-025): a failed query returns before m_qwLastSilkPoll is set; it is run again on
    // the next pass of the loop.
    if (!m_qwLastSilkPoll)
    {
        char szQuery[0x2800] = { 0 };
        strcpy_s(szQuery, sizeof(szQuery), s_szEncodedFirstQuery);
        DecodeText(szQuery);
        if (query.ExecuteFormat(szQuery, m_qwLastBuyNo) < 0)
        {
            LogPrintf(0x2000000,
                      "You cannot support Silk History Function for User... (ErrCode:0xA8)");
            return;
        }
        m_qwLastBuyNo = row.dwSilkReason0;
    }
    char szQuery[0x2800] = { 0 };
    strcpy_s(szQuery, sizeof(szQuery), s_szEncodedPollQuery);
    DecodeText(szQuery);
    if (query.ExecuteFormat(szQuery, m_qwLastBuyNo, m_qwLastBuyNo, m_qwLastBuyNo, m_qwLastBuyNo)
        < 0)
    {
        LogPrintf(0x2000000,
                  "You cannot support Silk History Function for User... (ErrCode:0x71)");
        return;
    }
    if (row.dwLastBuyNo)
        m_qwLastBuyNo = row.dwLastBuyNo;
    row.dwLastBuyNo = (DWORD)m_qwLastBuyNo;
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(0x231B);
    pMsg->WriteBytes(&g_pSelfBody->byDivisionID, 1);
    pMsg->WriteBytes(&row, sizeof(row));
    if (!m_pMainProcess)
        return;
    m_qwLastSilkPoll = GMSlave_GetPerfCounterMs(GMSlave_GetPerfFrequency());
    if (!RelayMsgToBody_6008(m_pMainProcess, pMsg, g_pSelfBody->wParentID, 0, 0, 0))
        LogPrintf(0x2000001, "RelayMsg Failed (%d) / %d", 346, 100);
}
