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

    virtual int __stdcall QueryInterface(DWORD, void**) { return 0; }
    virtual int __stdcall AddRef() { return 0; }
    virtual int __stdcall Shutdown() { return 0; }
    virtual BOOL __stdcall Initialize(BSNET_INIT_PARAM) { return 0; }
    virtual DWORD __stdcall ConnectSockAddr(DWORD*, const sockaddr*, int, DWORD) { return 0; }
    virtual int __stdcall Connect(const char*, u_short, const char*, DWORD, DWORD) { return 0; }
    virtual BOOL __stdcall ListenSockAddr(const sockaddr*) { return 0; }
    virtual int __stdcall Listen(const char*, u_short) { return 0; }
    virtual int __stdcall RegisterProactor(DWORD, void*, DWORD, DWORD) { return 0; }
    virtual int __stdcall vf09(DWORD, DWORD, DWORD, DWORD) { return 0; }
    virtual void* __stdcall StartProactorWorkers(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD*) { return 0; }
    virtual void* __stdcall ProactorResume(DWORD) { return 0; }
    virtual void* __stdcall ProactorSuspend(DWORD) { return 0; }
    virtual int __stdcall UnregisterProactor(DWORD) { return 0; }
    virtual int __stdcall GetStatistics(DWORD, DWORD) { return 0; }
    virtual int __stdcall GetSessionPeerAddress(DWORD, DWORD*, ULONG*, u_short*) { return 0; }
    virtual int __stdcall GetProactor(DWORD) { return 0; }
    virtual char __stdcall StartHandshake(DWORD, BYTE*) { return 0; }
    virtual int __stdcall BeginSessionFileTransfer(DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                                   DWORD, DWORD, DWORD, DWORD) { return 0; }
    virtual int __stdcall SendFile(DWORD, void*, DWORD, DWORD) { return 0; }
    virtual int __stdcall DisconnectSession(DWORD*, SOCKET) { return 0; }
    virtual int __stdcall CloseSessionTransferFile(DWORD, DWORD) { return 0; }
    virtual int __stdcall ExcludeSessionFromCleanup(DWORD) { return 0; }
    virtual int __stdcall GetSessionSocketState(DWORD) { return 0; }
    virtual int __stdcall RemoveKeepAliveTarget(DWORD) { return 0; }
    virtual void* __stdcall AddKeepAliveTarget(DWORD, DWORD, DWORD, DWORD, DWORD) { return 0; }
    virtual void __stdcall CloseAllFiles() {}
    virtual int __stdcall DumpMsgPool(BYTE, DWORD) { return 0; }
    virtual char __stdcall vf31(BYTE) { return 0; }
    virtual DWORD __stdcall CleanupDisconnectedSessions() { return 0; }
    virtual void* __stdcall GetMsgPool() { return &m_Pool; }
    virtual int __stdcall GetSessionLocalMacAddress(DWORD, DWORD*) { return 0; }
    virtual int __stdcall SetSecurityMode(BYTE) { return 0; }
    virtual int __stdcall PrintVersion() { return 0; }
    virtual BOOL __stdcall RegisterDatagramProactor(DWORD) { return 0; }
    virtual int __stdcall OpenDatagramSocket(int) { return 0; }
    virtual int __stdcall SendDatagram(DWORD, DWORD) { return 0; }
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
