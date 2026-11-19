#include "ServerCommon/PartyInfo.h"

// ---------------------------------------------------------------------------------------------
// sPartyMemberInfo

BYTE sPartyMemberInfo::Read(CMsg* pMsg)
{
    BYTE bMask = 0;
    *pMsg >> bMask;
    if (bMask & PARTY_MEMBER_ID)
        *pMsg >> m_dwID;
    if (bMask & PARTY_MEMBER_NAME)
    {
        *pMsg >> m_strName;
        *pMsg >> m_dwRefObjID;
    }
    if (bMask & PARTY_MEMBER_LEVEL)
        *pMsg >> m_bLevel;
    if (bMask & PARTY_MEMBER_HPMP)
        *pMsg >> m_bHPMP;
    if (bMask & PARTY_MEMBER_POSITION)
    {
        *pMsg >> m_wRegion;
        if (m_wRegion >= 0)
        {
            m_dwX = 0;
            m_dwZ = 0;
            m_dwY = 0;
            *pMsg >> *(WORD*)&m_dwX;
            *pMsg >> *(WORD*)&m_dwY;
            *pMsg >> *(WORD*)&m_dwZ;
        }
        else
        {
            *pMsg >> m_dwX;
            *pMsg >> m_dwY;
            *pMsg >> m_dwZ;
        }
    }
    if (bMask & PARTY_MEMBER_POSITION)
        *pMsg >> m_dwAngle;
    if (bMask & PARTY_MEMBER_GUILD)
        *pMsg >> m_strGuildName;
    if (bMask & PARTY_MEMBER_B41)
        *pMsg >> m_bPartyType;
    if (bMask & PARTY_MEMBER_MASTERY)
    {
        *pMsg >> m_dwMastery1;
        *pMsg >> m_dwMastery2;
    }
    return bMask;
}

void sPartyMemberInfo::Write(CMsg* pMsg, BYTE bMask) const
{
    pMsg->WriteBytes(&bMask, 1);
    if (bMask & PARTY_MEMBER_ID)
        pMsg->WriteBytes(&m_dwID, 4);
    if (bMask & PARTY_MEMBER_NAME)
    {
        *pMsg << m_strName.c_str();
        pMsg->WriteBytes(&m_dwRefObjID, 4);
    }
    if (bMask & PARTY_MEMBER_LEVEL)
        pMsg->WriteBytes(&m_bLevel, 1);
    if (bMask & PARTY_MEMBER_HPMP)
        pMsg->WriteBytes(&m_bHPMP, 1);
    if (bMask & PARTY_MEMBER_POSITION)
    {
        if (m_wRegion >= 0)
        {
            WORD wX = (WORD)m_dwX;
            WORD wY = (WORD)m_dwY;
            WORD wZ = (WORD)m_dwZ;
            pMsg->WriteBytes(&m_wRegion, 2);
            pMsg->WriteBytes(&wX, 2);
            pMsg->WriteBytes(&wY, 2);
            pMsg->WriteBytes(&wZ, 2);
        }
        else
        {
            pMsg->WriteBytes(&m_wRegion, 2);
            pMsg->WriteBytes(&m_dwX, 4);
            pMsg->WriteBytes(&m_dwY, 4);
            pMsg->WriteBytes(&m_dwZ, 4);
        }
    }
    if (bMask & PARTY_MEMBER_POSITION)
        pMsg->WriteBytes(&m_dwAngle, 4);
    if (bMask & PARTY_MEMBER_GUILD)
        *pMsg << m_strGuildName.c_str();
    if (bMask & PARTY_MEMBER_B41)
        pMsg->WriteBytes(&m_bPartyType, 1);
    if (bMask & PARTY_MEMBER_MASTERY)
    {
        pMsg->WriteBytes(&m_dwMastery1, 4);
        pMsg->WriteBytes(&m_dwMastery2, 4);
    }
}

// ---------------------------------------------------------------------------------------------
// sPartyInfo / sPartySetting

void sPartyInfo::Write(CMsg* pMsg) const
{
    pMsg->WriteBytes(&m_dwPartyID, 4);
    pMsg->WriteBytes(&m_dw04, 4);
    pMsg->WriteBytes(&m_dw08, 4);
    pMsg->WriteBytes(&m_bSetting, 1);
    m_Member.Write(pMsg, PARTY_MEMBER_ALL);
}

void sPartySetting::Read(CMsg* pMsg)
{
    *pMsg >> m_dw00;
    *pMsg >> m_b04;
    *pMsg >> m_b05;
    *pMsg >> m_w06;
}
