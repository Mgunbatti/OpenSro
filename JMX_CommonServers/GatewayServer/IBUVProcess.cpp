#include "IBUVProcess.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "BSNet/IBSNet.h"
#include "ServerFramework/MassiveMsg.h"
#include "ServerFramework/ServerDataExtern.h"

using namespace ServerFramework;

CChunkAllocatorST<IBUVData> g_IBUVDataPool;
CIBUVIPBlockMgr g_IBUVIPBlockMgr;
CIBUVTemporalCertParamMgr g_IBUVTemporalCertParamMgr;

// The singletons: the original constructors assert that no instance was registered before;
// CSingletonT registers the instance before the derived constructor runs.

// ---------------------------------------------------------------------------------------------
// IBUVData

void __stdcall IBUVData::Release()
{
    if (m_bInUse)
    {
        m_bInUse = 0;
        g_IBUVDataPool.Free(this);
    }
}

IBUVData* IBUVData_Alloc()
{
    IBUVData* pData = g_IBUVDataPool.Alloc();
    pData->m_bInUse = 1;
    return pData;
}

void IBUVData::ResetAndGenerateCode()
{
    m_b4 = 0;
    m_wstrCode.clear();
    m_wstrAnswer.clear();
    m_wImageSize = 0;
    memset(m_abImage, 0, sizeof(m_abImage));
    m_b4 = 0;

    WCHAR szCode[255];
    memset(szCode, 0, sizeof(szCode));
    if (!CSingletonT<CIBUVProcess>::GetInstance())
        BSAssertFail();
    const WCHAR* pszCharSet = CSingletonT<CIBUVProcess>::GetInstance()->m_szCharSet;
    int nCharSet = lstrlenW(pszCharSet);
    CIBUVProcess* pProcess;
    for (DWORD i = 0;; ++i)
    {
        pProcess = CSingletonT<CIBUVProcess>::GetInstance();
        if (!pProcess)
        {
            BSAssertFail();
            pProcess = CSingletonT<CIBUVProcess>::GetInstance();
        }
        if (i >= pProcess->m_nStringSize)
            break;
        szCode[i] = pszCharSet[rand() % nCharSet];
    }
    if ((DWORD)lstrlenW(szCode) != pProcess->m_nStringSize)
        BSAssertFail();
    m_wstrCode.assign(szCode, wcslen(szCode));
    m_wstrAnswer.assign(szCode, wcslen(szCode));
}

void IBUVData::WriteToMsg(CMsg* pMsg)
{
    pMsg->WriteBytes(&m_b4, 1);
    *pMsg << m_wstrAnswer.c_str();
    pMsg->WriteBytes(&m_wImageSize, 2);
    pMsg->WriteBytes(m_abImage, m_wImageSize);
}

// ---------------------------------------------------------------------------------------------
// CIBUVProcess

BS_IMPLEMENT_DYNCREATE(CIBUVProcess, &CServerProcessBase::classCServerProcessBase)

CIBUVProcess::CIBUVProcess()
{
    m_bEnabled = FALSE;
    m_dwLastTick = 0;
    m_hImageTrans = NULL;
    m_pfnImageTransToMemW = NULL;
    m_pOwnerTask = NULL;
    m_fQueuePrepareRatio = 0.05f;
    m_dwQueueReserveCount = 20000;
    m_dwCodePage = 949;
    m_nStringSize = 6;
    memset(m_szCharSet, 0, sizeof(m_szCharSet));
}

CIBUVProcess::~CIBUVProcess()
{
    if (m_hImageTrans)
        FreeLibrary(m_hImageTrans);
    while (m_deqImage.size())
    {
        IBUVData* pData = m_deqImage.front();
        m_deqImage.pop_front();
        pData->Release();
    }
}

// BUG(GWCERT-003): rand() is seeded on the calling thread, but the images are made on the IBUV
// thread, whose rand() state the CRT starts at seed 1: codes and distortions repeat after every
// start.
// BUG(GWCERT-004): the 20000 images made here are never made (the process is enabled only
// afterwards), and the configured reserve count does not take part.
bool CIBUVProcess::Initialize(const char* pszCharSet, CTask* pOwnerTask,
                              DWORD dwQueueReserveCount, float fQueuePrepareRatio,
                              int nStringSize)
{
    const char* pszError;
    if ((DWORD)nStringSize > 6)
    {
        LogPrintf(0x2000001, "IBUV: Too Long Image String Size : %d !", nStringSize);
        BSAssertFail();
        return false;
    }
    m_nStringSize = nStringSize;
    m_dwCodePage = 1258;
    m_hImageTrans = LoadLibraryA("ImageTrans.dll");
    if (!m_hImageTrans)
    {
        pszError = "IBUV: Failed To Load ImageTrans.dll";
        goto fail;
    }
    {
        PFN_IMAGETRANS_SET_CODE_PAGE pfnSetCodePage = (PFN_IMAGETRANS_SET_CODE_PAGE)GetProcAddress(
            m_hImageTrans, "?fnImageTransSetCodePage@@YAHK@Z");
        if (!pfnSetCodePage)
        {
            pszError = "IBUV: Failed To Get ImageTransSetCodePage() Pointer from ImageTrans.dll";
            goto fail;
        }
        pfnSetCodePage(m_dwCodePage);
    }
    m_pfnImageTransToMemW = (PFN_IMAGETRANS_TO_MEM_W)GetProcAddress(
        m_hImageTrans, "?fnImageTransToMemW@@YAHPAEJJJPA_WJJJJJJJ@Z");
    if (!m_pfnImageTransToMemW)
    {
        pszError = "IBUV: Failed To Get ImageTransToMemW() Pointer from ImageTrans.dll";
        goto fail;
    }
    srand((unsigned)time(NULL));
    m_fQueuePrepareRatio = fQueuePrepareRatio;
    m_dwQueueReserveCount = dwQueueReserveCount;
    m_pOwnerTask = pOwnerTask;
    if (!pOwnerTask)
    {
        pszError = "IBUV: MainProcTask Pointer is null";
        goto fail;
    }
    if (!pszCharSet || !strlen(pszCharSet))
    {
        pszError = "IBUV: Invalid IBUV CharSet!!!";
        goto fail;
    }
    {
        WCHAR szCharSet[1024];
        memset(szCharSet, 0, sizeof(szCharSet));
        if (MultiByteToWideChar(m_dwCodePage, 0, pszCharSet, (int)strlen(pszCharSet), szCharSet,
                                1024) > 255)
        {
            pszError = "IBUV: Invalid IBUV CharSet Size, TOO LONG!!!";
            goto fail;
        }
        LogPrintf(0x2000001, "IBUV: CharSet(%s)", pszCharSet);
        wcscpy_s(m_szCharSet, 0xFF, szCharSet);
    }
    for (int i = 20000; i; --i)
        GenerateAndQueueImage();
    LogPrintf(0x2000001, "IBUV: Preserved image created : (dwImageCount)");
    m_bEnabled = TRUE;
    return true;

fail:
    LogPrintf(0x2000001, pszError);
    BSAssertFail();
    return false;
}

void CIBUVProcess::InitMsgHandlers()
{
    CServerProcessBase::InitMsgHandlers();
    RegisterHandler(IBUV_MSG_IMAGE_REQ, &CIBUVProcess::OnMsg_6321);
}

void CIBUVProcess::OnUnhandledMsg(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    WORD* pOpcode = pMsg->m_pOpcode;
    pMsg->SkipRemaining();
    LogPrintf(0x1000000, "CIBUVProcess::Unhandled Msg: 0x%x", *pOpcode);
}

DWORD CIBUVProcess::Run(volatile LONG* pbStop, int nIndex)
{
    if (*pbStop)
        return 0;
    m_dwLastTick = GetTickCount();
    while (!*pbStop)
    {
        CTask* pTask = (CTask*)m_pOwner;
        CMsg* pMsg = NULL;
        if (!pTask->m_pQueue)
            BSAssertFail();
        LONG nCount = pTask->m_pQueue->GetSize();
        if (nCount > 0)
        {
            for (LONG i = 0; i < nCount; ++i)
            {
                if (((CTask*)m_pOwner)->m_pQueue->Pop(&pMsg, 1, FALSE) != 1)
                    goto closed;
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
            TickCreateImages();
            Sleep(1);
        }
    }
closed:
    LogPrintf(0x2000000, "CIBUVProcess Thread Closed!");
    return 0;
}

// BUG(GWCERT-010): when the image cannot be drawn the pooled record is not given back.
BOOL CIBUVProcess::GenerateAndQueueImage()
{
    if (!m_bEnabled)
        return FALSE;
    IBUVData* pData = IBUVData_Alloc();
    if (!pData)
    {
        BSAssertFail();
        return FALSE;
    }
    pData->ResetAndGenerateCode();
    if (!CreateImage(pData))
        return FALSE;
    m_deqImage.push_back(pData);
    return TRUE;
}

BOOL CIBUVProcess::CreateImage(IBUVData* pData)
{
    if (!m_pfnImageTransToMemW)
        return FALSE;
    long n7 = rand() % 21 - 10;
    long n8 = rand() % 21 + 100;
    long n9 = rand() % 41 + 20;
    long n10 = rand() % 601 - 300;
    long n11 = rand() % 2 + 2;
    int nSize = m_pfnImageTransToMemW(pData->m_abImage, IBUV_IMAGE_SIZE, IBUV_IMAGE_WIDTH,
                                      IBUV_IMAGE_HEIGHT, (wchar_t*)pData->m_wstrCode.c_str(), 0, 0,
                                      n7, n8, n9, 100 * n10, n11);
    if (nSize && (unsigned)nSize <= IBUV_IMAGE_SIZE)
    {
        pData->m_wImageSize = (WORD)nSize;
        return TRUE;
    }
    LogPrintf(0x2000001, "IBUV: Failed To Create Image!!!");
    BSAssertFail();
    return FALSE;
}

void CIBUVProcess::TickCreateImages()
{
    if (!m_bEnabled)
        return;
    DWORD dwNow = GetTickCount();
    if (dwNow - m_dwLastTick < 100)
        return;
    DWORD dwQueued = (DWORD)m_deqImage.size();
    m_dwLastTick = dwNow;
    if (m_dwQueueReserveCount <= dwQueued)
        return;
    DWORD dwCount = (DWORD)(__int64)(m_fQueuePrepareRatio
                                      * (double)(m_dwQueueReserveCount - dwQueued));
    if (dwCount < 1)
        dwCount = 1;
    do
    {
        GenerateAndQueueImage();
    } while (--dwCount);
}

void CIBUVProcess::OnMsg_6321(CMsg* pMsg, DWORD, DWORD, CMassiveMsg*)
{
    DWORD dwContext;
    *pMsg >> dwContext;
    if (!m_deqImage.size())
    {
        if (!GenerateAndQueueImage())
        {
            LogPrintf(0x2000001, "IBUV: Failed to get Image!!!!");
            CMsg* pAck = g_pBSNet->AllocMsg(FALSE);
            pAck->SetOpcode(IBUV_MSG_IMAGE_ACK);
            BYTE byResult = 2;
            pAck->WriteBytes(&dwContext, 4);
            pAck->WriteBytes(&byResult, 1);
            m_pOwnerTask->m_pQueue->Push(pAck);
            return;
        }
        LogPrintf(0x2000001, "IBUV: Not enough preserved images. generated..");
    }
    IBUVData* pData = m_deqImage.front();
    m_deqImage.pop_front();
    CMsg* pAck = g_pBSNet->AllocMsg(FALSE);
    pAck->SetOpcode(IBUV_MSG_IMAGE_ACK);
    BYTE byResult = 1;
    pAck->WriteBytes(&dwContext, 4);
    pAck->WriteBytes(&byResult, 1);
    pData->WriteToMsg(pAck);
    m_pOwnerTask->m_pQueue->Push(pAck);
    pData->Release();
}

CRuntimeClass* IBUVProcess_GetRuntimeClass()
{
    return &CIBUVProcess::classCIBUVProcess;
}

bool IBUVProcess_Initialize(CIBUVProcess* pProcess, CTask* pOwnerTask, DWORD dwQueueReserveCount,
                            float fQueuePrepareRatio, int nStringSize, const char* pszCharSet)
{
    return pProcess->Initialize(pszCharSet, pOwnerTask, dwQueueReserveCount, fQueuePrepareRatio,
                                nStringSize);
}

// ---------------------------------------------------------------------------------------------
// CIBUVIPBlockMgr

CIBUVIPBlockMgr::CIBUVIPBlockMgr()
{
    m_dwFailureIPTolerance = 0;
}

CIBUVIPBlockMgr::~CIBUVIPBlockMgr()
{
    m_mapFailure.clear();
}

void CIBUVIPBlockMgr::AddFailure(DWORD dwIP)
{
    if (!m_dwFailureIPTolerance)
        return;
    std::map<DWORD, DWORD>::iterator it = m_mapFailure.find(dwIP);
    if (it == m_mapFailure.end())
        m_mapFailure.insert(std::make_pair(dwIP, (DWORD)1));
    else
        ++m_mapFailure[dwIP];
}

// BUG(GWCERT-011): the counts are never reset, so an address stays blocked until the restart.
bool CIBUVIPBlockMgr::IsBlocked(DWORD dwIP)
{
    if (!m_dwFailureIPTolerance)
        return false;
    std::map<DWORD, DWORD>::iterator it = m_mapFailure.find(dwIP);
    if (it == m_mapFailure.end())
        return false;
    return m_dwFailureIPTolerance < it->second;
}

void IBUVIPBlockMgr_SetFailureIPTolerance(CIBUVIPBlockMgr* pMgr, DWORD dwTolerance)
{
    pMgr->SetFailureIPTolerance(dwTolerance);
}

// ---------------------------------------------------------------------------------------------
// CIBUVTemporalCertParamMgr

CIBUVTemporalCertParamMgr::CIBUVTemporalCertParamMgr()
{
}

CIBUVTemporalCertParamMgr::~CIBUVTemporalCertParamMgr()
{
    m_mapParam.clear();
}

bool CIBUVTemporalCertParamMgr::Register(DWORD dwSessionID, sCertifyParam* pParam)
{
    std::map<DWORD, sCertifyParam*>::iterator it = m_mapParam.find(dwSessionID);
    if (it == m_mapParam.end())
    {
        m_mapParam.insert(std::make_pair(dwSessionID, pParam));
        return true;
    }
    sCertifyParam* pOld = it->second;
    if (pOld)
    {
        LogPrintf(0x2000001,
                  "Duplicated IBUV CertParam Registration Request!, User Info (Session: %d, "
                  "Account: %s)",
                  pOld->dwSessionID, pOld->strID.c_str());
    }
    return false;
}

sCertifyParam* CIBUVTemporalCertParamMgr::FindBySession(DWORD dwSessionID)
{
    std::map<DWORD, sCertifyParam*>::iterator it = m_mapParam.find(dwSessionID);
    if (it == m_mapParam.end())
        return NULL;
    return it->second;
}

void CIBUVTemporalCertParamMgr::Remove(DWORD dwSessionID)
{
    std::map<DWORD, sCertifyParam*>::iterator it = m_mapParam.find(dwSessionID);
    if (it != m_mapParam.end())
        m_mapParam.erase(it);
}

void IBUVTemporalCertParamMgr_Remove(CIBUVTemporalCertParamMgr* pMgr, DWORD dwSessionID)
{
    pMgr->Remove(dwSessionID);
}
