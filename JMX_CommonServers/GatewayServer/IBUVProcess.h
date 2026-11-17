#pragma once

#include <winsock2.h>
#include <windows.h>
#include <deque>
#include <map>
#include <string>

#include "BSLib/ChunkAllocator.h"
#include "BSLib/SingletonT.h"
#include "ServerFramework/ServerProcess.h"
#include "CertifyParam.h"

// IBUV ("image based user validation"): the captcha of the login. Accounts the GlobalManager
// marks for it (0xA200 result 1, or a wrong captcha) get an image from the IBUV process (task
// GATEWAY_TASK_IBUV): 0x6321 {CERTIFY_PARAM*} to its queue, 0xA321 back to the main process, the
// image goes to the client (0x2322) and the code waits in CIBUVTemporalCertParamMgr until the
// client answers (0x6323). The images are drawn by ImageTrans.dll.

enum
{
    IBUV_MSG_IMAGE_REQ      = 0x6321,   // {DWORD context}
    IBUV_MSG_IMAGE_ACK      = 0xA321,   // {DWORD context, BYTE result, (1:) image}
    IBUV_MSG_IMAGE_TO_USER  = 0x2322,   // {BYTE, WORD size, image}

    IBUV_IMAGE_SIZE         = 0x800,
    IBUV_IMAGE_WIDTH        = 200,
    IBUV_IMAGE_HEIGHT       = 64,
};

// ?fnImageTransToMemW@@YAHPAEJJJPA_WJJJJJJJ@Z: draws pszText into pBuffer; the size of the
// image, 0 on errors. The seven trailing values are the distortion parameters.
typedef int (__cdecl* PFN_IMAGETRANS_TO_MEM_W)(BYTE* pBuffer, long cbBuffer, long nWidth,
                                               long nHeight, wchar_t* pszText, long n5, long n6,
                                               long n7, long n8, long n9, long n10, long n11);
// ?fnImageTransSetCodePage@@YAHK@Z
typedef int (__cdecl* PFN_IMAGETRANS_SET_CODE_PAGE)(unsigned long dwCodePage);

// One prepared image (0x844 bytes), pooled in g_IBUVDataPool.
class IBUVData
{
public:
    // 0x419150
    IBUVData()
    {
        m_bInUse = 0;
    }

    // ReleaseToPool (0x419200): back to g_IBUVDataPool when taken from it.
    virtual void __stdcall Release();                           // 0

    // IBUVImage_ResetAndGenerateCode (0x419250): clears the image and draws a new code of the
    // IBUV process's string size from its character set (one rand() per character).
    void ResetAndGenerateCode();
    // IBUVData_WriteToMsg (0x419100): BYTE m_b4, the code, WORD size, the image.
    void WriteToMsg(CMsg* pMsg);

public:
    BYTE            m_b4;                   // +4   always 0
    std::wstring    m_wstrCode;             // +8   text drawn into the image
    std::wstring    m_wstrAnswer;           // +24  sent to the main process
    WORD            m_wImageSize;           // +40
    BYTE            m_abImage[IBUV_IMAGE_SIZE]; // +42
    BYTE            m_bInUse;               // +842
};

C_ASSERT(FIELD_OFFSET(IBUVData, m_wstrCode) == 0x8);
C_ASSERT(FIELD_OFFSET(IBUVData, m_wstrAnswer) == 0x24);
C_ASSERT(FIELD_OFFSET(IBUVData, m_wImageSize) == 0x40);
C_ASSERT(FIELD_OFFSET(IBUVData, m_bInUse) == 0x842);
C_ASSERT(sizeof(IBUVData) == 0x844);

// 0x53B3B0
extern CChunkAllocatorST<IBUVData> g_IBUVDataPool;

// IBUVImage_InitAndMarkReady (0x4191f0): a pooled image marked as taken.
IBUVData* IBUVData_Alloc();

// The image process (task GATEWAY_TASK_IBUV, one thread). Keeps m_dwQueueReserveCount images
// ready and answers 0x6321 from that queue.
class CIBUVProcess : public ServerFramework::CServerProcessBase,
                     public CSingletonT<CIBUVProcess>
{
    BS_DECLARE_DYNCREATE(CIBUVProcess)                                          // 0

public:
    // 0x419430
    CIBUVProcess();
    // 0x419590: unloads ImageTrans.dll and releases the prepared images.
    virtual ~CIBUVProcess();                                                    // 1

    // MessageLoop (0x419920): messages of the queue, otherwise new images every 100 ms.
    virtual DWORD Run(volatile LONG* pbStop, int nIndex);                      // 3
    // 0x4198c0: base handlers and 0x6321.
    virtual void InitMsgHandlers();                                             // 6
    // 0x4198e0
    virtual void OnUnhandledMsg(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                                ServerFramework::CMassiveMsg* pMassive);        // 9

    // Initialize (0x4196b0): loads ImageTrans.dll, seeds rand() and takes the character set
    // (converted with the code page). False (asserted) on errors.
    bool Initialize(const char* pszCharSet, CTask* pOwnerTask, DWORD dwQueueReserveCount,
                    float fQueuePrepareRatio, int nStringSize);
    // OnMsg_6321_IBUVImageReq (0x419c20)
    void OnMsg_6321(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);

    // IBUV_GenerateAndQueueImage (0x419a30): one new image at the end of the queue (only while
    // enabled).
    BOOL GenerateAndQueueImage();
    // IBUV_CreateImage (0x419a90): draws the code of pData with random distortion.
    BOOL CreateImage(IBUVData* pData);
    // CIBUVProcess_TickCreateImages (0x419b80): every 100 ms, ratio * (reserve - queued) images
    // (at least one) while fewer than the reserve are queued.
    void TickCreateImages();

public:
    BOOL                        m_bEnabled;             // +400BC
    DWORD                       m_dwLastTick;           // +400C0
    HMODULE                     m_hImageTrans;          // +400C4
    PFN_IMAGETRANS_TO_MEM_W     m_pfnImageTransToMemW;  // +400C8
    CTask*                      m_pOwnerTask;           // +400CC main process, gets 0xA321
    std::deque<IBUVData*>       m_deqImage;             // +400D0
    DWORD                       m_dwQueueReserveCount;  // +400E4
    float                       m_fQueuePrepareRatio;   // +400E8
    DWORD                       m_dwCodePage;           // +400EC
    DWORD                       m_nStringSize;          // +400F0
    WCHAR                       m_szCharSet[256];       // +400F4
};

C_ASSERT(FIELD_OFFSET(CIBUVProcess, m_bEnabled) == 0x400BC);
C_ASSERT(FIELD_OFFSET(CIBUVProcess, m_pOwnerTask) == 0x400CC);
C_ASSERT(FIELD_OFFSET(CIBUVProcess, m_deqImage) == 0x400D0);
C_ASSERT(FIELD_OFFSET(CIBUVProcess, m_dwQueueReserveCount) == 0x400E4);
C_ASSERT(FIELD_OFFSET(CIBUVProcess, m_dwCodePage) == 0x400EC);
C_ASSERT(FIELD_OFFSET(CIBUVProcess, m_szCharSet) == 0x400F4);
C_ASSERT(sizeof(CIBUVProcess) == 0x402F4);

// Failed captchas per client address (0x53B398). Without a tolerance nothing is counted.
class CIBUVIPBlockMgr : public CSingletonT<CIBUVIPBlockMgr>
{
public:
    // CIBUVIPBlockMgr::CreateGlobal (0x417b30)
    CIBUVIPBlockMgr();
    // 0x417c30
    virtual ~CIBUVIPBlockMgr();                                 // 0

    // 0x417cd0
    void SetFailureIPTolerance(DWORD dwTolerance)
    {
        m_dwFailureIPTolerance = dwTolerance;
    }
    // CaptchaMap_IncrementOrInsert (0x417ce0)
    void AddFailure(DWORD dwIP);
    // STL_tree_IsKeyAbsent_LoginMap (0x417d60): more failures than tolerated.
    bool IsBlocked(DWORD dwIP);

public:
    DWORD                   m_dwFailureIPTolerance;     // +4
    std::map<DWORD, DWORD>  m_mapFailure;               // +8   address -> failed captchas
};

C_ASSERT(FIELD_OFFSET(CIBUVIPBlockMgr, m_mapFailure) == 0x8);
C_ASSERT(sizeof(CIBUVIPBlockMgr) == 0x14);

// The logins waiting for their captcha answer, by session (0x53B3F0).
class CIBUVTemporalCertParamMgr : public CSingletonT<CIBUVTemporalCertParamMgr>
{
public:
    // DynInit_CIBUVTemporalCertParamMgr (0x41b700)
    CIBUVTemporalCertParamMgr();
    // 0x41b7f0
    virtual ~CIBUVTemporalCertParamMgr();                       // 0

    // 0x41b890: false (and logged when the entry has a record) when the session has one.
    bool Register(DWORD dwSessionID, sCertifyParam* pParam);
    // 0x41b940: NULL when the session has none.
    sCertifyParam* FindBySession(DWORD dwSessionID);
    // 0x41b9a0
    void Remove(DWORD dwSessionID);

public:
    std::map<DWORD, sCertifyParam*> m_mapParam;         // +4
};

C_ASSERT(sizeof(CIBUVTemporalCertParamMgr) == 0x10);

// 0x53B398 and 0x53B3F0
extern CIBUVIPBlockMgr g_IBUVIPBlockMgr;
extern CIBUVTemporalCertParamMgr g_IBUVTemporalCertParamMgr;

// The functions the application object and the main process call (CertifierFwd.h).
CRuntimeClass* IBUVProcess_GetRuntimeClass();
bool IBUVProcess_Initialize(CIBUVProcess* pProcess, CTask* pOwnerTask, DWORD dwQueueReserveCount,
                            float fQueuePrepareRatio, int nStringSize, const char* pszCharSet);
void IBUVIPBlockMgr_SetFailureIPTolerance(CIBUVIPBlockMgr* pMgr, DWORD dwTolerance);
void IBUVTemporalCertParamMgr_Remove(CIBUVTemporalCertParamMgr* pMgr, DWORD dwSessionID);
// RequestIBUVImage (0x407300): 0x6321 {pParam} to the queue of the IBUV process.
void RequestIBUVImage(sCertifyParam* pParam);
