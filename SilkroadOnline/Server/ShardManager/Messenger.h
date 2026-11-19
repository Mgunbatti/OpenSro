#pragma once

#include <map>
#include <string>
#include <vector>

#include "BSLib/ChunkAllocator.h"
#include "ServerCommon/PoolObject.h"
#include "ServerCommon/InstanceRecords.h"
#include "ShardManager/SocialExtern.h"

// Messenger of SR_ShardManager: the friend list and the memos of every logged-in character
// (CMessengerMgr, g_pMessengerMgr 0x8a8928), relayed to the game servers with opcode 0x3B07 and
// answered with 0xBB06.

enum
{
    MSG_MESSENGER_NOTIFY    = 0x3B07,
    MSG_MESSENGER_ACK       = 0xBB06,
    MSG_FRIEND_ADD_REQ      = 0x7302,

    MESSENGER_MAX_FRIENDS   = 20,

    // result codes
    MESSENGER_ERR_NO_MESSENGER  = 0x6407,
    MESSENGER_ERR_FULL_SELF     = 0x6408,
    MESSENGER_ERR_ALREADY       = 0x6409,
    MESSENGER_ERR_NOT_FOUND     = 0x640D,
    MESSENGER_ERR_FULL_TARGET   = 0x640F,
    MESSENGER_ERR_MEMO_FULL     = 0x6414,
};

// One friend of a messenger (20 bytes, pool "Friend").
class Friend
{
public:
    // Friend::ctor (0x44c8e0)
    Friend()
        : m_nState(-1)
    {
        m_pRecord = NULL;
        m_pUser = NULL;
        m_nState = 1;
    }

    // Friend::vf00 (0x44c980)
    virtual void Release();                                         // 0

    // Friend::ReleaseRecord (0x44ca90): state 1, no user, record back to the _Friend table.
    void ReleaseRecord();
    void ResetForPool()
    {
        ReleaseRecord();
    }
    // Friend::WriteToMsg (0x44ca30): CharID, name, RefObjID, state byte.
    void WriteToMsg(CMsg* pMsg) const;

public:
    int                 m_nState;       // +0x04  0 online, 1 offline
    BYTE                m_bInUse;       // +0x08
    CInstanceFriend*    m_pRecord;      // +0x0C
    CUser*              m_pUser;        // +0x10

    static CChunkAllocatorST<Friend> s_Pool;
};

C_ASSERT(FIELD_OFFSET(Friend, m_pRecord) == 0x0C);
C_ASSERT(sizeof(Friend) == 20);

// One memo (12 bytes, pool "Memo").
class Memo
{
public:
    Memo()
        : m_pRecord(NULL)
    {
    }

    // Memo::vf00 (0x44c9f0)
    virtual void Release();                                         // 0

    // Memo::ReleaseRecord (0x44cad0): record back to the _Memo table.
    void ReleaseRecord();
    void ResetForPool()
    {
        ReleaseRecord();
    }

public:
    BYTE            m_bInUse;       // +0x04
    CInstanceMemo*  m_pRecord;      // +0x08

    static CChunkAllocatorST<Memo> s_Pool;
};

C_ASSERT(sizeof(Memo) == 12);

// CMessengerCommon (vftable 0x784d1c): only the virtual destructor.
class CMessengerCommon
{
public:
    // CMessengerCommon::ctor (0x44cb00), scalar deleting dtor 0x44cb10
    virtual ~CMessengerCommon() {}
};

typedef std::map<DWORD, Friend*> FriendMap;     // key: FriendCharID

// The messenger of one character (40 bytes, pool "CMessenger").
class CMessenger : public CMessengerCommon
{
public:
    CMessenger();                                                   // 0x445fa0
    virtual ~CMessenger();                                          // 0 (0x446070)
    // CMessenger::vf01 (0x445f60)
    virtual void Release();                                         // 1

    // CMessenger_ClearField8 (0x4460f0)
    void ResetForPool()
    {
        m_pOwner = NULL;
    }
    // CMessenger_GetOwnerField7C (0x446480)
    DWORD GetOwnerCharID() const
    {
        return m_pOwner->GetCharID();
    }

    // Binds pOwner and reads its _Friend rows (0x446100).
    BOOL Load(CUser* pOwner);
    // Tells the friends, frees the friends and memos, unbinds the owner (0x446300).
    BOOL Unload();
    // Count byte and the friends (0x446490).
    void WriteFriendList(CMsg* pMsg);
    // The friend dwCharID has me: refresh my user and state in its entry, log otherwise
    // (0x446560).
    void CheckFriendHasMe(CUser* pMe, DWORD dwMyCharID, int nState);
    // The online friends into ppOut (at most 20); their number (0x446610).
    BYTE CollectOnlineFriends(Friend** ppOut);
    // A Friend for pRecord, inserted by FriendCharID (0x4466a0).
    Friend* AddFriendEntry(CInstanceFriend* pRecord, BYTE bLoading);
    // The _Friend row (dwCharID, dwFriendCharID), NULL on failure (0x446730).
    static CInstanceFriend* DBLoadFriendRow(DWORD dwCharID, DWORD dwFriendCharID);
    // 0x4468d0
    Friend* FindFriend(DWORD dwFriendCharID);
    // _Friend_Remove and the other side's entry (0x446930). 1 or an error code.
    WORD RemoveFriend(DWORD dwFriendCharID);
    // The friend dwFriendCharID removed me (0x446b10).
    void OnRemovedByFriend(DWORD dwFriendCharID);
    // _Memo_Add from this messenger's owner to pszTo (0x446c00). 1 with *pllMemoID set, or an
    // error code.
    WORD SendMemo(__int64* pllMemoID, const char* pszText, const char* pszTo);

public:
    BYTE                m_bInUse;       // +0x04
    CUser*              m_pOwner;       // +0x08
    FriendMap           m_mapFriend;    // +0x0C
    std::vector<Memo*>  m_vecMemo;      // +0x18

    static CChunkAllocatorST<CMessenger> s_Pool;
};

C_ASSERT(FIELD_OFFSET(CMessenger, m_mapFriend) == 0x0C);
C_ASSERT(FIELD_OFFSET(CMessenger, m_vecMemo) == 0x18);
C_ASSERT(sizeof(CMessenger) == 40);

typedef std::map<DWORD, CMessenger*> MessengerMap;   // key: CUser JID

// The messenger manager (16 bytes).
class CMessengerMgr
{
public:
    CMessengerMgr();
    virtual ~CMessengerMgr();                                       // 0 (0x4470d0)

    // OnMsg_7001 (0x447130)
    BOOL OnCharLogin(CUser* pUser);
    // 0x4471b0
    CMessenger* FindMessenger(DWORD dwJID);
    // CUser::Reset (0x447210)
    void OnCharLogout(DWORD dwJID);
    // OnMsg_B003: 0x3B07 subtype 1 to the user's session (0x4472a0)
    static void SendFriendLoggedOut(CUser* pUser);
    // _Friend_Add for both users (0x447320). 1 or an error code.
    WORD AddFriend(CUser* pRequester, CUser* pTarget);
    // Friend status (nState) of pMessenger's owner to its online friends (0x447560).
    void NotifyFriendsStatus(CMessenger* pMessenger, int nState);
    // OnMsg_7C00 opcode 0x7302: asks the target with a trs_messenger_job (0x447650).
    WORD OnFriendAddRequest(DWORD dwRequesterJID, DWORD dwOpcode, CMsg* pMsg);
    // The target's answer (trs_messenger_job, 0x447850).
    void OnFriendAddAnswer(DWORD dwRequesterJID, DWORD dwTargetJID, BYTE bAnswer);
    // OnMsg_7B06 (0x447a10)
    void OnMessengerRequest(CMsg* pMsg);
    void OnFriendListRequest(CMsg* pMsg);                           // 0x447a70
    void OnRemoveFriend(CMsg* pMsg);                                // 0x447b80
    void OnSendMemo(CMsg* pMsg);                                    // 0x447d00
    void OnMemoRequest(CMsg* pMsg);                                 // 0x448180

    static CMsg* MakeReplyBB06(DWORD dwRequestID);                  // 0x448a90
    static void SendFriendAdded(CUser* pTarget, CUser* pRequester); // 0x448ad0
    static void SendFriendStatus(DWORD dwSessionID, DWORD dwJID, DWORD dwFriendCharID,
                                 BYTE bState);                      // 0x448c80
    static void SendFriendRemoved(DWORD dwSessionID, DWORD dwJID, DWORD dwFriendCharID);
                                                                    // 0x448d10

public:
    MessengerMap    m_mapMessenger;     // +0x04
};

C_ASSERT(sizeof(CMessengerMgr) == 16);

// g_pMessengerMgr (0x8a8928); GetSingleton_CMessengerMgr (0x418460) asserts it.
extern CMessengerMgr* g_pMessengerMgr;

inline CMessengerMgr* GetSingleton_CMessengerMgr()
{
    if (!g_pMessengerMgr)
        BSAssertFail();
    return g_pMessengerMgr;
}
