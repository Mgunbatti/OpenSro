// CServiceManagerProcess: control of the server bodies and shards (0x7005..0x7012, 0x7202,
// 0x7203, 0x7308) and the answers of the relayed requests.

#include "ServiceManagerProcess.h"

#include <string.h>
#include <strsafe.h>

#include "GlobalManagerDB.h"
#include "GlobalManagerSvcFwd.h"
#include "BSLib/BSDebug.h"
#include "BSLib/DB/DBQuery.h"
#include "BSNet/IBSNet.h"
#include "ServerFramework/CertArch.h"
#include "ServerFramework/ServerDataExtern.h"
#include "ServerFramework/ServerProcess.h"
#include "ServerFramework/Trigger.h"

using namespace ServerFramework;

static CServerProcessBase* TaskProcess(CTask* pTask)
{
    return (CServerProcessBase*)pTask->GetJob();
}

static CMsg* NewMsg(WORD wOpcode)
{
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(wOpcode);
    return pMsg;
}

static void SendAndRelease(DWORD dwSessionID, CMsg* pMsg)
{
    g_pBSNet->SendMsg(dwSessionID, pMsg);
    g_pBSNet->ReleaseMsg(pMsg);
}

static void WriteByte(CMsg* pMsg, BYTE b)
{
    pMsg->WriteBytes(&b, 1);
}

// Map53B744_Find (0x46c250)
static CERT_SHARD* FindCertShard(WORD wShardID)
{
    std::map<WORD, CERT_SHARD*>::iterator it = g_mapCertShard.find(wShardID);
    if (it == g_mapCertShard.end())
        return NULL;
    return it->second;
}

// ---------------------------------------------------------------------------------------------
// 0x7005 / 0xA300: ping a body (0x6300); the answer has the round trip time.

void CServiceManagerProcess::OnMsg_7005(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    WORD wBodyID;
    *pMsg >> wBodyID;
    sSMDefault_RelayMsg_Param* pParam = SMRelayParamPool_Alloc();
    pParam->dwSessionID = pMsg->m_dwSessionID;
    pParam->dw8 = GetTickCount();
    pParam->wBodyID = wBodyID;
    CMsg* pRelay = NewMsg(0x6300);
    if (!RelayMsgToBody_6008(TaskProcess(m_pTask), pRelay, wBodyID, 0, 0, (DWORD)pParam))
    {
        CMsg* pReply = NewMsg(0xB005);
        WriteByte(pReply, 2);
        pReply->WriteBytes(&pParam->wBodyID, 2);
        SendAndRelease(pParam->dwSessionID, pReply);
        pParam->Release();
    }
}

void CServiceManagerProcess::OnMsg_A300(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    sSMDefault_RelayMsg_Param* pParam = (sSMDefault_RelayMsg_Param*)dwContext;
    BYTE byResult;
    *pMsg >> byResult;
    DWORD dwElapsed = GetTickCount() - pParam->dw8;
    CMsg* pReply = NewMsg(0xB005);
    WriteByte(pReply, 1);
    pReply->WriteBytes(&pParam->wBodyID, 2);
    pReply->WriteBytes(&dwElapsed, 4);
    SendAndRelease(pParam->dwSessionID, pReply);
    pParam->Release();
}

// ---------------------------------------------------------------------------------------------
// 0x7006 / 0x7007 / 0x7308: launch, shut down or stop a body (0x6301 / 0x6302 to its
// MachineManager, 0x631D to the body).

// The common part of 0x7006 and 0x7007.
static void RelayToMachineManager(CServiceManagerProcess* pThis, CMsg* pMsg, WORD wRelayOpcode,
                                  WORD wReplyOpcode, const char* pszLog)
{
    WORD wBodyID;
    *pMsg >> wBodyID;
    pThis->WriteLog(pMsg->m_dwSessionID, 13, pszLog, wBodyID);
    SERVER_BODY* pBody = ServerBodyInfo_Find(wBodyID);
    if (!pBody)
    {
        CMsg* pReply = NewMsg(wReplyOpcode);
        WriteByte(pReply, 2);
        WriteByte(pReply, 4);
        pReply->WriteBytes(&wBodyID, 2);
        SendAndRelease(pMsg->m_dwSessionID, pReply);
        return;
    }
    sSMDefault_RelayMsg_Param* pParam = SMRelayParamPool_Alloc();
    pParam->dwSessionID = pMsg->m_dwSessionID;
    pParam->wBodyID = wBodyID;
    pParam->dw8 = 0;
    CMsg* pRelay = NewMsg(wRelayOpcode);
    pRelay->WriteBytes(&pBody->byModuleID, 1);
    if (!RelayMsgToBody_6008(TaskProcess(pThis->m_pTask), pRelay,
                             pBody->pMachine->wMachineManagerID, 0, 0, (DWORD)pParam))
    {
        CMsg* pReply = NewMsg(wReplyOpcode);
        WriteByte(pReply, 2);
        WriteByte(pReply, 3);
        pReply->WriteBytes(&wBodyID, 2);
        SendAndRelease(pMsg->m_dwSessionID, pReply);
        pParam->Release();
    }
}

void CServiceManagerProcess::OnMsg_7006(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    RelayToMachineManager(this, pMsg, 0x6301, 0xB006, "Launch Server REQ : %d");
}

void CServiceManagerProcess::OnMsg_7007(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    RelayToMachineManager(this, pMsg, 0x6302, 0xB007, "Shutdown Server REQ : %d");
}

void CServiceManagerProcess::OnMsg_7308(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    WORD wBodyID;
    *pMsg >> wBodyID;
    WriteLog(pMsg->m_dwSessionID, 13, "Shutdown Server REQ : %d", wBodyID);
    if (!ServerBodyInfo_Find(wBodyID))
    {
        CMsg* pReply = NewMsg(0xB308);
        WriteByte(pReply, 2);
        WriteByte(pReply, 4);
        pReply->WriteBytes(&wBodyID, 2);
        SendAndRelease(pMsg->m_dwSessionID, pReply);
        return;
    }
    sSMDefault_RelayMsg_Param* pParam = SMRelayParamPool_Alloc();
    pParam->dwSessionID = pMsg->m_dwSessionID;
    pParam->wBodyID = wBodyID;
    pParam->dw8 = 0;
    CMsg* pRelay = NewMsg(0x631D);
    if (!RelayMsgToBody_6008(TaskProcess(m_pTask), pRelay, wBodyID, 0, 0, (DWORD)pParam))
    {
        CMsg* pReply = NewMsg(0xB308);
        WriteByte(pReply, 2);
        WriteByte(pReply, 3);
        pReply->WriteBytes(&wBodyID, 2);
        SendAndRelease(pMsg->m_dwSessionID, pReply);
        pParam->Release();
    }
}

// The answers 0xA301 / 0xA302 / 0xA31D {BYTE result, BYTE error when 2}.
static void AnswerBodyCommand(CMsg* pMsg, DWORD dwContext, WORD wReplyOpcode)
{
    sSMDefault_RelayMsg_Param* pParam = (sSMDefault_RelayMsg_Param*)dwContext;
    BYTE byResult;
    *pMsg >> byResult;
    CMsg* pReply;
    if (byResult == 2)
    {
        *pMsg >> byResult;
        pReply = NewMsg(wReplyOpcode);
        WriteByte(pReply, 2);
        pReply->WriteBytes(&byResult, 1);
    }
    else
    {
        pReply = NewMsg(wReplyOpcode);
        WriteByte(pReply, 1);
    }
    pReply->WriteBytes(&pParam->wBodyID, 2);
    SendAndRelease(pParam->dwSessionID, pReply);
    pParam->Release();
}

void CServiceManagerProcess::OnMsg_A301(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    AnswerBodyCommand(pMsg, dwContext, 0xB006);
}

void CServiceManagerProcess::OnMsg_A302(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    AnswerBodyCommand(pMsg, dwContext, 0xB007);
}

void CServiceManagerProcess::OnMsg_A31D(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    AnswerBodyCommand(pMsg, dwContext, 0xB308);
}

// ---------------------------------------------------------------------------------------------
// 0x7008 / 0xA303: service mode of a body (0x6303).

void CServiceManagerProcess::OnMsg_7008(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    BYTE byOperation;
    WORD wBodyID;
    *pMsg >> byOperation;
    *pMsg >> wBodyID;
    WriteLog(pMsg->m_dwSessionID, 13, "Operate Service REQ : operation[%d], server_id[%d]",
             byOperation, wBodyID);
    if (!ServerBodyInfo_Find(wBodyID))
    {
        CMsg* pReply = NewMsg(0xB008);
        WriteByte(pReply, 2);
        WriteByte(pReply, 2);
        pReply->WriteBytes(&byOperation, 1);
        pReply->WriteBytes(&wBodyID, 2);
        SendAndRelease(pMsg->m_dwSessionID, pReply);
        return;
    }
    sSMDefault_RelayMsg_Param* pParam = SMRelayParamPool_Alloc();
    pParam->dwSessionID = pMsg->m_dwSessionID;
    pParam->dw8 = byOperation;
    pParam->wBodyID = wBodyID;
    CMsg* pRelay = NewMsg(0x6303);
    pRelay->WriteBytes(&byOperation, 1);
    if (!RelayMsgToBody_6008(TaskProcess(m_pTask), pRelay, wBodyID, 0, 0, (DWORD)pParam))
    {
        CMsg* pReply = NewMsg(0xB008);
        WriteByte(pReply, 2);
        WriteByte(pReply, 1);
        pReply->WriteBytes(&byOperation, 1);
        pReply->WriteBytes(&wBodyID, 2);
        SendAndRelease(pMsg->m_dwSessionID, pReply);
        pParam->Release();
    }
}

void CServiceManagerProcess::OnMsg_A303(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    sSMDefault_RelayMsg_Param* pParam = (sSMDefault_RelayMsg_Param*)dwContext;
    BYTE byResult;
    BYTE byCheck;
    BYTE byMode;
    *pMsg >> byResult;
    *pMsg >> byCheck;
    *pMsg >> byMode;
    CMsg* pReply = NewMsg(0xB008);
    BYTE byOperation = (BYTE)pParam->dw8;
    pReply->WriteBytes(&byResult, 1);
    pReply->WriteBytes(&byCheck, 1);
    pReply->WriteBytes(&byOperation, 1);
    pReply->WriteBytes(&pParam->wBodyID, 2);
    SendAndRelease(pParam->dwSessionID, pReply);
    pParam->Release();
}

// ---------------------------------------------------------------------------------------------
// 0x7009 / 0xA304: status of a body in service (0x6304).

void CServiceManagerProcess::OnMsg_7009(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    WORD wBodyID;
    *pMsg >> wBodyID;
    SERVER_BODY* pBody = ServerBodyInfo_Find(wBodyID);
    if (!pBody || pBody->nState != 5)
    {
        CMsg* pReply = NewMsg(0xB009);
        WriteByte(pReply, 2);
        WriteByte(pReply, 1);
        pReply->WriteBytes(&wBodyID, 2);
        SendAndRelease(pMsg->m_dwSessionID, pReply);
        return;
    }
    CMsg* pRelay = NewMsg(0x6304);
    sSMDefault_RelayMsg_Param* pParam = SMRelayParamPool_Alloc();
    pParam->dwSessionID = pMsg->m_dwSessionID;
    pParam->wBodyID = wBodyID;
    if (!RelayMsgToBody_6008(TaskProcess(m_pTask), pRelay, wBodyID, 0, 0, (DWORD)pParam))
    {
        CMsg* pReply = NewMsg(0xB009);
        WriteByte(pReply, 2);
        WriteByte(pReply, 2);
        pReply->WriteBytes(&wBodyID, 2);
        SendAndRelease(pMsg->m_dwSessionID, pReply);
        pParam->Release();
    }
}

void CServiceManagerProcess::OnMsg_A304(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    sSMDefault_RelayMsg_Param* pParam = (sSMDefault_RelayMsg_Param*)dwContext;
    BYTE byResult;
    *pMsg >> byResult;
    CMsg* pReply;
    if (byResult == 2)
    {
        pReply = NewMsg(0xB009);
        WriteByte(pReply, 2);
        WriteByte(pReply, 3);
        pReply->WriteBytes(&pParam->wBodyID, 2);
    }
    else
    {
        pReply = NewMsg(0xB009);
        WriteByte(pReply, 1);
        pReply->WriteBytes(&pParam->wBodyID, 2);
        pReply->WriteBytes(pMsg->m_pData + pMsg->m_wReadPos,
                           (WORD)(pMsg->m_wWritePos - pMsg->m_wReadPos));
    }
    SendAndRelease(pParam->dwSessionID, pReply);
    pMsg->SkipRemaining();
    pParam->Release();
}

// ---------------------------------------------------------------------------------------------
// 0x7202 / 0xA307: 0x6307 to every GatewayServer.

void CServiceManagerProcess::OnMsg_7202(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    ServerBodyList* pList = g_pServerBodyList;
    for (ServerBodyList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        SERVER_BODY* pBody = *it;
        if (pBody->pModule != CertModule_FindByName("GatewayServer"))
            continue;
        CMsg* pRelay = NewMsg(0x6307);
        sSMDefault_RelayMsg_Param* pParam = SMRelayParamPool_Alloc();
        pParam->dwSessionID = pMsg->m_dwSessionID;
        pParam->wBodyID = pBody->wID;
        if (!RelayMsgToBody_6008(TaskProcess(m_pTask), pRelay, pBody->wID, 0, 0, (DWORD)pParam))
        {
            // BUG(GM-001): the context of the failed relay is not released.
            CMsg* pReply = NewMsg(0xB202);
            pReply->WriteBytes(&pBody->wID, 2);
            WriteByte(pReply, 2);
            SendAndRelease(pMsg->m_dwSessionID, pReply);
        }
    }
}

void CServiceManagerProcess::OnMsg_A307(CMsg*, DWORD, DWORD dwContext, CMassiveMsg*)
{
    // BUG(GM-001): the context is not released either.
    sSMDefault_RelayMsg_Param* pParam = (sSMDefault_RelayMsg_Param*)dwContext;
    CMsg* pReply = NewMsg(0xB202);
    pReply->WriteBytes(&pParam->wBodyID, 2);
    WriteByte(pReply, 1);
    SendAndRelease(pParam->dwSessionID, pReply);
}

// ---------------------------------------------------------------------------------------------
// 0x7203: a request for a body, {WORD body, WORD opcode, data}; relayed as <opcode> {DWORD
// account, string account name, data}. The answers go through OnUnhandledAnswer.

void CServiceManagerProcess::OnMsg_7203(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    WORD wBodyID;
    WORD wOpcode;
    *pMsg >> wBodyID;
    *pMsg >> wOpcode;
    CMsg* pRelay = NewMsg(wOpcode);
    sServiceManagerUser* pUser = FindUser(pMsg->m_dwSessionID);
    if (!pUser)
        BSAssertFail();
    pRelay->WriteBytes(&pUser->dwID, 4);
    *pRelay << pUser->strID;
    pRelay->WriteBytes(pMsg->m_pData + pMsg->m_wReadPos,
                       (WORD)(pMsg->m_wWritePos - pMsg->m_wReadPos));
    sSMDefault_RelayMsg_Param* pParam = SMRelayParamPool_Alloc();
    pParam->dwSessionID = pMsg->m_dwSessionID;
    pParam->dw8 = wOpcode;
    pParam->wBodyID = wBodyID;
    if (RelayMsgToBody_6008(TaskProcess(m_pTask), pRelay, wBodyID, 0, 0, (DWORD)pParam))
    {
        pMsg->SkipRemaining();
        return;
    }
    CMsg* pReply = NewMsg(0xB203);
    WriteByte(pReply, 2);
    pReply->WriteBytes(&wBodyID, 2);
    pReply->WriteBytes(&wOpcode, 2);
    SendAndRelease(pParam->dwSessionID, pReply);
    pParam->Release();
    pMsg->SkipRemaining();
}

// ---------------------------------------------------------------------------------------------
// 0x7010 / 0xA310: shard name (type 0) or user limit; 0x6310 to the GlobalManager's parent,
// whose answer is passed to the bodies certified here (0x2311) and to every SMC.

void CServiceManagerProcess::OnMsg_7010(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    WORD wShardID;
    BYTE byType;
    std::string strName;
    *pMsg >> wShardID;
    *pMsg >> byType;
    CServerProcessBase* pProcess = TaskProcess(m_pTask);
    if (byType == 0)
    {
        *pMsg >> strName;
        if (strName.size() > 16)
        {
            LogPrintf(0x1000000, "Changing Shard Name is Failed (length:%d)", strName.size());
            CMsg* pReply = NewMsg(0xB010);
            WriteByte(pReply, 2);
            SendAndRelease(pMsg->m_dwSessionID, pReply);
            return;
        }
        CERT_SHARD* pShard = FindCertShard(wShardID);
        if (!pShard)
        {
            CMsg* pReply = NewMsg(0xB010);
            WriteByte(pReply, 2);
            SendAndRelease(pMsg->m_dwSessionID, pReply);
            return;
        }
        WriteLog(pMsg->m_dwSessionID, 13, "Change Shard Name %d : %s -> %s", pShard->wID,
                 (const char*)pShard->ab3 + 1, strName.c_str());
        CMsg* pRelay = NewMsg(0x6310);
        pRelay->WriteBytes(&wShardID, 2);
        pRelay->WriteBytes(&byType, 1);
        *pRelay << strName.c_str();
        if (!RelayMsgToBody_6008(pProcess, pRelay, g_pSelfBody->wParentID, 0, 0,
                                 pMsg->m_dwSessionID))
        {
            CMsg* pReply = NewMsg(0xB010);
            WriteByte(pReply, 2);
            SendAndRelease(pMsg->m_dwSessionID, pReply);
        }
        return;
    }
    WORD wMaxUser;
    *pMsg >> wMaxUser;
    CERT_SHARD* pShard = FindCertShard(wShardID);
    if (pShard)
    {
        WriteLog(pMsg->m_dwSessionID, 13, "Change Shard Max User %d[%s] : %d -> %d",
                 pShard->wID, (const char*)pShard->ab3 + 1, pShard->w548, wMaxUser);
        CMsg* pRelay = NewMsg(0x6310);
        pRelay->WriteBytes(&wShardID, 2);
        pRelay->WriteBytes(&byType, 1);
        pRelay->WriteBytes(&wMaxUser, 2);
        if (RelayMsgToBody_6008(pProcess, pRelay, g_pSelfBody->wParentID, 0, 0,
                                pMsg->m_dwSessionID))
        {
            return;
        }
    }
    CMsg* pReply = NewMsg(0xB010);
    WriteByte(pReply, 2);
    SendAndRelease(pMsg->m_dwSessionID, pReply);
}

// Sends the message to every body certified here except this one; releases it when there is
// no route.
static void SendToChildBodies(CMsg* (*pfnBuild)(void*), void* pParam)
{
    ServerBodyList* pList = g_pServerBodyList;
    for (ServerBodyList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        SERVER_BODY* pBody = *it;
        if (pBody == g_pSelfBody || pBody->wParentID != g_pSelfBody->wID)
            continue;
        CMsg* pMsg = pfnBuild(pParam);
        if (!ServerBody_SendMsg(pBody->wID, pMsg))
            g_pBSNet->ReleaseMsg(pMsg);
    }
}

struct SHARD_SETTING
{
    WORD        wShardID;
    BYTE        byType;
    WORD        wMaxUser;
    const char* pszName;
};

static CMsg* BuildShardSetting(void* pParam)
{
    const SHARD_SETTING* p = (const SHARD_SETTING*)pParam;
    CMsg* pMsg = NewMsg(0x2311);
    pMsg->WriteBytes(&p->wShardID, 2);
    pMsg->WriteBytes(&p->byType, 1);
    if (p->byType)
        pMsg->WriteBytes(&p->wMaxUser, 2);
    else
        *pMsg << p->pszName;
    return pMsg;
}

void CServiceManagerProcess::OnMsg_A310(CMsg* pMsg, DWORD, DWORD dwContext, CMassiveMsg*)
{
    // The context of 0x6310 is the SMC session.
    BYTE byResult;
    *pMsg >> byResult;
    if (byResult == 2)
    {
        CMsg* pReply = NewMsg(0xB010);
        WriteByte(pReply, 2);
        SendAndRelease(dwContext, pReply);
        return;
    }
    std::string strName;
    SHARD_SETTING setting;
    *pMsg >> setting.wShardID;
    *pMsg >> setting.byType;
    CERT_SHARD* pShard = FindCertShard(setting.wShardID);
    if (!pShard)
    {
        pMsg->SkipRemaining();
        CMsg* pReply = NewMsg(0xB010);
        WriteByte(pReply, 2);
        SendAndRelease(dwContext, pReply);
        return;
    }
    CMsg* pReply;
    if (setting.byType)
    {
        *pMsg >> setting.wMaxUser;
        pShard->w548 = setting.wMaxUser;
        SendToChildBodies(BuildShardSetting, &setting);
        pReply = NewMsg(0xB010);
        WriteByte(pReply, 1);
        pReply->WriteBytes(&setting.wShardID, 2);
        pReply->WriteBytes(&setting.byType, 1);
        pReply->WriteBytes(&setting.wMaxUser, 2);
    }
    else
    {
        *pMsg >> strName;
        if (strName.size() >= 0x20)
            BSAssertFail();
        char* pszShardName = (char*)pShard->ab3 + 1;
        if (FAILED(StringCchCopyA(pszShardName, 32, strName.c_str())))
            BSAssertFail();
        setting.pszName = pszShardName;
        SendToChildBodies(BuildShardSetting, &setting);
        pReply = NewMsg(0xB010);
        WriteByte(pReply, 1);
        pReply->WriteBytes(&setting.wShardID, 2);
        pReply->WriteBytes(&setting.byType, 1);
        *pReply << (const char*)pszShardName;
    }
    BroadcastToUsers(pReply);
}

void CServiceManagerProcess::OnMsg_A312(CMsg*, DWORD, DWORD, CMassiveMsg*)
{
}

// ---------------------------------------------------------------------------------------------
// 0x7011 / 0x7012: shard service types (_ShardService of the division DB).

void CServiceManagerProcess::OnMsg_7011(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    WORD wShardID;
    BYTE byType;
    *pMsg >> wShardID;
    *pMsg >> byType;
    WriteLog(pMsg->m_dwSessionID, 13, "Change Shard Service Type : %d, %d", wShardID, byType);
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pDefaultDBSession;
    query.m_nSession = 1;
    if (query.ExecuteFormat("update _ShardService set ServiceType = %d where ShardID = %d",
                            byType, wShardID) < 0)
    {
        CMsg* pReply = NewMsg(0xB011);
        WriteByte(pReply, 2);
        SendAndRelease(pMsg->m_dwSessionID, pReply);
        return;
    }
    ServerBodyList* pList = g_pServerBodyList;
    for (ServerBodyList::iterator it = pList->begin(); it != pList->end(); ++it)
    {
        SERVER_BODY* pBody = *it;
        if (pBody->pModule != CertModule_FindByName("GatewayServer"))
            continue;
        CMsg* pRelay = NewMsg(0x6312);
        pRelay->WriteBytes(&wShardID, 2);
        RelayMsgToBody_6008(TaskProcess(m_pTask), pRelay, pBody->wID, 0, 0, 0);
    }
    CMsg* pReply = NewMsg(0xB011);
    WriteByte(pReply, 1);
    pReply->WriteBytes(&wShardID, 2);
    pReply->WriteBytes(&byType, 1);
    BroadcastToUsers(pReply);
}

void CServiceManagerProcess::OnMsg_7012(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    CMsg* pReply = NewMsg(0xB012);
    WriteByte(pReply, 1);
    CClassTrigger<CServiceManagerProcess> trigger(this);
    trigger.m_pfnTrigger2 = &CServiceManagerProcess::OnShardServiceRow;
    BYTE abRow[3];
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    query.m_pSession = g_pDefaultDBSession;
    query.m_nSession = 1;
    query.m_pBuffer = abRow;
    query.m_nBufferSize = 3;
    query.m_nRowSize = 3;
    query.m_dwParam = (DWORD)pReply;
    query.m_pTrigger = &trigger;
    if (query.ExecuteFormat("select * from _ShardService") >= 0)
    {
        WriteByte(pReply, 0);
    }
    else
    {
        g_pBSNet->ReleaseMsg(pReply);
        pReply = NewMsg(0xB012);
        WriteByte(pReply, 2);
    }
    SendAndRelease(pMsg->m_dwSessionID, pReply);
}

// Each row {WORD shard, BYTE type} as BYTE 1 and the row.
int CServiceManagerProcess::OnShardServiceRow(DWORD dwQuery, DWORD)
{
    CDBQuery* pQuery = (CDBQuery*)dwQuery;
    if (!pQuery->m_bRowFetched)
    {
        memset(pQuery->m_pBuffer, 0, pQuery->m_nRowSize);
        return 1;
    }
    CMsg* pReply = (CMsg*)pQuery->m_dwParam;
    WriteByte(pReply, 1);
    pReply->WriteBytes(pQuery->m_pBuffer, 3);
    return 1;
}
