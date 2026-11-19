#include "ShardManager/Messenger.h"

#include <string.h>
#include <strsafe.h>

#include "BSNet/NetEngine.h"
#include "ServerFramework/ServerDataExtern.h"

CMessengerMgr* g_pMessengerMgr = NULL;

CChunkAllocatorST<CMessenger> CMessenger::s_Pool(NULL, 0, 500, "CMessenger");     // 0x449790
CChunkAllocatorST<Friend> Friend::s_Pool(NULL, 0, 500, "Friend");                 // 0x44cb40
CChunkAllocatorST<Memo> Memo::s_Pool(NULL, 0, 500, "Memo");                       // 0x44cd70

namespace
{

CMsg* AllocMsg(WORD wOpcode)
{
    CMsg* pMsg = g_pNetEngine->AllocMsg(FALSE);
    pMsg->SetOpcode(wOpcode);
    return pMsg;
}

void SendAndRelease(DWORD dwSessionID, CMsg* pMsg)
{
    g_pNetEngine->SendMsg(dwSessionID, pMsg);
    g_pNetEngine->ReleaseMsg(pMsg);
}

// The _Friend table of the shard DB (inlined vector access): an out-of-range slot asserts and
// gives NULL.
CDBTable* FriendTable()
{
    std::vector<CDBTable*>& vecTables = GetSingleton_CShardDB()->GetInstanceTables();
    if (vecTables.size() <= SHARD_TABLE_FRIEND)
    {
        BSAssertFail();
        return NULL;
    }
    return vecTables[SHARD_TABLE_FRIEND];
}

}

void PoolObject_LogDoubleRelease(const char* pszName)
{
    LogPrintf(0x2000000, "why do you about to delete already deleted !!! [%s]", pszName);
    BSAssertFail();
}

// ---------------------------------------------------------------------------------------------
// Friend / Memo

void Friend::Release()
{
    PoolObject_Release(this, "Friend");
}

void Friend::ReleaseRecord()
{
    m_pUser = NULL;
    m_nState = 1;
    if (m_pRecord)
    {
        if (g_pShardDB)
            g_pShardDB->GetTable(SHARD_TABLE_FRIEND)->FreeRecord(m_pRecord);
        m_pRecord = NULL;
    }
}

void Friend::WriteToMsg(CMsg* pMsg) const
{
    *pMsg << m_pRecord->FriendCharID << m_pRecord->FriendCharName.c_str()
          << m_pRecord->RefObjID << (BYTE)m_nState;
}

void Memo::Release()
{
    PoolObject_Release(this, "Memo");
}

void Memo::ReleaseRecord()
{
    if (m_pRecord)
    {
        if (g_pShardDB)
            g_pShardDB->GetTable(SHARD_TABLE_MEMO)->FreeRecord(m_pRecord);
        m_pRecord = NULL;
    }
}

// ---------------------------------------------------------------------------------------------
// CMessenger

CMessenger::CMessenger()
{
    m_pOwner = NULL;
}

CMessenger::~CMessenger()
{
}

void CMessenger::Release()
{
    PoolObject_Release(this, "CMessenger");
}

BOOL CMessenger::Load(CUser* pOwner)
{
    if (m_pOwner)
        BSAssertFail();
    m_pOwner = pOwner;

    CDBTable* pTable = FriendTable();
    if (!pTable)
    {
        BSAssertFail();
        return FALSE;
    }

    CDBRecordList listRows;
    char szClause[0xC00];
    if (StringCchPrintfA(szClause, 0xC00, "WHERE CharID = %d", GetOwnerCharID()) != 0)
    {
        BSAssertFail();
        return FALSE;
    }
    if (!pTable->SelectWhere(szClause, 1, &listRows, 1))
    {
        CDBObjectList_DetachAndClear(&listRows, pTable);
        BSAssertFail();
        return FALSE;
    }
    for (CDBRecordList::iterator it = listRows.begin(); it != listRows.end(); ++it)
        AddFriendEntry((CInstanceFriend*)*it, 1);
    return TRUE;
}

BOOL CMessenger::Unload()
{
    GetSingleton_CMessengerMgr()->NotifyFriendsStatus(this, 1);

    for (FriendMap::iterator it = m_mapFriend.begin(); it != m_mapFriend.end(); ++it)
    {
        Friend* pFriend = it->second;
        pFriend->ReleaseRecord();
        PoolObject_FreePtr(pFriend);
    }
    m_mapFriend.clear();

    for (std::vector<Memo*>::iterator it = m_vecMemo.begin(); it != m_vecMemo.end(); ++it)
    {
        Memo* pMemo = *it;
        pMemo->ReleaseRecord();
        PoolObject_FreePtr(pMemo);
    }
    m_vecMemo.clear();

    ResetForPool();
    return TRUE;
}

void CMessenger::WriteFriendList(CMsg* pMsg)
{
    *pMsg << (BYTE)m_mapFriend.size();
    for (FriendMap::iterator it = m_mapFriend.begin(); it != m_mapFriend.end(); ++it)
    {
        Friend* pFriend = it->second;
        CUser* pUser =
            GetSingleton_CClientManager()->FindUserByCharName(pFriend->m_pRecord->FriendCharName);
        if (pUser)
        {
            WORD wState = pUser->GetState();
            if (wState == 2 || wState == 1)
                pFriend->m_nState = 0;
        }
        pFriend->WriteToMsg(pMsg);
    }
}

void CMessenger::CheckFriendHasMe(CUser* pMe, DWORD dwMyCharID, int nState)
{
    FriendMap::iterator it = m_mapFriend.find(dwMyCharID);
    if (it == m_mapFriend.end())
    {
        BSAssertFail();
        LogPrintf(0x2000001, "Friend messenger doesn't have my list! MyName[%s] FriendName[%s]",
                  pMe->GetCharName(), m_pOwner->GetCharName());
        return;
    }
    it->second->m_pUser = pMe;
    it->second->m_nState = nState;
}

BYTE CMessenger::CollectOnlineFriends(Friend** ppOut)
{
    DWORD dwCount = (DWORD)m_mapFriend.size();
    if (dwCount > MESSENGER_MAX_FRIENDS)
    {
        BSAssertFail();
        return 0;
    }
    BYTE byOnline = 0;
    BYTE byIndex = 0;
    if (!dwCount)
        return 0;
    FriendMap::iterator it = m_mapFriend.begin();
    do
    {
        Friend* pFriend = it->second;
        if (pFriend->m_nState == 0)
            ppOut[byOnline++] = pFriend;
        ++byIndex;
        ++it;
    } while (byIndex < dwCount);
    return byOnline;
}

Friend* CMessenger::AddFriendEntry(CInstanceFriend* pRecord, BYTE bLoading)
{
    Friend* pFriend = PoolObject_Alloc<Friend>();
    int nState = 1;
    CUser* pUser = GetSingleton_CClientManager()->FindUserByCharName(pRecord->FriendCharName);
    if (!bLoading && pUser)
    {
        WORD wState = pUser->GetState();
        if (wState == 2 || wState == 1)
            nState = 0;
    }
    pFriend->m_pRecord = pRecord;
    pFriend->m_pUser = pUser;
    pFriend->m_nState = nState;
    // The result is not checked: a duplicate CharID leaves the Friend out of the map.
    m_mapFriend.insert(FriendMap::value_type(pRecord->FriendCharID, pFriend));
    return pFriend;
}

CInstanceFriend* CMessenger::DBLoadFriendRow(DWORD dwCharID, DWORD dwFriendCharID)
{
    CDBRecordList listRows;
    CDBTable* pTable = FriendTable();
    if (!pTable)
    {
        BSAssertFail();
        return NULL;
    }
    char szClause[0xC00];
    if (StringCchPrintfA(szClause, 0xC00, "WHERE CharID = %d and FriendCharID = %d", dwCharID,
                         dwFriendCharID) != 0)
    {
        BSAssertFail();
        return NULL;
    }
    if (!pTable->SelectWhere(szClause, 1, &listRows, 0))
    {
        CDBObjectList_DetachAndClear(&listRows, pTable);
        BSAssertFail();
        return NULL;
    }
    if (listRows.size() != 1)
        BSAssertFail();
    return (CInstanceFriend*)listRows.front();
}

Friend* CMessenger::FindFriend(DWORD dwFriendCharID)
{
    FriendMap::iterator it = m_mapFriend.find(dwFriendCharID);
    if (it == m_mapFriend.end())
        return NULL;
    return it->second;
}

WORD CMessenger::RemoveFriend(DWORD dwFriendCharID)
{
    FriendMap::iterator it = m_mapFriend.find(dwFriendCharID);
    if (it == m_mapFriend.end())
        return MESSENGER_ERR_NOT_FOUND;

    Friend* pFriend = it->second;
    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _Friend_Remove (%d,%d)}", GetOwnerCharID(),
                         pFriend->m_pRecord->FriendCharID) != 0)
    {
        BSAssertFail();
        return 2;
    }

    // BUG(SM-240): the int return value is read into an __int64 whose high half stays 0, so
    // the error-code asserts below never match.
    __int64 llRet = 0;
    if (CDatabase::ExecDirectWithReturnValue(GetSingleton_CShardDB()->GetSession(), 0, szSql,
                                             DBEXEC_RETURN_INT | DBEXEC_RETURN_BIGINT,
                                             &llRet) & ~1)
    {
        BSAssertFail();
        LogPrintf(0x2000000, "Execute Procedure failed!! _Friend_Remove");
        return 2;
    }
    if ((int)llRet <= 0)
    {
        switch (llRet)
        {
        case -1001:
        case -1000:
        case -2:
        case -1:
            BSAssertFail();
            break;
        }
        return 2;
    }

    CUser* pFriendUser =
        GetSingleton_CClientManager()->FindUserByCharName(pFriend->m_pRecord->FriendCharName);
    if (pFriendUser)
    {
        CMessenger* pOther = GetSingleton_CMessengerMgr()->FindMessenger(pFriendUser->GetJID());
        if (pOther)
            pOther->OnRemovedByFriend(GetOwnerCharID());
        else
            BSAssertFail();
    }
    pFriend->ReleaseRecord();
    PoolObject_FreePtr(pFriend);
    m_mapFriend.erase(it);
    return 1;
}

void CMessenger::OnRemovedByFriend(DWORD dwFriendCharID)
{
    FriendMap::iterator it = m_mapFriend.find(dwFriendCharID);
    if (it == m_mapFriend.end())
    {
        LogPrintf(0x2000000,
                  "Someone erase me from friend that I have no idea!!! CHAR[%d] FriendCharID[%d]",
                  GetOwnerCharID(), dwFriendCharID);
        return;
    }
    if (m_pOwner && m_pOwner->GetState() == 2)
    {
        GetSingleton_CMessengerMgr()->SendFriendRemoved(m_pOwner->GetSessionID(),
                                                        m_pOwner->GetJID(), dwFriendCharID);
    }
    Friend* pFriend = it->second;
    pFriend->ReleaseRecord();
    PoolObject_FreePtr(pFriend);
    m_mapFriend.erase(it);
}

WORD CMessenger::SendMemo(__int64* pllMemoID, const char* pszText, const char* pszTo)
{
    char szSql[0xC00];
    HRESULT hr = StringCchPrintfA(szSql, 0xC00, "{?=CALL _Memo_Add (?, ?, ?, ?)}");
    if (hr)
    {
        LogPrintf(0x2000001, "CMessenger::SendMemo() Error Code = %d", hr);
        return 2;
    }

    DBParamList listParams;
    SQLINTEGER cbTo = (SQLINTEGER)strnlen(pszTo, 0x40);
    const char* pszFrom = m_pOwner->GetCharName();
    SQLINTEGER cbFrom = (SQLINTEGER)strnlen(pszFrom, 0x40);
    SQLINTEGER cbText = (SQLINTEGER)strnlen(pszText, 0x3E8);

    DB_PARAM param;
    param.llValue = 0;
    param.cbValue = 0;
    param.nType = DBPARAM_INTEGER;
    param.nIOType = SQL_PARAM_OUTPUT;
    listParams.push_back(param);

    param.cbValue = cbTo;
    param.nType = DBPARAM_VARCHAR;
    param.pszValue = (char*)pszTo;
    param.nIOType = SQL_PARAM_INPUT;
    listParams.push_back(param);

    param.nType = DBPARAM_VARCHAR;
    param.pszValue = (char*)pszFrom;
    param.cbValue = cbFrom;
    param.nIOType = SQL_PARAM_INPUT;
    listParams.push_back(param);

    param.nType = DBPARAM_VARCHAR;
    param.pszValue = (char*)pszText;
    param.cbValue = cbText;
    param.nIOType = SQL_PARAM_INPUT;
    listParams.push_back(param);

    param.nType = DBPARAM_INTEGER;
    param.nValue = 20;
    param.nIOType = SQL_PARAM_INPUT;
    listParams.push_back(param);

    int nRet = CDatabase::ExecDirectWithParams(GetSingleton_CShardDB()->GetSession(), 0, szSql,
                                               &listParams);
    if (nRet & ~1)
    {
        LogPrintf(0x2000000, "DB Query Error(%d): %s", nRet, szSql);
        return 0;
    }

    int nValue = listParams.front().nValue;
    listParams.pop_front();
    if (nValue > 0)
    {
        *pllMemoID = nValue;
        return 1;
    }
    if (nValue == -2)
        return MESSENGER_ERR_MEMO_FULL;
    if (nValue == -1)
        return MESSENGER_ERR_NOT_FOUND;
    LogPrintf(0x2000001, "Query Error with errorcode(%d)", nValue);
    return 2;
}

// ---------------------------------------------------------------------------------------------
// CMessengerMgr

CMessengerMgr::CMessengerMgr()
{
    g_pMessengerMgr = this;
}

CMessengerMgr::~CMessengerMgr()
{
    g_pMessengerMgr = NULL;
}

BOOL CMessengerMgr::OnCharLogin(CUser* pUser)
{
    if (FindMessenger(pUser->GetJID()))
    {
        BSAssertFail();
        return FALSE;
    }
    CMessenger* pMessenger = PoolObject_Alloc<CMessenger>();
    if (!pMessenger->Load(pUser))
    {
        PoolObject_FreePtr(pMessenger);
        return FALSE;
    }
    m_mapMessenger.insert(MessengerMap::value_type(pUser->GetJID(), pMessenger));
    return TRUE;
}

CMessenger* CMessengerMgr::FindMessenger(DWORD dwJID)
{
    if (!dwJID)
        return NULL;
    MessengerMap::iterator it = m_mapMessenger.find(dwJID);
    if (it == m_mapMessenger.end())
        return NULL;
    return it->second;
}

void CMessengerMgr::OnCharLogout(DWORD dwJID)
{
    if (!dwJID)
        BSAssertFail();
    MessengerMap::iterator it = m_mapMessenger.find(dwJID);
    if (it == m_mapMessenger.end())
        return;
    CMessenger* pMessenger = it->second;
    pMessenger->Unload();
    m_mapMessenger.erase(it);
    PoolObject_FreePtr(pMessenger);
}

void CMessengerMgr::SendFriendLoggedOut(CUser* pUser)
{
    CMsg* pMsg = AllocMsg(MSG_MESSENGER_NOTIFY);
    *pMsg << (BYTE)1 << pUser->GetJID();
    SendAndRelease(pUser->GetSessionID(), pMsg);
}

WORD CMessengerMgr::AddFriend(CUser* pRequester, CUser* pTarget)
{
    if (!pRequester || !pTarget)
        return 2;
    CMessenger* pMine = FindMessenger(pRequester->GetJID());
    CMessenger* pOther = FindMessenger(pTarget->GetJID());
    if (!pMine || !pOther)
        return 2;

    DWORD dwMyCharID = pMine->GetOwnerCharID();
    DWORD dwOtherCharID = pOther->GetOwnerCharID();
    if (pMine->FindFriend(dwOtherCharID) || pOther->FindFriend(dwMyCharID))
        return MESSENGER_ERR_ALREADY;
    // Only the low byte of the map size is compared.
    if ((BYTE)pMine->m_mapFriend.size() >= MESSENGER_MAX_FRIENDS)
        return MESSENGER_ERR_FULL_SELF;
    if ((BYTE)pOther->m_mapFriend.size() >= MESSENGER_MAX_FRIENDS)
        return MESSENGER_ERR_FULL_TARGET;

    char szSql[0xC00];
    if (StringCchPrintfA(szSql, 0xC00, "{?=CALL _Friend_Add (%d,'%s',%d,'%s')}", dwMyCharID,
                         pRequester->GetCharName(), dwOtherCharID, pTarget->GetCharName()) != 0)
    {
        BSAssertFail();
        return 2;
    }

    __int64 llRet = 0;
    if (CDatabase::ExecDirectWithReturnValue(GetSingleton_CShardDB()->GetSession(), 0, szSql,
                                             DBEXEC_RETURN_INT | DBEXEC_RETURN_BIGINT,
                                             &llRet) & ~1)
    {
        LogPrintf(0x2000000, "Execute Procedure failed _Friend_Add[%s]", szSql);
        BSAssertFail();
        return 0;
    }
    int nRet = (int)llRet;
    if (nRet <= 0)
    {
        switch (nRet)
        {
        case -1003:
        case -1001:
            BSAssertFail();
            return MESSENGER_ERR_ALREADY;
        case -1002:
        case -1000:
            return MESSENGER_ERR_FULL_SELF;
        default:
            BSAssertFail();
            LogPrintf(0x2000001, "_Friend_Add FAILED: errcode: %d", nRet);
            return 2;
        }
    }

    CInstanceFriend* pMyRow = CMessenger::DBLoadFriendRow(dwMyCharID, dwOtherCharID);
    CInstanceFriend* pOtherRow = CMessenger::DBLoadFriendRow(dwOtherCharID, dwMyCharID);
    if (pMyRow && pOtherRow)
    {
        Friend* pMyEntry = pMine->AddFriendEntry(pMyRow, 0);
        Friend* pOtherEntry = pOther->AddFriendEntry(pOtherRow, 0);
        if (pMyEntry && pOtherEntry)
            return 1;
    }
    else
    {
        LogPrintf(0x2000000,
                  "DB Insert is completed but has no Instance!!! CharID[%d], FriendChar[%d]",
                  dwMyCharID, dwOtherCharID);
    }
    BSAssertFail();
    return 2;
}

void CMessengerMgr::NotifyFriendsStatus(CMessenger* pMessenger, int nState)
{
    if (!pMessenger)
    {
        BSAssertFail();
        return;
    }
    CUser* pMe = pMessenger->m_pOwner;
    DWORD dwMyCharID = pMessenger->GetOwnerCharID();
    Friend* apOnline[MESSENGER_MAX_FRIENDS];
    memset(apOnline, 0, sizeof(apOnline));
    BYTE byCount = pMessenger->CollectOnlineFriends(apOnline);
    for (BYTE i = 0; i < byCount; ++i)
    {
        Friend* pFriend = apOnline[i];
        if (!pFriend || !pFriend->m_pRecord)
        {
            BSAssertFail();
            continue;
        }
        CUser* pUser =
            GetSingleton_CClientManager()->FindUserByCharName(pFriend->m_pRecord->FriendCharName);
        if (!pUser)
            continue;
        CMessenger* pOther = FindMessenger(pUser->GetJID());
        if (!pOther)
            continue;
        pOther->CheckFriendHasMe(pMe, dwMyCharID, nState);
        SendFriendStatus(pUser->GetSessionID(), pUser->GetJID(), dwMyCharID, (BYTE)nState);
    }
}

WORD CMessengerMgr::OnFriendAddRequest(DWORD dwRequesterJID, DWORD dwOpcode, CMsg* pMsg)
{
    CUser* pRequester = GetSingleton_CClientManager()->FindUserByJID(dwRequesterJID);
    if (!pRequester || !FindMessenger(dwRequesterJID))
        return 2;

    trs_messenger_job* pJob = trs_messenger_job_Alloc();
    WORD wResult;
    if ((WORD)dwOpcode != MSG_FRIEND_ADD_REQ)
    {
        BSAssertFail();
        wResult = 2;
    }
    else
    {
        CUser* pTarget;
        {
            std::string strName;
            *pMsg >> strName;
            pTarget = GetSingleton_CClientManager()->FindUserByCharName(strName);
            if (!pTarget || pTarget->GetState() != 2)
            {
                wResult = MESSENGER_ERR_NOT_FOUND;
                goto release;
            }
            DWORD dwTargetJID = pTarget->GetJID();
            CMessenger* pOther = FindMessenger(dwTargetJID);
            if (!pOther)
            {
                wResult = MESSENGER_ERR_NO_MESSENGER;
                goto release;
            }
            if (pOther->m_mapFriend.size() >= MESSENGER_MAX_FRIENDS)
            {
                wResult = MESSENGER_ERR_FULL_TARGET;
                goto release;
            }
            if (pOther->FindFriend(pRequester->GetCharID()))
            {
                wResult = MESSENGER_ERR_ALREADY;
                goto release;
            }
            pJob->Init(dwRequesterJID, dwOpcode, dwTargetJID, 30.0f, 0, 0);
            pJob->m_dwTargetSessionID = pTarget->GetSessionID();
        }
        if (GetSingleton_COverlappedJobMgr()->StartJob(pJob, pRequester, pMsg))
            return 1;
        pJob->Release();
        return 2;
    }
release:
    pJob->Release();
    return wResult;
}

void CMessengerMgr::OnFriendAddAnswer(DWORD dwRequesterJID, DWORD dwTargetJID, BYTE bAnswer)
{
    CUser* pRequester = GetSingleton_CClientManager()->FindUserByJID(dwRequesterJID);
    CUser* pTarget = GetSingleton_CClientManager()->FindUserByJID(dwTargetJID);
    if (!pRequester || !pTarget)
        return;
    if (bAnswer == 1)
    {
        if (AddFriend(pRequester, pTarget) == 1)
            SendFriendAdded(pTarget, pRequester);
        return;
    }
    // Both refusal notices carry the target's name.
    CMsg* pMsg = AllocMsg(MSG_MESSENGER_NOTIFY);
    *pMsg << (BYTE)5 << dwRequesterJID << pTarget->GetCharName() << bAnswer;
    SendAndRelease(pRequester->GetSessionID(), pMsg);
    pMsg = AllocMsg(MSG_MESSENGER_NOTIFY);
    *pMsg << (BYTE)5 << dwTargetJID << pTarget->GetCharName() << bAnswer;
    SendAndRelease(pTarget->GetSessionID(), pMsg);
}

void CMessengerMgr::OnMessengerRequest(CMsg* pMsg)
{
    switch (pMsg->ReadForwardOnly<BYTE>())
    {
    case 2:
        OnRemoveFriend(pMsg);
        break;
    case 3:
        OnSendMemo(pMsg);
        break;
    case 6:
        OnFriendListRequest(pMsg);
        break;
    case 7:
        OnMemoRequest(pMsg);
        break;
    default:
        BSAssertFail();
        break;
    }
}

void CMessengerMgr::OnFriendListRequest(CMsg* pMsg)
{
    DWORD dwJID = pMsg->ReadForwardOnly<DWORD>();
    CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
    if (!pUser || pUser->GetState() != 2)
        return;
    CMessenger* pMessenger = FindMessenger(pUser->GetJID());
    if (!pMessenger)
        return;
    DWORD dwSessionID = pUser->GetSessionID();
    DWORD dw1C8 = pUser->GetDw1C8();
    CMsg* pReply = AllocMsg(MSG_MESSENGER_NOTIFY);
    *pReply << (BYTE)7 << pUser->GetJID();
    pMessenger->WriteFriendList(pReply);
    SendAndRelease(dwSessionID, pReply);
    if (!dw1C8)
        NotifyFriendsStatus(pMessenger, 0);
}

void CMessengerMgr::OnRemoveFriend(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    DWORD dwRequestID, dwJID, dwFriendCharID;
    *pMsg >> dwRequestID >> dwJID >> dwFriendCharID;

    DWORD dwResult = 2;
    CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
    if (pUser)
    {
        CMessenger* pMessenger = FindMessenger(pUser->GetJID());
        if (pMessenger)
        {
            dwResult = pMessenger->RemoveFriend(dwFriendCharID);
            if (dwResult == 1)
            {
                CMsg* pReply = MakeReplyBB06(dwRequestID);
                *pReply << (BYTE)1;
                SendAndRelease(dwSessionID, pReply);
                return;
            }
        }
        else
        {
            dwResult = MESSENGER_ERR_NO_MESSENGER;
        }
    }
    pMsg->SkipRemaining();
    CMsg* pReply = MakeReplyBB06(dwRequestID);
    *pReply << (BYTE)2 << (WORD)dwResult;
    SendAndRelease(dwSessionID, pReply);
}

void CMessengerMgr::OnSendMemo(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    std::string strTo;
    std::string strText;
    DWORD dwRequestID, dwJID, dwRefObjID;
    *pMsg >> dwRequestID >> dwJID >> dwRefObjID;
    *pMsg >> strTo >> strText;

    WORD wResult = 2;
    CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
    CMessenger* pMessenger = NULL;
    if (pUser)
        pMessenger = FindMessenger(dwJID);
    if (pMessenger)
    {
        __int64 llMemoID;
        wResult = pMessenger->SendMemo(&llMemoID, strText.c_str(), strTo.c_str());
        if (wResult == 1)
        {
            CUser* pTarget = GetSingleton_CClientManager()->FindUserByCharName(strTo);
            if (pTarget && pTarget->GetState() == 2)
            {
                DWORD dwDate = 0;
                PackedDateTime_GetLocal(&dwDate);
                CMsg* pNotify = AllocMsg(MSG_MESSENGER_NOTIFY);
                *pNotify << (BYTE)8 << pTarget->GetJID() << llMemoID << pUser->GetCharName()
                         << dwRefObjID << strText.c_str() << dwDate;
                SendAndRelease(pTarget->GetSessionID(), pNotify);
            }
            CMsg* pReply = MakeReplyBB06(dwRequestID);
            *pReply << (BYTE)1;
            SendAndRelease(dwSessionID, pReply);
            return;
        }
        CUser* pTarget = GetSingleton_CClientManager()->FindUserByCharName(strTo);
        if (pTarget && pTarget->GetState() == 2)
        {
            CMsg* pNotify = AllocMsg(MSG_MESSENGER_NOTIFY);
            *pNotify << (BYTE)16;
            SendAndRelease(pTarget->GetSessionID(), pNotify);
        }
    }
    pMsg->SkipRemaining();
    CMsg* pReply = MakeReplyBB06(dwRequestID);
    *pReply << (BYTE)2 << wResult;
    SendAndRelease(dwSessionID, pReply);
}

void CMessengerMgr::OnMemoRequest(CMsg* pMsg)
{
    DWORD dwSessionID = pMsg->m_dwSessionID;
    std::string strText;
    std::vector<std::string> vecNames;
    DWORD dwRequestID, dwJID, dwRefObjID;
    BYTE byCount;
    *pMsg >> dwRequestID >> dwJID >> dwRefObjID >> byCount;
    vecNames.reserve(byCount);
    for (BYTE i = byCount; i > 0; --i)
    {
        std::string strName;
        *pMsg >> strName;
        vecNames.push_back(strName);
    }
    *pMsg >> strText;

    WORD wResult = 2;
    CUser* pUser = GetSingleton_CClientManager()->FindUserByJID(dwJID);
    CMessenger* pMessenger = NULL;
    if (pUser)
        pMessenger = FindMessenger(dwJID);
    if (!pMessenger)
    {
        pMsg->SkipRemaining();
        CMsg* pReply = MakeReplyBB06(dwRequestID);
        *pReply << (BYTE)2 << wResult;
        SendAndRelease(dwSessionID, pReply);
        return;
    }

    // Failed receivers with their result (MemoNameEntry = the map's value_type).
    std::map<std::string, WORD> mapFailed;
    for (int i = 0; i < (int)byCount; ++i)
    {
        std::string strName = vecNames.at(i);
        __int64 llMemoID;
        WORD wSend = pMessenger->SendMemo(&llMemoID, strText.c_str(), strName.c_str());
        if (wSend == 1)
        {
            CUser* pTarget = GetSingleton_CClientManager()->FindUserByCharName(strName);
            if (pTarget && pTarget->GetState() == 2)
            {
                DWORD dwDate = 0;
                PackedDateTime_GetLocal(&dwDate);
                CMsg* pNotify = AllocMsg(MSG_MESSENGER_NOTIFY);
                *pNotify << (BYTE)8 << pTarget->GetJID() << llMemoID << pUser->GetCharName()
                         << dwRefObjID << strText.c_str() << dwDate;
                SendAndRelease(pTarget->GetSessionID(), pNotify);
            }
        }
        else
        {
            mapFailed.insert(std::make_pair(strName, wSend));
            CUser* pTarget = GetSingleton_CClientManager()->FindUserByCharName(strName);
            if (pTarget && pTarget->GetState() == 2)
            {
                CMsg* pNotify = AllocMsg(MSG_MESSENGER_NOTIFY);
                *pNotify << (BYTE)16;
                SendAndRelease(pTarget->GetSessionID(), pNotify);
            }
        }
    }

    CMsg* pReply = MakeReplyBB06(dwRequestID);
    *pReply << (BYTE)1 << (BYTE)mapFailed.size();
    for (std::map<std::string, WORD>::iterator it = mapFailed.begin(); it != mapFailed.end();
         ++it)
    {
        *pReply << it->first.c_str() << it->second;
    }
    SendAndRelease(dwSessionID, pReply);
}

CMsg* CMessengerMgr::MakeReplyBB06(DWORD dwRequestID)
{
    CMsg* pMsg = AllocMsg(MSG_MESSENGER_ACK);
    *pMsg << dwRequestID;
    return pMsg;
}

void CMessengerMgr::SendFriendAdded(CUser* pTarget, CUser* pRequester)
{
    if (!pRequester || !pTarget)
    {
        BSAssertFail();
        return;
    }
    CMsg* pMsg = AllocMsg(MSG_MESSENGER_NOTIFY);
    *pMsg << (BYTE)2 << pRequester->GetJID() << pTarget->GetCharID() << pTarget->GetCharName()
          << pTarget->GetRefObjID();
    SendAndRelease(pRequester->GetSessionID(), pMsg);

    pMsg = AllocMsg(MSG_MESSENGER_NOTIFY);
    *pMsg << (BYTE)2 << pTarget->GetJID() << pRequester->GetCharID()
          << pRequester->GetCharName() << pRequester->GetRefObjID();
    SendAndRelease(pTarget->GetSessionID(), pMsg);
}

void CMessengerMgr::SendFriendStatus(DWORD dwSessionID, DWORD dwJID, DWORD dwFriendCharID,
                                     BYTE bState)
{
    CMsg* pMsg = AllocMsg(MSG_MESSENGER_NOTIFY);
    *pMsg << (BYTE)4 << dwJID << dwFriendCharID << bState;
    SendAndRelease(dwSessionID, pMsg);
}

void CMessengerMgr::SendFriendRemoved(DWORD dwSessionID, DWORD dwJID, DWORD dwFriendCharID)
{
    CMsg* pMsg = AllocMsg(MSG_MESSENGER_NOTIFY);
    *pMsg << (BYTE)3 << dwJID << dwFriendCharID;
    SendAndRelease(dwSessionID, pMsg);
}
