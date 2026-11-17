#include "FarmManagerMainProcess.h"

#include <string>

#include "ServerFramework/CertArch.h"
#include "ServerFramework/ModuleVersion.h"
#include "ServerFramework/ServerApp.h"
#include "ServerFramework/ServerDataExtern.h"

using namespace ServerFramework;

BS_IMPLEMENT_DYNCREATE(CFarmManagerMainProcess, &CServerProcessMain::classCServerProcessMain)

CFarmManagerMainProcess::CFarmManagerMainProcess()
{
}

CFarmManagerMainProcess::~CFarmManagerMainProcess()
{
}

BOOL CFarmManagerMainProcess::Initialize(void* pOwner, void* pParam)
{
    {
        std::string strHost("");
        m_UdpPing.Open(strHost);
    }
    return CServerProcessBase::Initialize(pOwner, pParam);
}

DWORD CFarmManagerMainProcess::Run(volatile LONG* pbStop, int nIndex)
{
    if (m_UdpPing.Resume())
        LogPrintf(0, "UDP Ping Service was Started!");
    else
        LogPrintf(0x2000000, "Failed To Start UDP Ping Service!");
    return CServerProcessBase::Run(pbStop, nIndex);
}

int CFarmManagerMainProcess::SignalStop()
{
    m_UdpPing.Suspend();
    m_UdpPing.Stop();
    return CServerProcessBase::SignalStop();
}

void CFarmManagerMainProcess::InitMsgHandlers()
{
    CServerProcessMain::InitMsgHandlers();
}

void CFarmManagerMainProcess::OnCordEstablished(SERVER_CORD* pCord, SERVER_BODY* pPeer)
{
    if (pPeer->pModule != CertModule_FindByName("GlobalManager"))
        return;
    ModuleVersion_FreeAllData();
    CMsg* pMsg = g_pBSNet->AllocMsg(FALSE);
    pMsg->SetOpcode(FARM_MSG_PATCH_RESET);
    g_pBSNet->SendMsg(pCord->dwSessionID, pMsg);
    g_pBSNet->ReleaseMsg(pMsg);
}
