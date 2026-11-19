#pragma once

#include <windows.h>

#include <list>
#include <map>
#include <string>
#include <vector>

#include "ServerCommon/RefRecords.h"
#include "ShardManager/ShardWorldExtern.h"

class CMsg;
class CReferenceData;

// The game worlds of SR_ShardManager (_RefGame_World). Every CGameWorld has a slot vector of
// layers (instances, index = layer ID, slot 0 unused); worlds of a Type other than 0 open and
// close layers on request of the game servers (0x7C14 / 0x7C16) or of the schedule jobs, the
// others use layer 1 only. CGameWorldMgr owns the worlds, the (world, layer) -> instance ID
// registry and the enter reservations of the characters.

// ---------------------------------------------------------------------------------------------
// sGameWorldCreateParam (44 bytes): ctor 0x41ada0, dtor 0x4170a0

struct sGameWorldCreateParam
{
    sGameWorldCreateParam();

    // sGameWorldCreateParam::CopyAssign (0x65ba00): the fields, then the vector when the source
    // has one. BUG(SM-102): an empty source vector leaves the old one in place.
    void CopyTo(sGameWorldCreateParam& dst) const;
    // sGameWorldCreateParam::ReadFromMsg (0x65d620) / WriteToMsg (0x65b120).
    void ReadFromMsg(CMsg* pMsg);
    void WriteToMsg(CMsg* pMsg) const;

    DWORD               m_dw0;                  // +0
    BYTE                m_b4;                   // +4   1
    DWORD               m_dw8;                  // +8
    BYTE                m_b12;                  // +C
    BYTE                m_b13;                  // +D
    DWORD               m_dwDuration;           // +10  ms until the layer closes
    DWORD               m_dwEmptyRemainTime;    // +14  ms an empty layer stays open
    BYTE                m_b24;                  // +18  not sent
    std::vector<DWORD>  m_vecParam;             // +1C
};

C_ASSERT(FIELD_OFFSET(sGameWorldCreateParam, m_vecParam) == 0x1C);
C_ASSERT(sizeof(sGameWorldCreateParam) == 0x2C);

// An enter reservation (CGameWorldMgr::ReserveEnter): value of the reservation maps.
struct GameWorldReservation
{
    DWORD               dwCharID;               // +0
    WORD                wWorldID;               // +4
    WORD                wLayerID;               // +6
    DWORD               dwTick;                 // +8
};

C_ASSERT(sizeof(GameWorldReservation) == 12);

// ---------------------------------------------------------------------------------------------
// GameWorldLayer (0x70 bytes)

class GameWorldLayer
{
public:
    typedef std::map<DWORD, CUser*>                 MemberMap;
    typedef std::map<DWORD, GameWorldReservation>   ReservationMap;

    GameWorldLayer(WORD wLayerID);                      // 0x658190
    ~GameWorldLayer();                                  // 0x658ac0 (not virtual)

    // GameWorldLayer::Init (0x6582d0, slot 0): instance ID, create time, parameters, the open
    // and close ticks and the sorted remaining-time notices.
    virtual void Init(DWORD dwWorldID, const sGameWorldCreateParam* pParam, DWORD dwInstanceID,
                      const std::vector<int>& vecNotify);

    // GameWorldLayer::WriteToMsg (0x658360).
    void WriteToMsg(CMsg* pMsg) const;
    // GameWorldLayer::CountRecentEntries (0x6583b0): reservations younger than 10 seconds.
    WORD CountRecentReservations() const;

    WORD                    m_wLayerID;             // +4
    DWORD                   m_dwInstanceID;         // +8
    MemberMap               m_mapMember;            // +C   by CharID
    ReservationMap          m_mapReservation;       // +18  by CharID
    DWORD                   m_dwCreateTime;         // +24  PackedDateTime_GetLocal
    sGameWorldCreateParam   m_Param;                // +28
    DWORD                   m_dwEmptySince;         // +54  tick; 0 while a member entered
    DWORD                   m_dwCloseTick;          // +58  0: no close time
    std::vector<DWORD>      m_vecNotify;            // +5C  remaining ms of the 0x3CE8 notices
    DWORD                   m_dwWorldID;            // +6C
};

C_ASSERT(FIELD_OFFSET(GameWorldLayer, m_mapMember) == 0x0C);
C_ASSERT(FIELD_OFFSET(GameWorldLayer, m_dwCreateTime) == 0x24);
C_ASSERT(FIELD_OFFSET(GameWorldLayer, m_dwEmptySince) == 0x54);
C_ASSERT(FIELD_OFFSET(GameWorldLayer, m_vecNotify) == 0x5C);
C_ASSERT(sizeof(GameWorldLayer) == 0x70);

// ---------------------------------------------------------------------------------------------
// CGameWorld (0x34 bytes)

class CGameWorld
{
public:
    CGameWorld();                                       // 0x658440
    virtual ~CGameWorld();                              // 0x6584e0

    // CGameWorld::vf01 (0x658590): binds the row, then ResizeLayers and CollectStartPositions.
    virtual void Init(RefGameWorld* pRef);
    // CGameWorld::Tick (0x658810): remaining-time notices, close of the expired layers and of
    // the layers that stayed empty too long.
    virtual void Tick();
    // CGameWorld::vf03 (0x658760): WorldMaxCount + 1 slots.
    virtual void ResizeLayers();
    // CGameWorld::CollectRefEntries (0x658780): the _RefInstance_World_Start_Pos rows of the world.
    virtual void CollectStartPositions();
    // CGameWorld::CloseAllLayers (0x6589a0): deletes every layer, open or free.
    virtual void CloseAllLayers();
    // CGameWorld::CreateLayer (0x658b50): layer 1 of a world of Type 0, else a free layer;
    // NULL with *pbyResult = 7 when every slot is taken.
    virtual GameWorldLayer* CreateLayer(const sGameWorldCreateParam* pParam, DWORD dwInstanceID,
                                        BYTE* pbyResult);
    // CGameWorld::EnterLayer (0x658e30): 1, 2 (already a member) or 6 (no layer).
    virtual BYTE EnterLayer(WORD wLayerID, CUser* pUser);
    // CGameWorld::LeaveLayer (0x658f10): 1 or 6.
    virtual BYTE LeaveLayer(CUser* pUser, WORD wLayerID);
    // CGameWorld::AddReservation (0x659220) / RemoveReservation (0x6592d0).
    virtual void AddReservation(WORD wLayerID, const GameWorldReservation* pReservation);
    virtual void RemoveReservation(WORD wLayerID, const DWORD* pdwCharID);
    // CGameWorld::CloseLayer (0x6585b0): 1, 0 (no layer) or 3 (bad layer ID).
    virtual BYTE CloseLayer(WORD wLayerID);

    // CGameWorld::WriteLayerList (0x658fb0).
    void WriteLayerList(CMsg* pMsg) const;
    // CGameWorld::SetLayerCloseTime (0x659090): 1, 6 (no layer) or 8 (world of Type 0).
    BYTE SetLayerCloseTime(WORD wLayerID, DWORD dwRemain);
    // CGameWorld::CheckLayerCapacity (0x6591a0): 1, 2 (no user), 6 (no layer) or 14 (full).
    BYTE CheckLayerCapacity(WORD wLayerID, CUser* pUser);
    // CGameWorld::NotifyLayerRemainTime_3CE8 (0x6590f0).
    void NotifyLayerRemainTime(WORD wWorldID, WORD wLayerID, DWORD dwRemain);
    // CGameWorld::GetDisappearanceTime (0x65b750): the GAMEWORLD_DISAPPEARANCE_TIME values of
    // the world's config group.
    static BOOL GetDisappearanceTime(CReferenceData* pRefData, const std::string& strGroup,
                                     std::vector<int>& vecValue);

    // The slot of a layer ID, through vector::at like the original.
    GameWorldLayer*& LayerAt(WORD wLayerID) { return m_vecLayer.at(wLayerID); }

    RefGameWorld*                               m_pRef;             // +4
    std::vector<RefInstanceWorldStartPosData*>  m_vecStartPos;      // +8
    std::vector<GameWorldLayer*>                m_vecLayer;         // +18
    std::list<GameWorldLayer*>                  m_listFree;         // +28
};

C_ASSERT(FIELD_OFFSET(CGameWorld, m_vecLayer) == 0x18);
C_ASSERT(FIELD_OFFSET(CGameWorld, m_listFree) == 0x28);
C_ASSERT(sizeof(CGameWorld) == 0x34);

// ---------------------------------------------------------------------------------------------
// CGameWorldMgr (0x2C bytes; static object 0x8A7AA8, g_pGameWorldMgr = dword_8A8950)

class CGameWorldMgr
{
public:
    typedef std::map<DWORD, CGameWorld*>            WorldMap;
    typedef std::map<DWORD, DWORD>                  InstanceMap;
    typedef std::map<DWORD, GameWorldReservation>   ReservationMap;

    CGameWorldMgr();                                    // CGameWorldMgr::StaticInit 0x65bb70
    virtual ~CGameWorldMgr();                           // 0x65bc90

    // GetSingleton_CGameWorldMgr (0x40ba90): asserts when there is no object.
    static CGameWorldMgr* GetInstance();

    void TickAll();                                     // 0x65bfe0
    CGameWorld* FindWorld(WORD wWorldID);               // 0x65c050
    void Clear();                                       // 0x65c0b0
    // CGameWorldMgr::IsCharInLayer (0x65c170): the layer the character was in still exists
    // with the same create time.
    static bool IsCharInLayer(const GameWorldCharPos* pChar);
    // CGameWorldMgr::OpenLayer (0x65c200): *pbyResult 1, 2 (registry) or 6 (no world) and the
    // results of CGameWorld::CreateLayer.
    GameWorldLayer* OpenLayer(BYTE* pbyResult, WORD wWorldID, const sGameWorldCreateParam* pParam);
    bool RegisterInstance(const DWORD* pdwKey, DWORD dwInstanceID);    // 0x65c3f0
    void UnregisterInstance(const DWORD* pdwKey);                       // 0x65c460
    // CGameWorldMgr::OnCharLeaveWorld (0x65c4b0): drops the reservation and enters the layer.
    BYTE EnterWorld(CUser* pUser, DWORD dwWorldLayer);
    // CGameWorldMgr::OnUserLogout (0x65c4f0): 2 (not in a world), 6 or LeaveLayer's result.
    BYTE OnUserLogout(CUser* pUser);
    void ReserveEnter(const WORD* pwWorldLayer, CUser* pUser);         // 0x65ca70
    void CancelReservation(CUser* pUser);                               // 0x65cb40
    void EraseReservation(DWORD dwCharID);                              // 0x65cbe0

    // Requests of the game servers.
    void OnMsg_7C14(CMsg* pMsg);                        // 0x65c890
    void OnMsg_7C16(CMsg* pMsg);                        // 0x65c8c0
    void OnOpenWorldRequest(CMsg* pMsg);                // 0x65c530
    void OnBanCloseWorld(CMsg* pMsg);                   // 0x65c2d0
    void OnCloseWorldRequest(CMsg* pMsg);               // 0x65c720
    void OnWorldListRequest(CMsg* pMsg);                // 0x65c7c0

    // 0x3C15 to the game servers: GameWorld_BroadcastOpened / Banned / Closed (0x65c900 /
    // 0x65c970 / 0x65c9f0).
    static void BroadcastOpened(WORD wWorldID, const GameWorldLayer* pLayer);
    static void BroadcastBanned(WORD wWorldID, WORD wLayerID);
    static void BroadcastClosed(WORD wWorldID, WORD wLayerID);
    // CGameWorldMgr::AllocReply_BC14 (0x65bd20).
    static CMsg* AllocReply_BC14(DWORD dwRequestID);

    WorldMap            m_mapWorld;             // +4   filled by DB_GameWorldInitialize
    InstanceMap         m_mapInstance;          // +10  world | layer << 16 -> instance ID
    DWORD               m_dwInstanceCounter;    // +1C
    ReservationMap      m_mapReservation;       // +20  by CharID
};

C_ASSERT(FIELD_OFFSET(CGameWorldMgr, m_dwInstanceCounter) == 0x1C);
C_ASSERT(sizeof(CGameWorldMgr) == 0x2C);

extern CGameWorldMgr* g_pGameWorldMgr;
extern CGameWorldMgr g_GameWorldMgr;
