// GlobalManager application: content users and the play time job.

#include <winsock2.h>
#include <windows.h>
#include <string.h>

#include "LoopbackBind.h"
#include "TestCheck.h"

#include "GlobalManager/ContentUser.h"
#include "GlobalManager/GlobalManagerDB.h"
#include "GlobalManager/GlobalManagerJobs.h"
#include "GlobalManager/GlobalManagerMainFwd.h"
#include "BSLib/BSDebug.h"
#include "BSLib/DB/DBQuery.h"
#include "BSLib/Util/TimeSpan.h"
#include "BSNet/NetEngine.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerBody.h"

using namespace ServerFramework;

// Stands in for the main process: records the jobs instead of sending them.
class CTestProcess : public CServerProcessMain
{
public:
    virtual bool SendOverlapJob(COverlapJob_Local* pJob, CTask* pTask)
    {
        m_pLastJob = pJob;
        m_pLastTask = pTask;
        ++m_nJobs;
        return true;
    }

    COverlapJob_Local*  m_pLastJob;
    CTask*              m_pLastTask;
    int                 m_nJobs;
};

static CNetEngine s_Engine;

// Stand-in for the main process' setter (CGlobalManagerMainProcess is not ported yet).
static DWORD s_dwLastPlayTimeReset;

void GlobalManagerMain_SetLastPlayTimeReset(DWORD dwTime)
{
    s_dwLastPlayTimeReset = dwTime;
}

static sContentUser* MakeUser(sContentUser& user, DWORD dwIP, DWORD dwJID, const char* pszID,
                              WORD wShard, WORD wAgent)
{
    user.dwIP = dwIP;
    user.dwJID = dwJID;
    user.strID = pszID;
    user.byContentID = 22;
    user.wShardID = wShard;
    user.wAgentBodyID = wAgent;
    user.strMAC = "00-11-22-33-44-55";
    user.PlayTime.Clear();
    return &user;
}

static void TestWildcard()
{
    CHECK(ContentUser_WildcardMatch("*", "abc"));
    CHECK(ContentUser_WildcardMatch("a*c", "abbbc"));
    CHECK(ContentUser_WildcardMatch("a?c", "abc"));
    CHECK(!ContentUser_WildcardMatch("a?c", "abbc"));
    CHECK(ContentUser_WildcardMatch("*b*", "abc"));
    CHECK(!ContentUser_WildcardMatch("A*", "abc"));          // case-sensitive
    CHECK(ContentUser_WildcardMatch("ab**", "ab"));
    CHECK(!ContentUser_WildcardMatch("abc", "ab"));
    CHECK(ContentUser_IsOfficialIP(0x01AC95DD));
    CHECK(ContentUser_IsOfficialIP(0xFE82093B));
    CHECK(!ContentUser_IsOfficialIP(0x0100007F));
    CHECK(!ContentUser_IsNotToday(CDateTime::GetCurrent().GetTime()));
    CHECK(ContentUser_IsNotToday(CDateTime::GetCurrent().GetTime() - 3 * 86400));
}

static void TestRegister()
{
    CContentUserManager& m = g_ContentUserManager;
    g_nMaxUserPerIP = 2;
    sContentUser tmp;
    CHECK(m.Register(MakeUser(tmp, 0x0A00000A, 100, "alpha", 3, 7)) == 0);
    CHECK(tmp.byIPType == 4);
    CHECK(m.Register(MakeUser(tmp, 0x0A00000A, 100, "alpha", 3, 7)) == 1);
    CHECK(m.Register(MakeUser(tmp, 0x0A00000A, 101, "beta", 3, 7)) == 0);
    CHECK(m.Register(MakeUser(tmp, 0x0A00000A, 102, "gamma", 3, 8)) == 2);  // limit 2
    CHECK(m.Register(MakeUser(tmp, 0x0B00000A, 102, "gamma", 4, 8)) == 0);
    CHECK(m.m_nUserCount == 3);
    CHECK(m.m_nOccupiedIPCount == 2);
    CHECK(m.m_nDailyVisited == 3 && m.m_nTotalVisited == 3);
    CHECK(ContentUser_TakeRegisteredCount() == 3);
    CHECK(ContentUser_TakeRegisteredCount() == 0);
    CHECK(m.m_bChanged);

    sContentUser* p = m.Find(101);
    CHECK(p && p->strID == "beta" && p->wAgentBodyID == 7 && p->bInUse == 1);
    CHECK(p != &tmp && p->strMAC == "00-11-22-33-44-55");
    CHECK(m.FindByID("gamma") == m.Find(102));
    CHECK(m.FindByID("GAMMA") == NULL);
    CHECK(m.CheckLogin(103, 0x0A00000A) == 2);
    CHECK(m.CheckLogin(100, 0x0C00000A) == 1);

    // Unregister: the address is returned, a user with a play time count gets a job.
    CTestProcess* pProcess = new CTestProcess;
    pProcess->m_nJobs = 0;
    g_pGlobalManagerProcess = pProcess;
    m.Find(100)->PlayTime.dwTime = CDateTime::GetCurrent().GetTime() - 10;
    m.Find(100)->PlayTime.wPlayTime = 5;
    DWORD dwIP = 1;
    CHECK(m.Unregister(100, &dwIP));
    CHECK(dwIP == 0x0A00000A);
    CHECK(pProcess->m_nJobs == 1);
    COverlapJob_UpdatePlayTimeOfUser* pJob =
        (COverlapJob_UpdatePlayTimeOfUser*)pProcess->m_pLastJob;
    CHECK(pJob->m_dwJID == 100 && pJob->m_wPlayTime >= 15 && pJob->m_wPlayTime <= 17);
    CHECK(pJob->m_dwID != 0 && pProcess->m_pLastTask == NULL);
    CHECK(pJob->Prepare() && pJob->OnComplete() && pJob->OnFail());
    pJob->ReturnToPool();
    CHECK(g_UpdatePlayTimeOfUserJobPool.m_nInUse == 0);
    CHECK(!m.Unregister(100, &dwIP));
    CHECK(dwIP == 0);
    CHECK(ContentUser_TakeUnregisteredCount() == 1);
    CHECK(m.m_nUserCount == 2);
    CHECK(m.FindIP(0x0A00000A)->m_mapUser.size() == 1);

    // A user of another AgentServer stays.
    CHECK(m.UnregisterByAgent(7) == 1);
    CHECK(m.Find(101) == NULL && m.Find(102) != NULL);
    CHECK(pProcess->m_nJobs == 1);
    CHECK(g_ContentUserPool.m_nInUse == 1);
    g_pGlobalManagerProcess = NULL;
    delete pProcess;
}

static void TestQueries()
{
    CContentUserManager& m = g_ContentUserManager;
    sContentUser tmp;
    CHECK(m.Register(MakeUser(tmp, 0x0A00000A, 200, "abc", 3, 7)) == 0);

    CMassiveMsg* pMsg = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pMsg->SetOpcode(0x1234);
    CHECK(m.WriteUserQuery(pMsg, "a?c"));
    pMsg->BeginRead();
    BYTE b;
    DWORD dw;
    WORD w;
    char sz[8] = {0};
    pMsg->Read(&b, 1);
    CHECK(b == 1);
    pMsg->Read(&dw, 4);
    CHECK(dw == 0x0A00000A);
    pMsg->Read(&dw, 4);
    CHECK(dw == 200);
    pMsg->Read(&b, 1);
    CHECK(b == 4);
    pMsg->Read(&w, 2);
    CHECK(w == 3);
    pMsg->Read(sz, 3);
    CHECK(strcmp(sz, "abc") == 0);
    pMsg->Read(&b, 1);
    CHECK(b == 22);
    pMsg->Read(&w, 2);
    CHECK(w == 3);
    SYSTEMTIME st;
    pMsg->Read(&st, 16);
    CHECK(st.wYear >= 2024);
    pMsg->Read(&b, 1);
    CHECK(b == 0);
    CHECK(pMsg->GetUnreadSize() == 0);
    pMsg->Free();

    // "*" matches every user (gamma and abc).
    pMsg = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pMsg->SetOpcode(0x1234);
    m.WriteUserQuery(pMsg, "*");
    pMsg->BeginRead();
    int nUsers = 0;
    for (;;)
    {
        pMsg->Read(&b, 1);
        if (!b)
            break;
        ++nUsers;
        BYTE ab[4 + 4 + 1];
        pMsg->Read(ab, sizeof(ab));
        pMsg->Read(&w, 2);
        char szName[32];
        pMsg->Read(szName, w);
        BYTE ab2[1 + 2 + 16];
        pMsg->Read(ab2, sizeof(ab2));
    }
    CHECK(nUsers == 2);
    pMsg->Free();

    // Billing list: WORD count, per user 4+4+4+4+1+4+4, WORD length, MAC.
    pMsg = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pMsg->SetOpcode(0x1234);
    m.WriteBillingUserList(pMsg);
    pMsg->BeginRead();
    pMsg->Read(&w, 2);
    CHECK(w == 2);
    BYTE abUser[25];
    pMsg->Read(abUser, sizeof(abUser));
    CHECK(*(DWORD*)abUser == 0x0B00000A && *(DWORD*)(abUser + 4) == 102);
    CHECK(*(DWORD*)(abUser + 8) == 22 && *(DWORD*)(abUser + 12) == 4);
    pMsg->Read(&w, 2);
    CHECK(w == 17);
    pMsg->Free();

    // Shard counts: users of unknown shards are only logged.
    CERT_SHARD shard;
    memset(&shard, 0, sizeof(shard));
    shard.wID = 3;
    shard.w557 = 99;
    CertShardList shards;
    shards.push_back(&shard);
    g_pCertShardList = &shards;
    g_mapCertShard[3] = &shard;
    CMsg* pCount = s_Engine.AllocMsg(FALSE);
    m.WriteShardUserCount(pCount);
    CHECK(pCount->GetUnreadSize() == 5);
    CHECK(pCount->m_pData[6] == 1 && *(WORD*)(pCount->m_pData + 7) == 3);
    CHECK(*(WORD*)(pCount->m_pData + 9) == 1);
    s_Engine.ReleaseMsg(pCount);
    g_mapCertShard.clear();
    g_pCertShardList = NULL;
}

static void TestFranchise()
{
    CContentUserManager& m = g_ContentUserManager;
    // Row trigger: before the row the buffer is cleared, after it the address is added.
    CDBQuery query;
    memset(&query, 0, sizeof(query));
    BYTE row[6];
    memset(row, 0xCC, sizeof(row));
    query.m_pBuffer = row;
    CHECK(m.OnFranchiseIPRow((DWORD)&query, 0) == 1);
    CHECK(*(DWORD*)row == 0 && *(short*)(row + 4) == 0);
    *(DWORD*)row = 0x0D00000A;
    *(short*)(row + 4) = 5;
    query.m_bRowFetched = 1;
    int nBefore = m.m_nFranchiseIPCount;
    CHECK(m.OnFranchiseIPRow((DWORD)&query, 0) == 1);
    CSingleIPContentUserMap* pMap = m.FindIP(0x0D00000A);
    CHECK(pMap && pMap->m_nLimit == 5 && pMap->m_bFranchise);
    CHECK(m.m_nFranchiseIPCount == nBefore + 1);
    sContentUser tmp;
    CHECK(m.Register(MakeUser(tmp, 0x0D00000A, 300, "delta", 3, 9)) == 0);
    CHECK(tmp.byIPType == 3);
    // Official networks are unlimited.
    g_nMaxUserPerIP = 1;
    CHECK(m.AddIP(0x05AC95DD, FALSE)->m_nLimit == -1);
    CHECK(m.AddIP(0x0E00000A, FALSE)->m_nLimit == 1);
}

static void TestRemoveAll()
{
    CContentUserManager& m = g_ContentUserManager;
    CHECK(g_ContentUserPool.m_nInUse == 3);
    m.RemoveAll();
    CHECK(g_ContentUserPool.m_nInUse == 0);
    CHECK(g_SingleIPContentUserMapPool.m_nInUse == 0);
    CHECK(m.m_mapUser.empty() && m.m_mapIP.empty());
    CHECK(m.m_setDailyJID.empty() && m.m_setTotalJID.empty());
}

int main()
{
    CHECK(LoopbackBind_Install() == 1);
    BSDebugSetFlags(0, 0);
    BSNET_INIT_PARAM param;
    CHECK(s_Engine.Initialize(param));
    g_pBSNet = &s_Engine;

    TestWildcard();
    TestRegister();
    TestQueries();
    TestFranchise();
    TestRemoveAll();

    s_Engine.Shutdown();
    g_pBSNet = NULL;
    return TEST_RESULT("GlobalManagerTest");
}
