#pragma once

#include <winsock2.h>
#include <windows.h>

#include "ServerFramework/ServerProcess.h"

// The crest uploader: its own process that stores a new guild / alliance crest (256 bytes) as
// .\Crest\<name>.crb and uploads it to the CREST_FTP server, then reports back to the main
// process. Request and answer are both 0x380E:
//   request {WORD type (5 guild, 6 alliance), DWORD d1, DWORD d2, DWORD d3, DWORD d4, crest}
//   answer  {WORD type, DWORD d1, DWORD d2, DWORD d3, DWORD d4, BYTE result (1 stored, 2 not)}
// The file name is "%c%u_%u_%u.crb": 'A' (alliance) or 'G', the shard ID, d3 and d4.

enum
{
    CREST_MSG_UPLOAD        = 0x380E,
    CREST_TYPE_GUILD        = 5,
    CREST_TYPE_ALLIANCE     = 6,
    CREST_SIZE              = 0x100,
};

struct CREST_DATA
{
    BYTE    ab[CREST_SIZE];
};

C_ASSERT(sizeof(CREST_DATA) == CREST_SIZE);

class CCrestUploader : public ServerFramework::CServerProcessBase
{
    BS_DECLARE_DYNCREATE(CCrestUploader)                                    // 0

public:
    // CCrestUploader::ctor (0x40ab40): registers the instance (asserts a second one).
    CCrestUploader();
    // CCrestUploader::dtor (0x40abd0)
    virtual ~CCrestUploader();                                              // 1

    // CCrestUploader::vf03 (0x40aca0): takes the messages queued so far (sleeps 1 ms when there
    // are none) and dispatches them until the stop flag is set or a pop fails.
    virtual DWORD Run(volatile LONG* pbStop, int nIndex);                  // 3
    // CCrestUploader::vf06 (0x40ac50): the base handlers and 0x380E.
    virtual void InitMsgHandlers();                                         // 6

    // CCrestUploader::OnMsg_380E (0x40b050)
    void OnMsg_380E(CMsg* pMsg, DWORD dwRelayID, DWORD dwContext,
                    ServerFramework::CMassiveMsg* pMassive);

public:
    DWORD   m_dw400B8;      // +400B8  not initialised
};

C_ASSERT(sizeof(CCrestUploader) == 0x400BC);

// 0x858850
extern CCrestUploader* g_pCrestUploader;

// CMainProcess_IsCrestFtpEnabled (0x40ac30): a CREST_FTP address is configured.
BOOL CMainProcess_IsCrestFtpEnabled();
// CCrestUploader_PostUploadRequest (0x412050): queues the request to the uploader; bGuild 1
// gives type 5 (guild), anything else 6 (alliance).
BOOL CCrestUploader_PostUploadRequest(DWORD dw1, DWORD dw2, BYTE bGuild, DWORD dw3, DWORD dw4,
                                      const CREST_DATA* pCrest);
