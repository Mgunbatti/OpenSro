// GatewayServer certification part: certifier jobs, certification handlers, captcha (IBUV)
// process and managers, privileged addresses and the login / captcha handlers of the main
// process. The network engine is a recording double; CDBQuery::Execute / ExecuteFormat are
// replaced below (no database): every query takes the next scripted result and row.

#include "BSLib/BSDebug.h"
#include "BSLib/DB/DBQuery.h"
#include "BSNet/MsgPool.h"
#include "BSNet/QueMT.h"
#include "BSNet/security/MD5.h"
#include "GatewayServer/CertifyConstants.h"
#include "GatewayServer/CertifyDB.h"
#include "GatewayServer/CertifyParam.h"
#include "GatewayServer/GatewayMainProcess.h"
#include "GatewayServer/GatewayServer.h"
#include "GatewayServer/IBUVProcess.h"
#include "GatewayServer/PrivilegedIPManager.h"
#include "GatewayServer/ShardConditionManager.h"
#include "GatewayServer/UserCertifier.h"
#include "ServerFramework/CertData.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerDataExtern.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "LogCapture.h"
#include "TestCheck.h"

using namespace ServerFramework;

typedef std::vector<BYTE> Bytes;

// ---------------------------------------------------------------------------------------------
// Byte helpers

static void Put(Bytes& v, const void* p, size_t n)
{
    v.insert(v.end(), (const BYTE*)p, (const BYTE*)p + n);
}

static void PutB(Bytes& v, BYTE b)
{
    v.push_back(b);
}

static void PutW(Bytes& v, WORD w)
{
    Put(v, &w, 2);
}

static void PutD(Bytes& v, DWORD d)
{
    Put(v, &d, 4);
}

static void PutS(Bytes& v, const char* psz)
{
    PutW(v, (WORD)strlen(psz));
    Put(v, psz, strlen(psz));
}

static void PutWS(Bytes& v, const wchar_t* psz)
{
    PutW(v, (WORD)wcslen(psz));
    Put(v, psz, 2 * wcslen(psz));
}

static Bytes B(const char* pszHex)
{
    Bytes v;
    for (const char* p = pszHex; *p;)
    {
        if (*p == ' ')
        {
            ++p;
            continue;
        }
        unsigned n = 0;
        sscanf_s(p, "%2x", &n);
        v.push_back((BYTE)n);
        p += 2;
    }
    return v;
}

static std::string Hex(const Bytes& v)
{
    std::string s;
    char sz[4];
    for (size_t i = 0; i < v.size(); ++i)
    {
        sprintf_s(sz, "%02x ", v[i]);
        s += sz;
    }
    return s;
}

static bool Same(const Bytes& vGot, const Bytes& vWant)
{
    if (vGot == vWant)
        return true;
    printf("  got:  %s\n  want: %s\n", Hex(vGot).c_str(), Hex(vWant).c_str());
    return false;
}

// ---------------------------------------------------------------------------------------------
// Engine double

struct SENT
{
    DWORD   dwSessionID;
    WORD    wOpcode;
    bool    bEncrypted;
    Bytes   data;
};

class CFakeNet : public IBSNet
{
public:
    CFakeNet() : m_nSocketState(1) {}

    CMsgPool m_Pool;
    std::vector<SENT> m_Sent;
    std::vector<DWORD> m_Disconnected;
    std::map<DWORD, DWORD> m_mapPeerIP;     // session -> address; no entry: lookup fails
    int m_nSocketState;
    std::vector<DWORD> m_Registered;
    std::map<DWORD, CTask*> m_mapTask;

    virtual CMsg* __stdcall AllocMsg(BOOL bEncrypted)
    {
        CMsg* p = m_Pool.Alloc();
        if (bEncrypted == 1)
            p->SetEncryptFlag();
        InterlockedIncrement(&p->m_lRefCount);
        return p;
    }
    virtual LONG __stdcall ReleaseMsg(CMsg* p)
    {
        if (p->m_lRefCount > 0 && InterlockedDecrement(&p->m_lRefCount) == 0)
            m_Pool.Free(p);
        return 0;
    }
    virtual int __stdcall SendMsg(DWORD dwSessionID, CMsg* p)
    {
        SENT s;
        s.dwSessionID = dwSessionID;
        s.wOpcode = p->GetOpcode();
        s.bEncrypted = (*p->m_pSize & MSG_ENCRYPTED) != 0;
        Put(s.data, p->m_pData + MSG_HEADER_SIZE, p->m_wWritePos - MSG_HEADER_SIZE);
        m_Sent.push_back(s);
        return 1;
    }
    virtual BOOL __stdcall GetSessionPeerAddress(DWORD dwSessionID, std::string*, ULONG* pulIP,
                                                 u_short* pwPort)
    {
        std::map<DWORD, DWORD>::iterator it = m_mapPeerIP.find(dwSessionID);
        if (it == m_mapPeerIP.end())
            return FALSE;
        if (pulIP)
            *pulIP = it->second;
        if (pwPort)
            *pwPort = 0x4567;
        return TRUE;
    }
    virtual int __stdcall DisconnectSession(DWORD dwSessionID, DWORD)
    {
        m_Disconnected.push_back(dwSessionID);
        return 0;
    }
    virtual int __stdcall GetSessionSocketState(DWORD) { return m_nSocketState; }
    virtual BOOL __stdcall RegisterProactor(DWORD dwID, CTask*, CRuntimeClass* pClass, void*)
    {
        CHECK(pClass == &CServerProcessOverlap::classCServerProcessOverlap);
        m_Registered.push_back(dwID);
        return TRUE;
    }
    virtual BOOL __stdcall StartProactorWorkers(DWORD, int nCount, int, unsigned, DWORD,
                                                THREAD_AFFINITY*)
    {
        CHECK(nCount == 1);
        return TRUE;
    }
    virtual CTask* __stdcall GetProactor(DWORD dwID)
    {
        return m_mapTask[dwID];
    }

    virtual HRESULT __stdcall QueryInterface(const GUID&, void**) { return 0; }
    virtual ULONG __stdcall AddRef() { return 0; }
    virtual int __stdcall Shutdown() { return 0; }
    virtual BOOL __stdcall Initialize(BSNET_INIT_PARAM) { return 0; }
    virtual DWORD __stdcall ConnectSockAddr(const sockaddr*, const sockaddr*, DWORD, int)
    {
        return 0;
    }
    virtual DWORD __stdcall Connect(const char*, u_short, const char*, DWORD, int) { return 0; }
    virtual BOOL __stdcall ListenSockAddr(const sockaddr*) { return 0; }
    virtual BOOL __stdcall Listen(const char*, u_short) { return 0; }
    virtual int __stdcall vf09(DWORD, DWORD, DWORD, DWORD) { return 0; }
    virtual BOOL __stdcall ProactorResume(DWORD) { return 0; }
    virtual BOOL __stdcall ProactorSuspend(DWORD) { return 0; }
    virtual BOOL __stdcall UnregisterProactor(DWORD) { return 0; }
    virtual void __stdcall GetStatistics(DWORD, NETENGINE_STATISTICS*) {}
    virtual BOOL __stdcall StartHandshake(DWORD, SECURITY_HANDSHAKE_PARAM*) { return 0; }
    virtual BOOL __stdcall BeginSessionFileTransfer(DWORD, std::string, DWORD, DWORD, int)
    {
        return 0;
    }
    virtual BOOL __stdcall SendFile(DWORD, const std::string&, DWORD, int) { return 0; }
    virtual int __stdcall CloseSessionTransferFile(DWORD, DWORD) { return 0; }
    virtual void __stdcall ExcludeSessionFromCleanup(DWORD) {}
    virtual BOOL __stdcall RemoveKeepAliveTarget(DWORD) { return 0; }
    virtual void __stdcall AddKeepAliveTarget(const char*, u_short, const char*, DWORD, DWORD) {}
    virtual void __stdcall CloseAllFiles() {}
    virtual void __stdcall DumpMsgPool(BYTE, DWORD) {}
    virtual void __stdcall SetDebugAsserted(BYTE) {}
    virtual void __stdcall CleanupDisconnectedSessions() {}
    virtual CMsgPool* __stdcall GetMsgPool() { return &m_Pool; }
    virtual BOOL __stdcall GetSessionLocalMacAddress(DWORD, BYTE*) { return 0; }
    virtual void __stdcall SetSecurityMode(BYTE) {}
    virtual void __stdcall PrintVersion() {}
    virtual BOOL __stdcall RegisterDatagramProactor(DWORD) { return 0; }
    virtual int __stdcall OpenDatagramSocket(WORD) { return 0; }
    virtual int __stdcall SendDatagram(const sockaddr*, CMsg*) { return 0; }
};

static CFakeNet g_Net;

// Takes the one packet sent so far (checks there is exactly one).
static bool TakeOne(SENT* pSent)
{
    if (g_Net.m_Sent.size() != 1)
    {
        printf("  %u packets sent\n", (unsigned)g_Net.m_Sent.size());
        g_Net.m_Sent.clear();
        return false;
    }
    *pSent = g_Net.m_Sent[0];
    g_Net.m_Sent.clear();
    return true;
}

// The one packet must be opcode to session with exactly these bytes.
static bool Expect(DWORD dwSessionID, WORD wOpcode, const Bytes& want)
{
    SENT s;
    if (!TakeOne(&s))
        return false;
    bool b = true;
    if (s.dwSessionID != dwSessionID || s.wOpcode != wOpcode)
    {
        printf("  sent 0x%X to %u, want 0x%X to %u\n", s.wOpcode, s.dwSessionID, wOpcode,
               dwSessionID);
        b = false;
    }
    return Same(s.data, want) && b;
}

// A relayed message to the GlobalManager route: 0x6008 {relay ID, WORD 7, WORD opcode, data}.
static const DWORD GM_SESSION = 900;
static const WORD GM_BODY = 7;

static bool ExpectRelay(WORD wOpcode, const Bytes& want)
{
    SENT s;
    if (!TakeOne(&s))
        return false;
    if (s.dwSessionID != GM_SESSION || s.wOpcode != SERVER_MSG_RELAY_REQ || s.data.size() < 8)
    {
        printf("  sent 0x%X to %u\n", s.wOpcode, s.dwSessionID);
        return false;
    }
    WORD wTarget = *(WORD*)&s.data[4];
    WORD wInner = *(WORD*)&s.data[6];
    Bytes inner(s.data.begin() + 8, s.data.end());
    bool b = wTarget == GM_BODY && wInner == wOpcode;
    if (!b)
        printf("  relay to %u of 0x%X\n", wTarget, wInner);
    return Same(inner, want) && b;
}

static CMsg* NewMsg(DWORD dwSessionID, const Bytes& data)
{
    CMsg* p = g_Net.AllocMsg(FALSE);
    if (!data.empty())
        p->WriteBytes(&data[0], (WORD)data.size());
    p->m_dwSessionID = dwSessionID;
    return p;
}

// ---------------------------------------------------------------------------------------------
// Log lines

static std::vector<CAPTURED_LOG> s_vecLog;

static void ClearLogs()
{
    LogCapture_Drain();
    s_vecLog.clear();
}

static bool HasLog(const std::string& strPrefix, DWORD dwFlags = (DWORD)-1)
{
    std::vector<CAPTURED_LOG> v = LogCapture_Drain();
    s_vecLog.insert(s_vecLog.end(), v.begin(), v.end());
    for (size_t i = 0; i < s_vecLog.size(); ++i)
    {
        if (s_vecLog[i].strText.compare(0, strPrefix.size(), strPrefix) == 0
            && (dwFlags == (DWORD)-1 || s_vecLog[i].dwFlags == dwFlags))
        {
            return true;
        }
    }
    printf("  missing log line: %s\n", strPrefix.c_str());
    return false;
}

// ---------------------------------------------------------------------------------------------
// Query double: CDBQuery::ExecuteFormat formats like the original, then answers from the script.

struct QUERY_ANSWER
{
    int     nResult;
    Bytes   row;
};

struct QUERY_CALL
{
    std::string     strSQL;
    CDBSessionMgr*  pSession;
    int             nSession;
    int             nBufferSize;
    int             nRowSize;
};

static std::deque<QUERY_ANSWER> s_deqAnswer;
static std::vector<QUERY_CALL> s_vecCall;

static void Answer(int nResult, const Bytes& row)
{
    QUERY_ANSWER a;
    a.nResult = nResult;
    a.row = row;
    s_deqAnswer.push_back(a);
}

int CDBQuery::Execute()
{
    QUERY_CALL c;
    c.strSQL = m_szQuery;
    c.pSession = m_pSession;
    c.nSession = m_nSession;
    c.nBufferSize = m_nBufferSize;
    c.nRowSize = m_nRowSize;
    s_vecCall.push_back(c);
    if (s_deqAnswer.empty())
    {
        printf("  unexpected query: %s\n", m_szQuery);
        ++g_nFailed;
        return -2;
    }
    QUERY_ANSWER a = s_deqAnswer.front();
    s_deqAnswer.pop_front();
    if (m_pTrigger)
    {
        m_bRowFetched = 0;
        m_pTrigger->OnTrigger2((DWORD)this, 0);
        if (!a.row.empty())
            memcpy(m_pBuffer, &a.row[0], a.row.size());
        m_bRowFetched = 1;
        m_pTrigger->OnTrigger2((DWORD)this, 0);
    }
    else if (!a.row.empty())
    {
        CHECK((int)a.row.size() <= m_nRowSize);
        memcpy(m_pBuffer, &a.row[0], a.row.size());
    }
    return a.nResult;
}

int CDBQuery::ExecuteFormat(const char* pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf_s(m_szQuery, sizeof(m_szQuery), sizeof(m_szQuery) - 1, pszFormat, args);
    va_end(args);
    return Execute();
}

static CDBSessionMgr* const GATEWAY_DB = (CDBSessionMgr*)0x1111;
static CDBSessionMgr* const ACCOUNT_DB = (CDBSessionMgr*)0x2222;

static std::string MD5Hex(const char* psz)
{
    CMD5 md5;
    md5.Compute(psz);
    return md5.GetHexDigest();
}

static bool ExpectCall(size_t nIndex, const std::string& strSQL, CDBSessionMgr* pSession,
                       int nSession, int nRowSize)
{
    if (nIndex >= s_vecCall.size())
    {
        printf("  query %u missing (%s)\n", (unsigned)nIndex, strSQL.c_str());
        return false;
    }
    const QUERY_CALL& c = s_vecCall[nIndex];
    bool b = c.strSQL == strSQL && c.pSession == pSession && c.nSession == nSession
          && c.nRowSize == nRowSize && c.nBufferSize == nRowSize;
    if (!b)
    {
        printf("  query %u: %s (%p, %d, %d / %d)\n", (unsigned)nIndex, c.strSQL.c_str(),
               c.pSession, c.nSession, c.nRowSize, c.nBufferSize);
    }
    return b;
}

// ---------------------------------------------------------------------------------------------
// The architecture the handlers use

static CERT_DIVISION s_Division;
static SERVER_BODY s_SelfBody;
static SERVER_CORD s_GMCord;
static CERT_MACHINE s_AgentMachine;
static CERT_SHARD s_Shard;
static SERVER_BODY s_AgentBody;
static SHARD_CONDITION* s_pCondition;
static const WORD SHARD_ID = 64;
static const BYTE CONTENT_ID = 22;

static void SetupArchitecture()
{
    memset(&s_Division, 0, sizeof(s_Division));
    s_Division.wGlobalManagerID = GM_BODY;
    memset(&s_SelfBody, 0, sizeof(s_SelfBody));
    s_SelfBody.pDivision = &s_Division;
    s_SelfBody.byDivisionID = 30;
    g_pSelfBody = &s_SelfBody;

    memset(&s_GMCord, 0, sizeof(s_GMCord));
    s_GMCord.dwSessionID = GM_SESSION;
    g_hmapServerRoute[GM_BODY] = &s_GMCord;

    memset(&s_AgentMachine, 0, sizeof(s_AgentMachine));
    strcpy_s(s_AgentMachine.szPublicIP, "121.1.2.3");
    strcpy_s(s_AgentMachine.szPrivateIP, "192.168.0.9");
    memset(&s_Shard, 0, sizeof(s_Shard));
    s_Shard.wID = SHARD_ID;
    s_Shard.ab3[0] = CONTENT_ID;
    strcpy_s((char*)&s_Shard.ab3[1], 32, "Shard");
    s_Shard.w548 = 1000;
    s_Shard.w557 = 10;
    memset(&s_AgentBody, 0, sizeof(s_AgentBody));
    s_AgentBody.wID = 30;
    s_AgentBody.wPort = 15884;
    s_AgentBody.nState = 5;
    s_AgentBody.pShard = &s_Shard;
    s_AgentBody.pMachine = &s_AgentMachine;
    g_mapServerBody[s_AgentBody.wID] = &s_AgentBody;
    g_mapCertShard[SHARD_ID] = &s_Shard;

    s_pCondition = new SHARD_CONDITION;
    s_pCondition->pShard = &s_Shard;
    s_pCondition->byServiceType = SHARD_SERVICE_OPERATING;
    s_pCondition->listAgent.push_back(&s_AgentBody);
    s_pCondition->itNextAgent = s_pCondition->listAgent.end();
    g_ShardConditionManager[SHARD_ID] = s_pCondition;

    g_pCertDataSettingList = new CertDataSettingList;
    g_pGatewayDBSession = GATEWAY_DB;
    g_pAccountDBSession = ACCOUNT_DB;
}

static CTask* NewTask()
{
    CTask* pTask = new CTask;
    pTask->m_pQueue = new CQueMT<CMsg>;
    return pTask;
}

// Every message queued for the task so far.
static std::vector<CMsg*> DrainQueue(CTask* pTask)
{
    std::vector<CMsg*> v;
    for (;;)
    {
        CMsg* p = NULL;
        if (!pTask->m_pQueue->Pop(&p, 0, FALSE) || !p)
            break;
        v.push_back(p);
    }
    return v;
}

static Bytes Payload(CMsg* p)
{
    Bytes v;
    Put(v, p->m_pData + MSG_HEADER_SIZE, p->m_wWritePos - MSG_HEADER_SIZE);
    return v;
}

static const DWORD CLIENT_SESSION = 77;
static const DWORD CLIENT_IP = 0x0A01A8C0;     // 192.168.1.10 (public path)
static const DWORD LAN_IP = 0x1400A8C0;        // 192.168.0.20

static sCertifyParam* NewParam(DWORD dwType = 0)
{
    sCertifyParam* p = CertParam_Alloc();
    p->dwType = dwType;
    p->strID = "tester";
    p->str24 = "secret";
    p->dwSessionID = CLIENT_SESSION;
    p->dw258 = CLIENT_IP;
    p->b40 = 3;
    p->b41 = 3;
    p->dw0 = 4711;
    return p;
}

// ---------------------------------------------------------------------------------------------
// Records and pools

static void TestCertParam()
{
    DWORD dwBefore = g_CertifyParamPool.m_nInUse;
    sCertifyParam* p = CertParam_Alloc();
    CHECK(p != NULL && g_CertifyParamPool.m_nInUse == dwBefore + 1);
    CHECK(g_CertifyParamPool.m_nItemsPerBlock == 500);
    CHECK(g_UserCertifierJobPool.m_nItemsPerBlock == 500 && g_IBUVDataPool.m_nItemsPerBlock == 500);
    CHECK(p->strID.empty() && p->str24.empty() && p->wstrIBUVCode.empty() && !p->pFreeParam1);
    p->dwType = 2;
    p->pFreeParam1 = new sCertifyFreeParam;
    sCertifyParam* pKeep = p;
    CertParam_Release(&p);
    CHECK(p == NULL && g_CertifyParamPool.m_nInUse == dwBefore);
    CHECK(pKeep->pFreeParam1 == NULL);
    // Other contents keep the second record (it is only made for 2 and 24).
    p = CertParam_Alloc();
    CHECK(p != NULL);
    sCertifyFreeParam* pFree = new sCertifyFreeParam;
    p->dwType = 5;
    p->pFreeParam1 = pFree;
    pKeep = p;
    CertParam_Release(&p);
    CHECK(pKeep->pFreeParam1 == pFree);
    delete pFree;
    sCertifyParam* pNull = NULL;
    CertParam_Release(&pNull);
    CHECK(g_CertifyParamPool.m_nInUse == dwBefore);
}

// ---------------------------------------------------------------------------------------------
// Privileged addresses and row triggers

static void TestPrivilegedIP()
{
    ClearLogs();
    // The rows hold the range in network order.
    DWORD adwRange[2] = { inet_addr("10.0.0.254"), inet_addr("10.0.1.1") };
    CDBQuery query;
    query.m_pBuffer = (BYTE*)adwRange;
    query.m_dwParam = 2;
    query.m_bRowFetched = 0;
    g_GatewayServer.OnPrivilegedIPRow((DWORD)&query, 0);
    CHECK(adwRange[0] == 0 && adwRange[1] == 0);
    adwRange[0] = inet_addr("10.0.0.254");
    adwRange[1] = inet_addr("10.0.1.1");
    query.m_bRowFetched = 1;
    CHECK(g_GatewayServer.OnPrivilegedIPRow((DWORD)&query, 0) == 1);
    CHECK(HasLog("previleged ip : 10.0.0.254 - 10.0.1.1", 0));
    CHECK(g_PrivilegedIPManager.m_setIP.size() == 4);
    CHECK(IsPrivilegedIP(inet_addr("10.0.0.254")) && IsPrivilegedIP(inet_addr("10.0.0.255")));
    CHECK(IsPrivilegedIP(inet_addr("10.0.1.0")) && IsPrivilegedIP(inet_addr("10.0.1.1")));
    CHECK(!IsPrivilegedIP(inet_addr("10.0.1.2")) && !IsPrivilegedIP(inet_addr("10.0.0.253")));
    // An inverted range adds nothing but is still logged.
    ClearLogs();
    DWORD adwInverted[2] = { inet_addr("10.9.0.5"), inet_addr("10.9.0.1") };
    query.m_pBuffer = (BYTE*)adwInverted;
    g_GatewayServer.OnPrivilegedIPRow2((DWORD)&query, 0);
    CHECK(g_PrivilegedIPManager2.m_setIP.empty());
    CHECK(HasLog("previleged ip : 10.9.0.5 - 10.9.0.1"));
    DWORD adwOne[2] = { inet_addr("172.16.0.1"), inet_addr("172.16.0.1") };
    query.m_pBuffer = (BYTE*)adwOne;
    g_GatewayServer.OnPrivilegedIPRow2((DWORD)&query, 0);
    CHECK(g_PrivilegedIPManager2.Contains(inet_addr("172.16.0.1")));
    CHECK(!IsPrivilegedIP(inet_addr("172.16.0.1")));
}

static void TestModuleVersionRow()
{
    ModuleVersionList modules;
    ModuleVersionFileList files;
    CDBQuery query;
    query.m_pUserData = &modules;
    query.m_dwParam = 0;
    query.m_bRowFetched = 0;
    CHECK(g_GatewayServer.OnModuleVersionRow((DWORD)&query, 0) == 1);
    CHECK(query.m_nRowSize == sizeof(MODULE_VERSION) && query.m_pBuffer != NULL);
    CHECK(query.m_pBuffer[0] == 0 && query.m_pBuffer[sizeof(MODULE_VERSION) - 1] == 0);
    query.m_pBuffer[6] = 9;
    query.m_bRowFetched = 1;
    CHECK(g_GatewayServer.OnModuleVersionRow((DWORD)&query, 0) == 1);
    CHECK(modules.size() == 1 && modules.front()->byModuleID == 9);
    query.m_pUserData = &files;
    query.m_dwParam = 1;
    query.m_bRowFetched = 0;
    g_GatewayServer.OnModuleVersionRow((DWORD)&query, 0);
    CHECK(query.m_nRowSize == sizeof(MODULE_VERSION_FILE));
    query.m_bRowFetched = 1;
    g_GatewayServer.OnModuleVersionRow((DWORD)&query, 0);
    CHECK(files.size() == 1 && modules.size() == 1);
    operator delete(modules.front());
    operator delete(files.front());
}

// ---------------------------------------------------------------------------------------------
// Captcha managers

static void TestIPBlockMgr()
{
    CIBUVIPBlockMgr* pMgr = CSingletonT<CIBUVIPBlockMgr>::GetInstance();
    CHECK(pMgr == &g_IBUVIPBlockMgr);
    // Without a tolerance nothing is counted or blocked.
    IBUVIPBlockMgr_SetFailureIPTolerance(pMgr, 0);
    for (int i = 0; i < 10; ++i)
        pMgr->AddFailure(0x01020304);
    CHECK(pMgr->m_mapFailure.empty() && !pMgr->IsBlocked(0x01020304));
    IBUVIPBlockMgr_SetFailureIPTolerance(pMgr, 2);
    CHECK(pMgr->m_dwFailureIPTolerance == 2);
    pMgr->AddFailure(0x01020304);
    pMgr->AddFailure(0x01020304);
    CHECK(pMgr->m_mapFailure[0x01020304] == 2 && !pMgr->IsBlocked(0x01020304));
    pMgr->AddFailure(0x01020304);
    CHECK(pMgr->IsBlocked(0x01020304) && !pMgr->IsBlocked(0x01020305));
    // The counts stay (GWCERT-011); a higher tolerance unblocks.
    IBUVIPBlockMgr_SetFailureIPTolerance(pMgr, 3);
    CHECK(!pMgr->IsBlocked(0x01020304));
    IBUVIPBlockMgr_SetFailureIPTolerance(pMgr, 0);
    CHECK(!pMgr->IsBlocked(0x01020304));
    pMgr->m_mapFailure.clear();
}

static void TestTemporalCertParamMgr()
{
    CIBUVTemporalCertParamMgr* pMgr = CSingletonT<CIBUVTemporalCertParamMgr>::GetInstance();
    CHECK(pMgr == &g_IBUVTemporalCertParamMgr);
    sCertifyParam* p1 = NewParam();
    sCertifyParam* p2 = NewParam();
    p2->strID = "other";
    p2->dwSessionID = 78;
    ClearLogs();
    CHECK(pMgr->Register(77, p1));
    CHECK(pMgr->Register(78, p2));
    CHECK(pMgr->FindBySession(77) == p1 && pMgr->FindBySession(78) == p2);
    CHECK(pMgr->FindBySession(79) == NULL);
    CHECK(!pMgr->Register(77, p2));
    CHECK(HasLog("Duplicated IBUV CertParam Registration Request!, User Info (Session: 77, "
                 "Account: tester)", 0x2000001));
    CHECK(pMgr->FindBySession(77) == p1);
    // A duplicate of an entry without record is refused silently.
    CHECK(pMgr->Register(90, NULL));
    ClearLogs();
    CHECK(!pMgr->Register(90, p1));
    LogCapture_Drain();
    CHECK(s_vecLog.empty());
    // Entries do not expire: they stay until the session is lost or answered.
    Sleep(20);
    CHECK(pMgr->FindBySession(78) == p2);
    IBUVTemporalCertParamMgr_Remove(pMgr, 78);
    CHECK(pMgr->FindBySession(78) == NULL);
    pMgr->Remove(78);
    pMgr->Remove(77);
    pMgr->Remove(90);
    CHECK(pMgr->m_mapParam.empty());
    CertParam_Release(&p1);
    CertParam_Release(&p2);
}

// ---------------------------------------------------------------------------------------------
// Certification handlers

static Bytes Row7(BYTE byResult, DWORD dwJID, BYTE b1, BYTE b2)
{
    Bytes v;
    PutB(v, byResult);
    PutD(v, dwJID);
    PutB(v, b1);
    PutB(v, b2);
    return v;
}

static Bytes Row6(DWORD dwJID, BYTE b1, BYTE b2)
{
    Bytes v;
    PutD(v, dwJID);
    PutB(v, b1);
    PutB(v, b2);
    return v;
}

static Bytes BlockRow(const char* pszReason)
{
    Bytes v(0x210, 0);
    memcpy(&v[0], pszReason, strlen(pszReason));
    for (int i = 0; i < 16; ++i)
        v[0x200 + i] = (BYTE)(0xA0 + i);
    return v;
}

static void TestCertifyHandlers()
{
    std::string strHash = MD5Hex("secret");
    std::string strTB = "exec _CertifyTB_User 'tester','" + strHash + "'";
    std::string strKey = std::string("exec _CertifyUser 'tester','") + strHash + "','"
                       + GATEWAY_CERTIFY_USER_KEY + "'";
    sCertifyParam* p = NewParam();

    // _CertifyTB_User: ok.
    s_vecCall.clear();
    Answer(0, Row7(0, 1234, 1, 2));
    CHECK(Certify_TBUser(5, p) == CERTIFY_RESULT_OK);
    CHECK(p->dw0 == 1234 && p->b40 == 1 && p->b41 == 2);
    CHECK(ExpectCall(0, strTB, GATEWAY_DB, 5, 7));
    // Blocked: _QueryBlockGuide on the account database fills the reason.
    s_vecCall.clear();
    Answer(0, Row7(3, 555, 1, 1));
    Answer(0, BlockRow("cheating"));
    memset(p->szBlockReason, 'x', sizeof(p->szBlockReason));
    CHECK(Certify_TBUser(6, p) == CERTIFY_RESULT_BLOCKED);
    CHECK(ExpectCall(1, "exec _QueryBlockGuide 555", ACCOUNT_DB, 6, 0x210));
    CHECK(strcmp(p->szBlockReason, "cheating") == 0 && p->abBlockEnd[15] == 0xAF);
    s_vecCall.clear();
    Answer(0, Row7(3, 555, 1, 1));
    Answer(-2, Bytes());
    CHECK(Certify_TBUser(6, p) == CERTIFY_RESULT_ERROR);
    // Wrong password (logged only by Certify_TBUser), query failure.
    ClearLogs();
    Answer(0, Row7(1, 0, 0, 0));
    CHECK(Certify_TBUser(6, p) == CERTIFY_RESULT_WRONG_PASSWORD);
    CHECK(HasLog("Incorrect ID or PWD: tester", 0x2000000));
    Answer(0, Row7(2, 0, 0, 0));
    CHECK(Certify_TBUser_3(6, p) == CERTIFY_RESULT_WRONG_PASSWORD);
    Answer(0, Row7(2, 0, 0, 0));
    CHECK(Certify_TBUser_4(6, p) == CERTIFY_RESULT_WRONG_PASSWORD);
    Answer(-1, Bytes());
    CHECK(Certify_TBUser(6, p) == CERTIFY_RESULT_ERROR);

    // The 15-byte row also fills w26C / dw270.
    s_vecCall.clear();
    Bytes ext = Row7(0, 99, 3, 3);
    PutW(ext, 0x1234);
    PutW(ext, 0xFFFF);
    PutD(ext, 0xCAFEBABE);
    Answer(0, ext);
    CHECK(Certify_TBUser_Ext(1, p) == CERTIFY_RESULT_OK);
    CHECK(p->dw0 == 99 && p->w26C == 0x1234 && p->dw270 == 0xCAFEBABE);
    CHECK(ExpectCall(0, strTB, GATEWAY_DB, 1, 15));
    Answer(0, ext);
    CHECK(DB_CertifyTBUser_WithBlockGuide(1, p) == CERTIFY_RESULT_OK);

    // _CertifyUser with the publisher key (empty in the repository build).
    s_vecCall.clear();
    Answer(0, Row7(0, 321, 3, 3));
    CHECK(DB_CertifyUser_WithBlockGuide(2, p) == CERTIFY_RESULT_OK && p->dw0 == 321);
    CHECK(ExpectCall(0, strKey, GATEWAY_DB, 2, 7));
    s_vecCall.clear();
    Answer(0, Row6(777, 3, 3));
    CHECK(DB_CertifyUser(3, p) == CERTIFY_RESULT_OK && p->dw0 == 777);
    CHECK(ExpectCall(0, strKey, ACCOUNT_DB, 3, 6));
    Answer(0, Row6(0, 3, 3));
    CHECK(DB_CertifyUser(3, p) == CERTIFY_RESULT_WRONG_PASSWORD);
    Answer(-3, Bytes());
    CHECK(DB_CertifyUser(3, p) == CERTIFY_RESULT_ERROR);
    s_vecCall.clear();
    Answer(0, Row6(888, 127, 127));
    Answer(0, BlockRow("blocked"));
    CHECK(Certify_CertifyUserWithKey(4, p) == CERTIFY_RESULT_BLOCKED);
    CHECK(ExpectCall(0, strKey, ACCOUNT_DB, 4, 6));
    CHECK(ExpectCall(1, "exec _QueryBlockGuide 888", ACCOUNT_DB, 4, 0x210));
    Answer(0, Row6(889, 127, 1));
    CHECK(Certify_CertifyUserWithKey(4, p) == CERTIFY_RESULT_OK && p->dw0 == 889);
    Answer(0, Row6(0, 1, 1));
    CHECK(Certify_CertifyUserWithKey(4, p) == CERTIFY_RESULT_WRONG_PASSWORD);

    // Yahoo "#$" accounts: _CertifyUser_SU with the plain password, then the security group.
    s_vecCall.clear();
    p->strID = "op#$1";
    Bytes su;
    PutB(su, 0);
    PutD(su, 4242);
    Answer(0, su);
    Answer(-1, Bytes());
    p->b40 = p->b41 = 0;
    CHECK(Certify_YahooKoreaUDB(8, p) == CERTIFY_RESULT_OK);
    CHECK(p->dw0 == 4242 && p->b40 == 3 && p->b41 == 3);
    CHECK(ExpectCall(0, "exec _CertifyUser_SU 'op#$1','secret'", ACCOUNT_DB, 8, 5));
    CHECK(ExpectCall(1, "exec _GetUserSecurityGroup 'op#$1'", GATEWAY_DB, 8, 2));
    Bytes su2;
    PutB(su2, 2);
    PutD(su2, 0);
    Answer(0, su2);
    CHECK(Certify_YahooKoreaUDB(8, p) == CERTIFY_RESULT_2);
    p->strID = "tester";

    // The division selects the handler.
    s_vecCall.clear();
    s_SelfBody.byDivisionID = 30;
    Answer(0, Row6(31, 3, 3));
    CHECK(UserCertifier_GetHandler(9, p) == CERTIFY_RESULT_OK && p->dw0 == 31);
    CHECK(ExpectCall(0, strKey, ACCOUNT_DB, 9, 6));
    s_SelfBody.byDivisionID = 6;
    Answer(0, Row7(0, 32, 3, 3));
    CHECK(UserCertifier_GetHandler(9, p) == CERTIFY_RESULT_OK && p->dw0 == 32);
    CHECK(ExpectCall(1, strTB, GATEWAY_DB, 9, 7));
    ClearLogs();
    s_SelfBody.byDivisionID = 37;
    CHECK(UserCertifier_GetHandler(9, p) == CERTIFY_RESULT_ERROR);
    CHECK(HasLog("There is no certification handler!!! you must register it first!!! [Div ID: "
                 "37]", 0x2000000));
    s_SelfBody.byDivisionID = 30;
    CHECK(s_deqAnswer.empty());
    CertParam_Release(&p);

    std::string strHead, strTail;
    std::string strIn("a\x05" "bc\x05" "d");
    CHECK(String_SplitAtDelimiter(&strIn, &strHead, &strIn) && strHead == "a"
          && strIn == "bc\x05" "d");
    CHECK(!String_SplitAtDelimiter(&strHead, &strTail, &strTail));
}

// ---------------------------------------------------------------------------------------------
// Certifier jobs

static CGatewayMainProcess* s_pMain;
static CTask* s_pMainTask;
static std::vector<CTask*> s_vecCertifierTask;

static void SetupProcesses()
{
    s_pMainTask = NewTask();
    s_pMain = new CGatewayMainProcess;
    s_pMain->m_pOwner = s_pMainTask;
    s_pMainTask->m_desc.pJob = s_pMain;
    for (int i = 0; i < 30; ++i)
    {
        CTask* pTask = NewTask();
        s_vecCertifierTask.push_back(pTask);
        g_Net.m_mapTask[GATEWAY_TASK_CERTIFIER_FIRST + i] = pTask;
    }
}

static void TestProcessPool()
{
    CHECK(UserCertifier_InitProcessOverlapPool());
    CHECK(g_Net.m_Registered.size() == 30);
    CHECK(g_Net.m_Registered.front() == 0x0100000A && g_Net.m_Registered.back() == 0x01000027);
    CHECK(g_CertifierProcessPool.mapTask.size() == 30);
    CHECK(g_CertifierProcessPool.itNext == g_CertifierProcessPool.mapTask.begin());
    // The cursor moves before it is read: the first job goes to the second process.
    DWORD dwID;
    CHECK(UserCertifier_NextProcess(&dwID) == s_vecCertifierTask[1] && dwID == 0x0100000B);
    for (int i = 2; i < 30; ++i)
        UserCertifier_NextProcess(&dwID);
    CHECK(dwID == 0x01000027);
    CHECK(UserCertifier_NextProcess(&dwID) == s_vecCertifierTask[0] && dwID == 0x0100000A);
    UserCertifier_AddTimingStat(10);
    UserCertifier_AddTimingStat(30);
    CHECK(g_CertifierProcessPool.dwElapsedSum == 40 && g_CertifierProcessPool.dwElapsedCount == 2);
    g_CertifierProcessPool.dwElapsedSum = 0;
    g_CertifierProcessPool.dwElapsedCount = 0;
}

// A job set up as UserCertifier_StartJob does, for the flow tests.
static CUserCertifier_OverlapJob* NewJob(BYTE byContent = CONTENT_ID)
{
    CUserCertifier_OverlapJob* pJob = UserCertifier_AllocJob();
    g_Net.m_mapPeerIP[CLIENT_SESSION] = CLIENT_IP;
    CHECK(pJob->Setup(s_pMainTask, 4, CLIENT_SESSION, "tester", "secret", byContent, SHARD_ID));
    // Setup leaves the security groups of the pooled record as they were.
    pJob->m_pCertParam->b40 = 3;
    pJob->m_pCertParam->b41 = 3;
    return pJob;
}

static void FinishJob(CUserCertifier_OverlapJob* pJob)
{
    if (pJob->m_pCertParam)
        CertParam_Release(&pJob->m_pCertParam);
    pJob->ReturnToPool();
}

static void TestStartJob()
{
    g_Net.m_mapPeerIP[CLIENT_SESSION] = CLIENT_IP;
    DWORD dwJobs = g_UserCertifierJobPool.m_nInUse;
    DWORD dwParams = g_CertifyParamPool.m_nInUse;
    CHECK(UserCertifier_StartJob(s_pMainTask, CLIENT_SESSION, "tester", "secret", 2, SHARD_ID)
          == 0);
    CHECK(g_UserCertifierJobPool.m_nInUse == dwJobs + 1);
    CHECK(g_CertifyParamPool.m_nInUse == dwParams + 1);
    // 0x6009 {job} went to the second certifier process.
    std::vector<CMsg*> v = DrainQueue(s_vecCertifierTask[1]);
    CHECK(v.size() == 1);
    if (v.size() == 1)
    {
        CHECK(v[0]->GetOpcode() == SERVER_MSG_OVERLAP_JOB_REQ);
        CUserCertifier_OverlapJob* pJob = *(CUserCertifier_OverlapJob**)(v[0]->m_pData + 6);
        CHECK(pJob->m_pOwner == s_pMainTask && pJob->m_dwProcessIndex == 1);
        CHECK(pJob->m_wShardID == SHARD_ID && pJob->m_dwID != 0);
        sCertifyParam* p = pJob->m_pCertParam;
        CHECK(p->strID == "tester" && p->str24 == "secret" && p->dwType == 2);
        CHECK(p->dwSessionID == CLIENT_SESSION && p->dw258 == CLIENT_IP);
        CHECK(p->pFreeParam1 != NULL);
        CHECK(s_pMain->m_OverlapJobManager.Remove(pJob->m_dwID) == pJob);
        FinishJob(pJob);
        CHECK(g_CertifyParamPool.m_nInUse == dwParams && g_UserCertifierJobPool.m_nInUse == dwJobs);
        g_Net.ReleaseMsg(v[0]);
    }
    // No client address: the job goes back, 3.
    g_Net.m_mapPeerIP.erase(CLIENT_SESSION);
    dwJobs = g_UserCertifierJobPool.m_nInUse;
    CHECK(UserCertifier_StartJob(s_pMainTask, CLIENT_SESSION, "tester", "secret", 0, SHARD_ID)
          == CERTIFY_RESULT_ERROR);
    CHECK(g_UserCertifierJobPool.m_nInUse == dwJobs);
    CHECK(DrainQueue(s_vecCertifierTask[2]).empty());
    g_Net.m_mapPeerIP[CLIENT_SESSION] = CLIENT_IP;
}

static void TestJobPrepareExecute()
{
    CUserCertifier_OverlapJob* pJob = NewJob();
    CHECK(pJob->Prepare());
    g_Net.m_nSocketState = -1;
    CHECK(!pJob->Prepare());
    g_Net.m_nSocketState = 1;
    g_Net.m_mapPeerIP[CLIENT_SESSION] = CLIENT_IP + 1;
    g_Net.m_Disconnected.clear();
    CHECK(!pJob->Prepare());
    CHECK(g_Net.m_Disconnected.size() == 1 && g_Net.m_Disconnected[0] == CLIENT_SESSION);
    g_Net.m_mapPeerIP[CLIENT_SESSION] = CLIENT_IP;

    s_vecCall.clear();
    Answer(0, Row6(5150, 3, 3));
    CHECK(pJob->Execute() && pJob->m_nResult == CERTIFY_RESULT_OK);
    CHECK(pJob->m_pCertParam->dw0 == 5150);
    CHECK(s_vecCall.size() == 1 && s_vecCall[0].nSession == 4);
    Answer(0, Row6(0, 3, 3));
    CHECK(!pJob->Execute() && pJob->m_nResult == CERTIFY_RESULT_WRONG_PASSWORD);
    FinishJob(pJob);
}

static void TestJobOnFail()
{
    // Wrong password: 0x6111 {ID, 0} to the GlobalManager; the record is the relay context.
    CUserCertifier_OverlapJob* pJob = NewJob();
    pJob->m_nResult = CERTIFY_RESULT_WRONG_PASSWORD;
    sCertifyParam* p = pJob->m_pCertParam;
    DWORD dwSum = g_CertifierProcessPool.dwElapsedCount;
    CHECK(pJob->OnFail());
    CHECK(g_CertifierProcessPool.dwElapsedCount == dwSum + 1);
    Bytes want;
    PutS(want, "tester");
    PutB(want, 0);
    CHECK(ExpectRelay(GATEWAY_MSG_VALIDATION_FAIL_REQ, want));
    CHECK(pJob->m_pCertParam == NULL);
    COverlapJob* pRelay = NULL;
    for (OverlapJobMap::iterator it = s_pMain->m_OverlapJobManager.m_mapJob.begin();
         it != s_pMain->m_OverlapJobManager.m_mapJob.end(); ++it)
    {
        pRelay = it->second;
    }
    CHECK(pRelay && ((COverlapJob_RelayMsg*)pRelay)->m_dwContext == (DWORD)p);
    if (pRelay)
        s_pMain->m_OverlapJobManager.RemoveAndRelease(pRelay);
    CertParam_Release(&p);
    FinishJob(pJob);

    // Relay failure: 0xA102 {2, 6}.
    pJob = NewJob();
    pJob->m_nResult = CERTIFY_RESULT_WRONG_PASSWORD;
    g_hmapServerRoute.erase(GM_BODY);
    ClearLogs();
    CHECK(pJob->OnFail());
    CHECK(HasLog("[MultiprocessUserCertifier] RelayMsg Failed", 0x2000000));
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 06")));
    CHECK(pJob->m_pCertParam == NULL);
    g_hmapServerRoute[GM_BODY] = &s_GMCord;
    FinishJob(pJob);

    // Result 2, no account, error codes.
    pJob = NewJob();
    pJob->m_nResult = CERTIFY_RESULT_2;
    ClearLogs();
    CHECK(pJob->OnFail());
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 02 02")));
    FinishJob(pJob);
    pJob = NewJob();
    pJob->m_nResult = CERTIFY_RESULT_NO_ACCOUNT;
    CHECK(pJob->OnFail());
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 02 03")));
    CHECK(HasLog("there is no account info [tester]", 0x1000000));
    FinishJob(pJob);
    for (int nResult = 3; nResult <= 7; nResult += 4)
    {
        pJob = NewJob();
        pJob->m_nResult = nResult;
        ClearLogs();
        CHECK(pJob->OnFail());
        CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 06")));
        char sz[80];
        sprintf_s(sz, "cannot certify user : unstable server condition (%d)", nResult);
        CHECK(HasLog(sz, 0x2000000));
        FinishJob(pJob);
    }
    // Blocked: reason string and the 16 bytes after it.
    pJob = NewJob();
    pJob->m_nResult = CERTIFY_RESULT_BLOCKED;
    memset(pJob->m_pCertParam->szBlockReason, 0, sizeof(pJob->m_pCertParam->szBlockReason));
    strcpy_s(pJob->m_pCertParam->szBlockReason, "abuse");
    for (int i = 0; i < 16; ++i)
        pJob->m_pCertParam->abBlockEnd[i] = (BYTE)i;
    ClearLogs();
    CHECK(pJob->OnFail());
    want.clear();
    PutB(want, 2);
    PutB(want, 2);
    PutB(want, 1);
    PutS(want, "abuse");
    for (int i = 0; i < 16; ++i)
        PutB(want, (BYTE)i);
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, want));
    CHECK(HasLog("user blocked [tester]", 0x1000000));
    FinishJob(pJob);
    // A session that went away gets nothing.
    pJob = NewJob();
    pJob->m_nResult = CERTIFY_RESULT_2;
    g_Net.m_nSocketState = -1;
    CHECK(!pJob->OnFail());
    CHECK(g_Net.m_Sent.empty() && pJob->m_pCertParam != NULL);
    g_Net.m_nSocketState = 1;
    FinishJob(pJob);
}

static void TestJobOnComplete()
{
    // Accepted: 0x6200 {address, JID, content} relayed with the record as context.
    CUserCertifier_OverlapJob* pJob = NewJob();
    sCertifyParam* p = pJob->m_pCertParam;
    p->dw0 = 1001;
    p->b40 = 3;
    p->b41 = 3;
    CHECK(pJob->OnComplete());
    Bytes want;
    PutD(want, CLIENT_IP);
    PutD(want, 1001);
    PutB(want, CONTENT_ID);
    CHECK(ExpectRelay(GATEWAY_MSG_LOGIN_GRANT_REQ, want));
    CHECK(p->pAgentBody == &s_AgentBody && p->wPort == 0x4567 && pJob->m_pCertParam == NULL);
    s_pMain->m_OverlapJobManager.ReleaseAll();
    CertParam_Release(&p);
    FinishJob(pJob);

    // No AgentServer in state 5: 4.
    s_AgentBody.nState = 4;
    pJob = NewJob();
    CHECK(!pJob->OnComplete());
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 04")));
    CHECK(pJob->m_pCertParam == NULL);
    FinishJob(pJob);
    s_AgentBody.nState = 5;
    // Full shard: 5.
    s_Shard.w557 = s_Shard.w548;
    pJob = NewJob();
    CHECK(!pJob->OnComplete());
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 05")));
    FinishJob(pJob);
    s_Shard.w557 = 10;
    // GM account from an unprivileged address: 13.
    pJob = NewJob();
    pJob->m_pCertParam->b40 = 1;
    pJob->m_pCertParam->b41 = 1;
    ClearLogs();
    CHECK(!pJob->OnComplete());
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 0d")));
    CHECK(HasLog("[tester] login refused (GM cannot login from the PC that has not privileged ip)"
                 " - 192.168.1.10", 0x2000000));
    FinishJob(pJob);
    // ... from a privileged one: accepted.
    DWORD adwRange[2] = { CLIENT_IP, CLIENT_IP };
    g_PrivilegedIPManager.AddRange(adwRange);
    pJob = NewJob();
    pJob->m_pCertParam->b40 = 1;
    pJob->m_pCertParam->b41 = 1;
    p = pJob->m_pCertParam;
    CHECK(pJob->OnComplete());
    g_Net.m_Sent.clear();
    s_pMain->m_OverlapJobManager.ReleaseAll();
    CertParam_Release(&p);
    FinishJob(pJob);
    g_PrivilegedIPManager.m_setIP.erase(CLIENT_IP);

    // Age checked contents (shard content 2 / 24).
    s_Shard.ab3[0] = 2;
    pJob = NewJob(2);
    pJob->m_pCertParam->pFreeParam1->nAge = 14;
    CHECK(!pJob->OnComplete());
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 0a")));
    FinishJob(pJob);
    pJob = NewJob(2);
    pJob->m_pCertParam->pFreeParam1->nAge = 17;
    CHECK(!pJob->OnComplete());
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 0a")));
    FinishJob(pJob);
    strcpy_s((char*)&s_Shard.ab3[1], 32, "Test#$T");
    pJob = NewJob(2);
    pJob->m_pCertParam->pFreeParam1->nAge = 14;
    CHECK(!pJob->OnComplete());
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 0b")));
    FinishJob(pJob);
    pJob = NewJob(2);
    pJob->m_pCertParam->pFreeParam1->nAge = 17;
    p = pJob->m_pCertParam;
    CHECK(pJob->OnComplete());
    g_Net.m_Sent.clear();
    s_pMain->m_OverlapJobManager.ReleaseAll();
    CertParam_Release(&p);
    FinishJob(pJob);
    strcpy_s((char*)&s_Shard.ab3[1], 32, "Shard");
    // Login content 24 is not age checked; content without the record: 6.
    pJob = NewJob(24);
    pJob->m_pCertParam->pFreeParam1->nAge = 3;
    p = pJob->m_pCertParam;
    CHECK(pJob->OnComplete());
    g_Net.m_Sent.clear();
    s_pMain->m_OverlapJobManager.ReleaseAll();
    CertParam_Release(&p);
    FinishJob(pJob);
    pJob = NewJob(5);
    ClearLogs();
    CHECK(!pJob->OnComplete());
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 06")));
    CHECK(HasLog("[MultiprocessUserCertifier] m_pCertParam == NULL || "
                 "m_pCertParam->dwFreeParam1 == NULL"));
    FinishJob(pJob);
    s_Shard.ab3[0] = CONTENT_ID;
    // Relay failure: {2, 6}, the record is dropped (GWCERT-002).
    g_hmapServerRoute.erase(GM_BODY);
    pJob = NewJob();
    DWORD dwParams = g_CertifyParamPool.m_nInUse;
    CHECK(pJob->OnComplete());
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 06")));
    CHECK(pJob->m_pCertParam == NULL && g_CertifyParamPool.m_nInUse == dwParams);
    g_hmapServerRoute[GM_BODY] = &s_GMCord;
    FinishJob(pJob);
}

// ---------------------------------------------------------------------------------------------
// Login request and answer of the main process

static CMsg* LoginRequest(BYTE byContent, const char* pszID, const char* pszPassword,
                          WORD wShard)
{
    Bytes v;
    PutB(v, byContent);
    PutS(v, pszID);
    PutS(v, pszPassword);
    PutW(v, wShard);
    return NewMsg(CLIENT_SESSION, v);
}

static void Login(BYTE byContent, const char* pszID, WORD wShard)
{
    CMsg* pMsg = LoginRequest(byContent, pszID, "pw'\"", wShard);
    s_pMain->OnMsg_6102(pMsg, 0, 0, NULL);
    g_Net.ReleaseMsg(pMsg);
}

static void TestLoginRequest()
{
    g_Net.m_mapPeerIP[CLIENT_SESSION] = CLIENT_IP;
    // Unknown shard, wrong content, no address: {2, 7}.
    Login(CONTENT_ID, "Tester", 99);
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 07")));
    Login(CONTENT_ID + 1, "Tester", SHARD_ID);
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 07")));
    g_Net.m_mapPeerIP.erase(CLIENT_SESSION);
    Login(CONTENT_ID, "Tester", SHARD_ID);
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 07")));
    g_Net.m_mapPeerIP[CLIENT_SESSION] = CLIENT_IP;
    // Address blocked for failed captchas: {2, 6}.
    g_IBUVIPBlockMgr.SetFailureIPTolerance(1);
    g_IBUVIPBlockMgr.AddFailure(CLIENT_IP);
    g_IBUVIPBlockMgr.AddFailure(CLIENT_IP);
    Login(CONTENT_ID, "Tester", SHARD_ID);
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 06")));
    g_IBUVIPBlockMgr.SetFailureIPTolerance(0);
    g_IBUVIPBlockMgr.m_mapFailure.clear();
    // Shard not in service: {2, 2, 2}, unless the client is privileged.
    s_pCondition->byServiceType = SHARD_SERVICE_EXAMINATION;
    Login(CONTENT_ID, "Tester", SHARD_ID);
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 02 02")));
    DWORD adwRange[2] = { CLIENT_IP, CLIENT_IP };
    g_PrivilegedIPManager.AddRange(adwRange);
    Login(CONTENT_ID, "Tester", SHARD_ID);
    CHECK(g_Net.m_Sent.empty());
    g_PrivilegedIPManager.m_setIP.erase(CLIENT_IP);
    s_pCondition->byServiceType = SHARD_SERVICE_OPERATING;
    // Accepted: no answer, the job is on a certifier process with the lower-case name and the
    // quotes of the name replaced.
    Login(CONTENT_ID, "Te'St", SHARD_ID);
    CHECK(g_Net.m_Sent.empty());
    int nJobs = 0;
    int nRenamed = 0;
    for (size_t i = 0; i < s_vecCertifierTask.size(); ++i)
    {
        std::vector<CMsg*> v = DrainQueue(s_vecCertifierTask[i]);
        for (size_t j = 0; j < v.size(); ++j)
        {
            CUserCertifier_OverlapJob* pJob = *(CUserCertifier_OverlapJob**)(v[j]->m_pData + 6);
            ++nJobs;
            if (pJob->m_pCertParam->strID == "te st")
            {
                ++nRenamed;
                CHECK(pJob->m_pCertParam->str24 == "pw'\"");
                CHECK(pJob->m_pCertParam->dwType == CONTENT_ID);
            }
            s_pMain->m_OverlapJobManager.Remove(pJob->m_dwID);
            FinishJob(pJob);
            g_Net.ReleaseMsg(v[j]);
        }
    }
    CHECK(nJobs == 2 && nRenamed == 1);
    // The job cannot be started: {2, 6}.
    for (size_t i = 0; i < s_vecCertifierTask.size(); ++i)
        s_vecCertifierTask[i]->m_pQueue->Stop();
    Login(CONTENT_ID, "Tester", SHARD_ID);
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 06")));
    for (size_t i = 0; i < s_vecCertifierTask.size(); ++i)
        s_vecCertifierTask[i]->m_pQueue->Start();
}

static void TestLoginGranted()
{
    g_Net.m_Disconnected.clear();
    // Public client: public address of the AgentServer.
    sCertifyParam* p = NewParam();
    DWORD dwParams = g_CertifyParamPool.m_nInUse;
    Bytes v;
    PutB(v, 1);
    PutD(v, 0xDEADBEEF);
    PutW(v, s_AgentBody.wID);
    CMsg* pMsg = NewMsg(GM_SESSION, v);
    s_pMain->OnMsg_A203(pMsg, 0, (DWORD)p, NULL);
    g_Net.ReleaseMsg(pMsg);
    Bytes want;
    PutB(want, 1);
    PutD(want, 0xDEADBEEF);
    PutS(want, "121.1.2.3");
    PutW(want, 15884);
    SENT s;
    CHECK(TakeOne(&s) && s.dwSessionID == CLIENT_SESSION && s.wOpcode == GATEWAY_MSG_LOGIN_ACK);
    CHECK(Same(s.data, want) && s.bEncrypted);
    CHECK(g_CertifyParamPool.m_nInUse == dwParams - 1 && g_Net.m_Disconnected.empty());
    // Client in 192.168.0.x: private address.
    p = NewParam();
    p->dw258 = LAN_IP;
    pMsg = NewMsg(GM_SESSION, v);
    s_pMain->OnMsg_A203(pMsg, 0, (DWORD)p, NULL);
    g_Net.ReleaseMsg(pMsg);
    want.clear();
    PutB(want, 1);
    PutD(want, 0xDEADBEEF);
    PutS(want, "192.168.0.9");
    PutW(want, 15884);
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, want));
    // Refused: {2, 6}, closed, released.
    p = NewParam();
    dwParams = g_CertifyParamPool.m_nInUse;
    Bytes refused;
    PutB(refused, 2);
    pMsg = NewMsg(GM_SESSION, refused);
    s_pMain->OnMsg_A203(pMsg, 0, (DWORD)p, NULL);
    g_Net.ReleaseMsg(pMsg);
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 06")));
    CHECK(g_Net.m_Disconnected.size() == 1 && g_Net.m_Disconnected[0] == CLIENT_SESSION);
    CHECK(g_CertifyParamPool.m_nInUse == dwParams - 1);
    g_Net.m_Disconnected.clear();
    // Without a context nothing happens.
    pMsg = NewMsg(GM_SESSION, refused);
    s_pMain->OnMsg_A203(pMsg, 0, 0, NULL);
    g_Net.ReleaseMsg(pMsg);
    CHECK(g_Net.m_Sent.empty());
}

// ---------------------------------------------------------------------------------------------
// IBUV process

// The CRT's rand(): every thread starts at seed 1.
struct RAND_MODEL
{
    DWORD dwSeed;
    int Next()
    {
        dwSeed = dwSeed * 214013 + 2531011;
        return (int)((dwSeed >> 16) & 0x7FFF);
    }
};

struct RENDER_CALL
{
    std::wstring    wstrText;
    long            an[11];
};

static std::vector<RENDER_CALL> s_vecRender;
static int s_nRenderResult = -1;    // -1: the size computed from the parameters

// Stand-in for ImageTrans.dll: the image is a function of the text and the parameters.
static int __cdecl FakeImageTransToMemW(BYTE* pBuffer, long cbBuffer, long nWidth, long nHeight,
                                        wchar_t* pszText, long n5, long n6, long n7, long n8,
                                        long n9, long n10, long n11)
{
    RENDER_CALL c;
    c.wstrText = pszText;
    long an[11] = { cbBuffer, nWidth, nHeight, n5, n6, n7, n8, n9, n10, n11, 0 };
    memcpy(c.an, an, sizeof(an));
    s_vecRender.push_back(c);
    if (s_nRenderResult >= 0)
        return s_nRenderResult;
    DWORD dw = 2166136261u;
    for (const wchar_t* p = pszText; *p; ++p)
        dw = (dw ^ *p) * 16777619u;
    for (int i = 0; i < 10; ++i)
        dw = (dw ^ (DWORD)an[i]) * 16777619u;
    int nSize = 100 + (int)(dw % 900);
    for (int i = 0; i < nSize; ++i)
    {
        dw = dw * 1103515245u + 12345u;
        pBuffer[i] = (BYTE)(dw >> 16);
    }
    return nSize;
}

static const wchar_t CHARSET[] = L"ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static CIBUVProcess* s_pIBUV;
static CTask* s_pIBUVTask;

struct GENERATED
{
    std::wstring    wstrCode;
    Bytes           image;
};

static GENERATED TakeImage()
{
    GENERATED g;
    IBUVData* pData = s_pIBUV->m_deqImage.back();
    g.wstrCode = pData->m_wstrCode;
    Put(g.image, pData->m_abImage, pData->m_wImageSize);
    return g;
}

static void ClearImages()
{
    while (!s_pIBUV->m_deqImage.empty())
    {
        IBUVData* pData = s_pIBUV->m_deqImage.front();
        s_pIBUV->m_deqImage.pop_front();
        pData->Release();
    }
}

static DWORD WINAPI GenerateOnNewThread(LPVOID pParam)
{
    *(BOOL*)pParam = s_pIBUV->GenerateAndQueueImage();
    return 0;
}

static void TestIBUVInitialize()
{
    CIBUVProcess* pProcess = new CIBUVProcess;
    s_pIBUV = pProcess;
    CHECK(CSingletonT<CIBUVProcess>::GetInstance() == pProcess);
    CHECK(IBUVProcess_GetRuntimeClass() == &CIBUVProcess::classCIBUVProcess);
    CHECK(strcmp(CIBUVProcess::classCIBUVProcess.m_lpszClassName, "CIBUVProcess") == 0);
    CHECK(CIBUVProcess::classCIBUVProcess.m_nObjectSize == 0x402F4);
    CHECK(pProcess->m_dwCodePage == 949 && pProcess->m_nStringSize == 6);
    CHECK(pProcess->m_dwQueueReserveCount == 20000 && pProcess->m_fQueuePrepareRatio == 0.05f);
    CHECK(!pProcess->m_bEnabled);
    ClearLogs();
    CHECK(!IBUVProcess_Initialize(pProcess, s_pMainTask, 100, 0.5f, 7, "ABC"));
    CHECK(HasLog("IBUV: Too Long Image String Size : 7 !", 0x2000001));
    ClearLogs();
    CHECK(!IBUVProcess_Initialize(pProcess, s_pMainTask, 100, 0.5f, 5, "ABC"));
    CHECK(HasLog("IBUV: Failed To Load ImageTrans.dll", 0x2000001));
    CHECK(pProcess->m_nStringSize == 5 && pProcess->m_dwCodePage == 1258);
    CHECK(!pProcess->m_bEnabled);
    // While disabled nothing is made (the 20000 images of Initialize, GWCERT-004).
    pProcess->m_pfnImageTransToMemW = FakeImageTransToMemW;
    CHECK(!pProcess->GenerateAndQueueImage() && s_vecRender.empty());

    s_pIBUVTask = NewTask();
    pProcess->CServerProcessBase::Initialize(s_pIBUVTask, NULL);
    CHECK(pProcess->GetOwner() == s_pIBUVTask);
    s_pIBUVTask->m_desc.pJob = pProcess;
    pProcess->m_pOwnerTask = s_pMainTask;
    pProcess->m_nStringSize = 6;
    wcscpy_s(pProcess->m_szCharSet, CHARSET);
    pProcess->m_bEnabled = TRUE;
}

static void TestIBUVDeterminism()
{
    const int nCharSet = (int)wcslen(CHARSET);
    srand(1234);
    CHECK(s_pIBUV->GenerateAndQueueImage());
    GENERATED a1 = TakeImage();
    CHECK(s_pIBUV->GenerateAndQueueImage());
    GENERATED a2 = TakeImage();
    srand(1234);
    CHECK(s_pIBUV->GenerateAndQueueImage());
    GENERATED b1 = TakeImage();
    CHECK(s_pIBUV->GenerateAndQueueImage());
    GENERATED b2 = TakeImage();
    CHECK(a1.wstrCode == b1.wstrCode && a1.image == b1.image && !a1.image.empty());
    CHECK(a2.wstrCode == b2.wstrCode && a2.image == b2.image);
    CHECK(a1.wstrCode != a2.wstrCode);

    // The order of the rand() calls: one per character, then the five distortion values.
    RAND_MODEL m;
    m.dwSeed = 1234;
    std::wstring wstrWant;
    for (int i = 0; i < 6; ++i)
        wstrWant += CHARSET[m.Next() % nCharSet];
    long n7 = m.Next() % 21 - 10;
    long n8 = m.Next() % 21 + 100;
    long n9 = m.Next() % 41 + 20;
    long n10 = m.Next() % 601 - 300;
    long n11 = m.Next() % 2 + 2;
    CHECK(a1.wstrCode == wstrWant);
    const RENDER_CALL& c = s_vecRender[s_vecRender.size() - 4];
    CHECK(c.wstrText == wstrWant);
    CHECK(c.an[0] == 0x800 && c.an[1] == 200 && c.an[2] == 64 && c.an[3] == 0 && c.an[4] == 0);
    CHECK(c.an[5] == n7 && c.an[6] == n8 && c.an[7] == n9 && c.an[8] == 100 * n10);
    CHECK(c.an[9] == n11);
    IBUVData* pData = s_pIBUV->m_deqImage.front();
    CHECK(pData->m_wstrAnswer == pData->m_wstrCode && pData->m_b4 == 0 && pData->m_bInUse);
    ClearImages();

    // The IBUV thread's rand() is never seeded (GWCERT-003): its codes are those of seed 1.
    BOOL bOk = FALSE;
    HANDLE hThread = CreateThread(NULL, 0, GenerateOnNewThread, &bOk, 0, NULL);
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    CHECK(bOk);
    RAND_MODEL one;
    one.dwSeed = 1;
    std::wstring wstrSeed1;
    for (int i = 0; i < 6; ++i)
        wstrSeed1 += CHARSET[one.Next() % nCharSet];
    CHECK(s_pIBUV->m_deqImage.size() == 1 && TakeImage().wstrCode == wstrSeed1);
    ClearImages();

    // Failed drawing: logged, the record is lost to the pool (GWCERT-010).
    DWORD dwInUse = g_IBUVDataPool.m_nInUse;
    s_nRenderResult = 0;
    ClearLogs();
    CHECK(!s_pIBUV->GenerateAndQueueImage());
    CHECK(HasLog("IBUV: Failed To Create Image!!!", 0x2000001));
    s_nRenderResult = 0x801;
    CHECK(!s_pIBUV->GenerateAndQueueImage());
    CHECK(s_pIBUV->m_deqImage.empty() && g_IBUVDataPool.m_nInUse == dwInUse + 2);
    s_nRenderResult = -1;
}

static void TestIBUVTick()
{
    ClearImages();
    s_pIBUV->m_dwQueueReserveCount = 10;
    s_pIBUV->m_fQueuePrepareRatio = 0.5f;
    s_pIBUV->m_dwLastTick = GetTickCount();
    s_pIBUV->TickCreateImages();
    CHECK(s_pIBUV->m_deqImage.empty());
    s_pIBUV->m_dwLastTick = GetTickCount() - 200;
    s_pIBUV->TickCreateImages();
    CHECK(s_pIBUV->m_deqImage.size() == 5);
    s_pIBUV->m_dwLastTick -= 200;
    s_pIBUV->TickCreateImages();
    CHECK(s_pIBUV->m_deqImage.size() == 7);     // 0.5 * 5 = 2.5 -> 2
    s_pIBUV->m_fQueuePrepareRatio = 0.01f;
    s_pIBUV->m_dwLastTick -= 200;
    s_pIBUV->TickCreateImages();
    CHECK(s_pIBUV->m_deqImage.size() == 8);     // at least one
    s_pIBUV->m_dwQueueReserveCount = 8;
    s_pIBUV->m_dwLastTick -= 200;
    s_pIBUV->TickCreateImages();
    CHECK(s_pIBUV->m_deqImage.size() == 8);
    s_pIBUV->m_bEnabled = FALSE;
    s_pIBUV->m_dwQueueReserveCount = 20;
    s_pIBUV->m_dwLastTick -= 200;
    s_pIBUV->TickCreateImages();
    CHECK(s_pIBUV->m_deqImage.size() == 8);
    s_pIBUV->m_bEnabled = TRUE;
}

static void TestIBUVImageRequest()
{
    // RequestIBUVImage: 0x6321 {record} to the IBUV queue.
    sCertifyParam* p = NewParam();
    RequestIBUVImage(p);
    std::vector<CMsg*> v = DrainQueue(s_pIBUVTask);
    CHECK(v.size() == 1);
    if (v.size() != 1)
        return;
    CHECK(v[0]->GetOpcode() == IBUV_MSG_IMAGE_REQ && Payload(v[0]).size() == 4);
    CHECK(*(sCertifyParam**)(v[0]->m_pData + 6) == p);

    // From the queue: 0xA321 {record, 1, BYTE 0, code, size, image} to the main process.
    IBUVData* pFront = s_pIBUV->m_deqImage.front();
    std::wstring wstrCode = pFront->m_wstrAnswer;
    Bytes image(pFront->m_abImage, pFront->m_abImage + pFront->m_wImageSize);
    size_t nQueued = s_pIBUV->m_deqImage.size();
    DWORD dwInUse = g_IBUVDataPool.m_nInUse;
    s_pIBUV->Dispatch(v[0], 0, 0, NULL);
    g_Net.ReleaseMsg(v[0]);
    CHECK(s_pIBUV->m_deqImage.size() == nQueued - 1 && g_IBUVDataPool.m_nInUse == dwInUse - 1);
    CHECK(!pFront->m_bInUse);
    std::vector<CMsg*> acks = DrainQueue(s_pMainTask);
    CHECK(acks.size() == 1);
    Bytes want;
    PutD(want, (DWORD)p);
    PutB(want, 1);
    PutB(want, 0);
    PutWS(want, wstrCode.c_str());
    PutW(want, (WORD)image.size());
    Put(want, &image[0], image.size());
    if (acks.size() == 1)
    {
        CHECK(acks[0]->GetOpcode() == IBUV_MSG_IMAGE_ACK && Same(Payload(acks[0]), want));
        // The main process: the code waits, the client gets 0x2322 {0, size, image}.
        s_pMain->OnMsg_A321(acks[0], 0, 0, NULL);
        g_Net.ReleaseMsg(acks[0]);
        Bytes toUser;
        PutB(toUser, 0);
        PutW(toUser, (WORD)image.size());
        Put(toUser, &image[0], image.size());
        CHECK(Expect(CLIENT_SESSION, IBUV_MSG_IMAGE_TO_USER, toUser));
        CHECK(g_IBUVTemporalCertParamMgr.FindBySession(CLIENT_SESSION) == p);
        CHECK(p->wstrIBUVCode == wstrCode);
    }

    // Empty queue: one image is made on demand ...
    ClearImages();
    ClearLogs();
    Bytes req;
    PutD(req, 0x12345678);
    CMsg* pReq = NewMsg(0, req);
    pReq->SetOpcode(IBUV_MSG_IMAGE_REQ);
    s_pIBUV->Dispatch(pReq, 0, 0, NULL);
    g_Net.ReleaseMsg(pReq);
    CHECK(HasLog("IBUV: Not enough preserved images. generated..", 0x2000001));
    acks = DrainQueue(s_pMainTask);
    CHECK(acks.size() == 1 && Payload(acks[0])[4] == 1);
    for (size_t i = 0; i < acks.size(); ++i)
        g_Net.ReleaseMsg(acks[i]);
    // ... or the request fails: {record, 2}.
    s_nRenderResult = 0;
    ClearLogs();
    pReq = NewMsg(0, req);
    pReq->SetOpcode(IBUV_MSG_IMAGE_REQ);
    s_pIBUV->Dispatch(pReq, 0, 0, NULL);
    g_Net.ReleaseMsg(pReq);
    s_nRenderResult = -1;
    CHECK(HasLog("IBUV: Failed to get Image!!!!", 0x2000001));
    acks = DrainQueue(s_pMainTask);
    CHECK(acks.size() == 1 && Same(Payload(acks[0]), B("78 56 34 12 02")));
    for (size_t i = 0; i < acks.size(); ++i)
        g_Net.ReleaseMsg(acks[i]);
    // Unknown opcodes are skipped and logged.
    ClearLogs();
    CMsg* pOther = NewMsg(0, req);
    pOther->SetOpcode(0x7777);
    s_pIBUV->Dispatch(pOther, 0, 0, NULL);
    CHECK(pOther->GetUnreadSize() == 0);
    g_Net.ReleaseMsg(pOther);
    CHECK(HasLog("CIBUVProcess::Unhandled Msg: 0x7777", 0x1000000));
}

static CMsg* ImageAck(sCertifyParam* p, BYTE byResult)
{
    Bytes v;
    PutD(v, (DWORD)p);
    PutB(v, byResult);
    if (byResult == 1)
    {
        PutB(v, 0);
        PutWS(v, L"XY7");
        PutW(v, 3);
        PutB(v, 0xAA);
        PutB(v, 0xBB);
        PutB(v, 0xCC);
    }
    CMsg* pMsg = NewMsg(0, v);
    pMsg->SetOpcode(IBUV_MSG_IMAGE_ACK);
    return pMsg;
}

static void TestImageAck()
{
    g_IBUVTemporalCertParamMgr.m_mapParam.clear();
    sCertifyParam* p = NewParam();
    CMsg* pMsg = ImageAck(p, 1);
    s_pMain->OnMsg_A321(pMsg, 0, 0, NULL);
    g_Net.ReleaseMsg(pMsg);
    CHECK(Expect(CLIENT_SESSION, IBUV_MSG_IMAGE_TO_USER, B("00 03 00 aa bb cc")));
    CHECK(p->wstrIBUVCode == L"XY7");
    // Second image for the same session: refused, closed, released.
    sCertifyParam* p2 = NewParam();
    DWORD dwParams = g_CertifyParamPool.m_nInUse;
    ClearLogs();
    pMsg = ImageAck(p2, 1);
    s_pMain->OnMsg_A321(pMsg, 0, 0, NULL);
    g_Net.ReleaseMsg(pMsg);
    SENT s;
    CHECK(TakeOne(&s) && s.wOpcode == GATEWAY_MSG_LOGIN_ACK && Same(s.data, B("02 06")));
    CHECK(s.bEncrypted);
    CHECK(HasLog("Duplicated IBUV CertParam Registration Request!, User Info (Session: 77, "
                 "Account: tester)", 0x2000001));
    CHECK(g_Net.m_Disconnected.size() == 1 && g_CertifyParamPool.m_nInUse == dwParams - 1);
    g_Net.m_Disconnected.clear();
    CHECK(g_IBUVTemporalCertParamMgr.FindBySession(CLIENT_SESSION) == p);
    // Failed image: refused, closed, released.
    g_IBUVTemporalCertParamMgr.Remove(CLIENT_SESSION);
    ClearLogs();
    pMsg = ImageAck(p, 2);
    s_pMain->OnMsg_A321(pMsg, 0, 0, NULL);
    g_Net.ReleaseMsg(pMsg);
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 06")));
    CHECK(HasLog("IBUV GENERATE Failed, User Info (4711, tester)", 0x2000001));
    CHECK(g_Net.m_Disconnected.size() == 1);
    g_Net.m_Disconnected.clear();
    // No record.
    ClearLogs();
    pMsg = ImageAck(NULL, 1);
    s_pMain->OnMsg_A321(pMsg, 0, 0, NULL);
    g_Net.ReleaseMsg(pMsg);
    CHECK(HasLog("Invalid CertParam Pointer in OnFRAMEWORKMSG_IBUV_GENERATE_ACK", 0x2000001));
    CHECK(g_Net.m_Sent.empty());
}

static void Answer6323(const char* pszAnswer)
{
    Bytes v;
    PutS(v, pszAnswer);
    CMsg* pMsg = NewMsg(CLIENT_SESSION, v);
    s_pMain->OnMsg_6323(pMsg, 0, 0, NULL);
    g_Net.ReleaseMsg(pMsg);
}

static void TestCaptchaAnswer()
{
    // No pending captcha: {2, 6}, logged, closed.
    g_IBUVTemporalCertParamMgr.m_mapParam.clear();
    ClearLogs();
    Answer6323("ABC");
    SENT s;
    CHECK(TakeOne(&s) && s.wOpcode == GATEWAY_MSG_LOGIN_ACK && Same(s.data, B("02 06")));
    CHECK(s.bEncrypted);
    CHECK(HasLog("FRAMEWORKMSG_IMAGE_BASED_USER_VALIDATE_REQ Failed, No Registered CertParam!!",
                 0x2000001));
    CHECK(g_Net.m_Disconnected.size() == 1);
    g_Net.m_Disconnected.clear();

    // Right answer: 0xA323 {1}, 0x6112 {ID} to the GlobalManager, the code is consumed.
    sCertifyParam* p = NewParam();
    p->wstrIBUVCode = L"AB7";
    g_IBUVTemporalCertParamMgr.Register(CLIENT_SESSION, p);
    Answer6323("AB7");
    CHECK(g_Net.m_Sent.size() == 2);
    if (g_Net.m_Sent.size() == 2)
    {
        CHECK(g_Net.m_Sent[0].wOpcode == GATEWAY_MSG_IBUV_ANSWER_ACK);
        CHECK(Same(g_Net.m_Sent[0].data, B("01")) && !g_Net.m_Sent[0].bEncrypted);
        g_Net.m_Sent.erase(g_Net.m_Sent.begin());
    }
    Bytes want;
    PutS(want, "tester");
    CHECK(ExpectRelay(GATEWAY_MSG_VALIDATION_OK_REQ, want));
    CHECK(p->wstrIBUVCode.empty() && !g_IBUVTemporalCertParamMgr.FindBySession(CLIENT_SESSION));
    s_pMain->m_OverlapJobManager.ReleaseAll();

    // Wrong answer (case sensitive): one failure for the address, 0x6111 {ID, 1}.
    g_IBUVIPBlockMgr.SetFailureIPTolerance(5);
    p->wstrIBUVCode = L"AB7";
    g_IBUVTemporalCertParamMgr.Register(CLIENT_SESSION, p);
    Answer6323("ab7");
    want.clear();
    PutS(want, "tester");
    PutB(want, 1);
    CHECK(ExpectRelay(GATEWAY_MSG_VALIDATION_FAIL_REQ, want));
    CHECK(g_IBUVIPBlockMgr.m_mapFailure[CLIENT_IP] == 1);
    s_pMain->m_OverlapJobManager.ReleaseAll();
    // Relay failures: {2, 6}; the right answer also closes and releases.
    g_hmapServerRoute.erase(GM_BODY);
    p->wstrIBUVCode = L"AB7";
    g_IBUVTemporalCertParamMgr.Register(CLIENT_SESSION, p);
    ClearLogs();
    Answer6323("AB");
    CHECK(Expect(CLIENT_SESSION, GATEWAY_MSG_LOGIN_ACK, B("02 06")));
    CHECK(HasLog("[OnFRAMEWORKMSG_IMAGE_BASED_USER_VALIDATE_REQ] RelayMsg Failed", 0x2000000));
    CHECK(g_Net.m_Disconnected.empty() && g_IBUVIPBlockMgr.m_mapFailure[CLIENT_IP] == 2);
    DWORD dwParams = g_CertifyParamPool.m_nInUse;
    p->wstrIBUVCode = L"AB7";
    g_IBUVTemporalCertParamMgr.Register(CLIENT_SESSION, p);
    Answer6323("AB7");
    CHECK(g_Net.m_Sent.size() == 2);
    if (g_Net.m_Sent.size() == 2)
    {
        CHECK(g_Net.m_Sent[1].wOpcode == GATEWAY_MSG_LOGIN_ACK);
        CHECK(Same(g_Net.m_Sent[1].data, B("02 06")) && !g_Net.m_Sent[1].bEncrypted);
    }
    g_Net.m_Sent.clear();
    CHECK(g_Net.m_Disconnected.size() == 1 && g_CertifyParamPool.m_nInUse == dwParams - 1);
    g_Net.m_Disconnected.clear();
    g_hmapServerRoute[GM_BODY] = &s_GMCord;
    g_IBUVIPBlockMgr.SetFailureIPTolerance(0);
    g_IBUVIPBlockMgr.m_mapFailure.clear();
}

// Prints the name of each part, so a crash shows where it happened.
#define RUN(fn) (printf("%s\n", #fn), fflush(stdout), fn())

int main()
{
    g_pBSNet = &g_Net;
    LogCapture_Start();
    SetupArchitecture();
    SetupProcesses();

    RUN(TestCertParam);
    RUN(TestPrivilegedIP);
    RUN(TestModuleVersionRow);
    RUN(TestIPBlockMgr);
    RUN(TestTemporalCertParamMgr);
    RUN(TestCertifyHandlers);
    RUN(TestProcessPool);
    RUN(TestStartJob);
    RUN(TestJobPrepareExecute);
    RUN(TestJobOnFail);
    RUN(TestJobOnComplete);
    RUN(TestLoginRequest);
    RUN(TestLoginGranted);
    RUN(TestIBUVInitialize);
    RUN(TestIBUVDeterminism);
    RUN(TestIBUVTick);
    RUN(TestIBUVImageRequest);
    RUN(TestImageAck);
    RUN(TestCaptchaAnswer);

    return TEST_RESULT("GatewayCertTest");
}
