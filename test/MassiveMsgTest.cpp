#include "ServerFramework/MassiveMsg.h"
#include "BSNet/MsgPool.h"

#include <string.h>
#include <vector>

#include "TestCheck.h"

using namespace ServerFramework;

// Minimal engine: messages from a CMsgPool with reference counts, sends are recorded.
class CFakeNet : public IBSNet
{
public:
    CMsgPool m_Pool;
    std::vector<CMsg*> m_Sent;

    virtual CMsg* __stdcall AllocMsg(BOOL bEncrypted)
    {
        CMsg* p = m_Pool.Alloc();
        if (bEncrypted == 1)
            p->SetEncryptFlag();
        InterlockedIncrement(&p->m_lRefCount);
        return p;
    }
    virtual LONG __stdcall ReleaseMsg(CMsg* p)
    {
        if (p->m_lRefCount > 0 && InterlockedDecrement(&p->m_lRefCount) == 0)
            m_Pool.Free(p);
        return 0;
    }
    virtual int __stdcall SendMsg(DWORD, CMsg* p)
    {
        // the real engine copies or queues the message; keep a reference like it does
        InterlockedIncrement(&p->m_lRefCount);
        m_Sent.push_back(p);
        return 1;
    }

    virtual HRESULT __stdcall QueryInterface(const GUID&, void**) { return 0; }
    virtual ULONG __stdcall AddRef() { return 0; }
    virtual int __stdcall Shutdown() { return 0; }
    virtual BOOL __stdcall Initialize(BSNET_INIT_PARAM) { return 0; }
    virtual DWORD __stdcall ConnectSockAddr(const sockaddr*, const sockaddr*, DWORD, int)
    {
        return 0;
    }
    virtual DWORD __stdcall Connect(const char*, u_short, const char*, DWORD, int) { return 0; }
    virtual BOOL __stdcall ListenSockAddr(const sockaddr*) { return 0; }
    virtual BOOL __stdcall Listen(const char*, u_short) { return 0; }
    virtual BOOL __stdcall RegisterProactor(DWORD, CTask*, CRuntimeClass*, void*) { return 0; }
    virtual int __stdcall vf09(DWORD, DWORD, DWORD, DWORD) { return 0; }
    virtual BOOL __stdcall StartProactorWorkers(DWORD, int, int, unsigned, DWORD,
                                                THREAD_AFFINITY*)
    {
        return 0;
    }
    virtual BOOL __stdcall ProactorResume(DWORD) { return 0; }
    virtual BOOL __stdcall ProactorSuspend(DWORD) { return 0; }
    virtual BOOL __stdcall UnregisterProactor(DWORD) { return 0; }
    virtual void __stdcall GetStatistics(DWORD, NETENGINE_STATISTICS*) {}
    virtual BOOL __stdcall GetSessionPeerAddress(DWORD, std::string*, ULONG*, u_short*)
    {
        return 0;
    }
    virtual CTask* __stdcall GetProactor(DWORD) { return 0; }
    virtual BOOL __stdcall StartHandshake(DWORD, SECURITY_HANDSHAKE_PARAM*) { return 0; }
    virtual BOOL __stdcall BeginSessionFileTransfer(DWORD, std::string, DWORD, DWORD, int)
    {
        return 0;
    }
    virtual BOOL __stdcall SendFile(DWORD, const std::string&, DWORD, int) { return 0; }
    virtual int __stdcall DisconnectSession(DWORD, DWORD) { return 0; }
    virtual int __stdcall CloseSessionTransferFile(DWORD, DWORD) { return 0; }
    virtual void __stdcall ExcludeSessionFromCleanup(DWORD) {}
    virtual int __stdcall GetSessionSocketState(DWORD) { return 0; }
    virtual BOOL __stdcall RemoveKeepAliveTarget(DWORD) { return 0; }
    virtual void __stdcall AddKeepAliveTarget(const char*, u_short, const char*, DWORD, DWORD) {}
    virtual void __stdcall CloseAllFiles() {}
    virtual void __stdcall DumpMsgPool(BYTE, DWORD) {}
    virtual void __stdcall SetDebugAsserted(BYTE) {}
    virtual void __stdcall CleanupDisconnectedSessions() {}
    virtual CMsgPool* __stdcall GetMsgPool() { return &m_Pool; }
    virtual BOOL __stdcall GetSessionLocalMacAddress(DWORD, BYTE*) { return 0; }
    virtual void __stdcall SetSecurityMode(BYTE) {}
    virtual void __stdcall PrintVersion() {}
    virtual BOOL __stdcall RegisterDatagramProactor(DWORD) { return 0; }
    virtual int __stdcall OpenDatagramSocket(WORD) { return 0; }
    virtual int __stdcall SendDatagram(const sockaddr*, CMsg*) { return 0; }
};

int main()
{
    CFakeNet net;
    g_pBSNet = &net;

    // 10000 bytes plus a string: needs three parts of up to 4089 payload bytes each
    static BYTE data[10000];
    for (int i = 0; i < 10000; ++i)
        data[i] = (BYTE)(i * 7);

    CMassiveMsg* pOut = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pOut->SetOpcode(0x3013);
    *pOut << (DWORD)0xCAFEBABE;
    for (int i = 0; i < 10000; i += 1000)
        pOut->Write(data + i, 1000);
    pOut->WriteString("end");
    CHECK(pOut->m_MsgList.size() == 3);
    CHECK(pOut->GetUnreadSize() == 4 + 10000 + 5 + 3);   // +1 part byte per packet

    char sz[256];
    CHECK(strcmp(pOut->FormatDesc(sz), "(Massive)NoDir:0x13 (3), 10012") == 0);

    pOut->SendTo(1, FALSE);
    CHECK(net.m_Sent.size() == 4);
    CHECK(g_MassiveMsgPool.m_nInUse == 0);

    // header packet: BYTE 1, WORD part count, WORD inner opcode
    CMsg* pHeader = net.m_Sent[0];
    CHECK(pHeader->GetOpcode() == MASSIVE_MSG_OPCODE && pHeader->GetPayloadSize() == 5);
    CHECK(GetMassiveOpcode(pHeader, NULL) == 0x3013);
    CHECK(pHeader->m_wReadPos == MSG_HEADER_SIZE);
    BYTE bType;
    WORD wCount, wOpcode;
    *pHeader >> bType >> wCount >> wOpcode;
    CHECK(bType == 1 && wCount == 3 && wOpcode == 0x3013);

    // SendTo marked the parts as read when it released them; a real receive hands over fresh
    // messages, so rewind them before feeding them back in.
    CHECK(net.m_Sent[1]->GetUnreadSize() == 0);
    for (size_t i = 1; i < net.m_Sent.size(); ++i)
        net.m_Sent[i]->m_wReadPos = MSG_HEADER_SIZE;

    // receive side
    CMassiveMsg* pIn = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    pIn->SetRecvHeader(wOpcode, wCount);
    CHECK(GetMassiveOpcode(net.m_Sent[1], pIn) == 0x3013);
    CHECK(!pIn->AddRecvPart(net.m_Sent[1]));
    CHECK(!pIn->AddRecvPart(net.m_Sent[2]));
    CHECK(pIn->AddRecvPart(net.m_Sent[3]));
    pIn->BeginRead();

    DWORD dwMagic;
    *pIn >> dwMagic;
    CHECK(dwMagic == 0xCAFEBABE);
    // reads, like writes, may cross only one part boundary per call (SRVFW-002)
    static BYTE back[10000];
    for (int i = 0; i < 10000; i += 1000)
        pIn->Read(back + i, 1000);
    CHECK(memcmp(back, data, 10000) == 0);
    WORD wLen;
    char szEnd[4] = {0};
    *pIn >> wLen;
    pIn->Read(szEnd, wLen);
    CHECK(wLen == 3 && strcmp(szEnd, "end") == 0);
    CHECK(pIn->GetUnreadSize() == 0);

    // appending whole payloads (part byte stripped)
    CMassiveMsg* pCopy = CMassiveMsg::Alloc(MASSIVE_MODE_WRITE);
    CHECK(pCopy->AppendFrom(pIn, FALSE));
    CHECK(pCopy->GetUnreadSize() == 4 + 10000 + 5 + 3);
    pCopy->Free();

    pIn->Free();
    for (size_t i = 0; i < net.m_Sent.size(); ++i)
        net.ReleaseMsg(net.m_Sent[i]);
    CHECK(net.m_Pool.m_nInUse == 0);

    return TEST_RESULT("MassiveMsgTest");
}
