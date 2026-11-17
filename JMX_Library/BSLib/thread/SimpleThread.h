#pragma once

#include <windows.h>

// ATL-style __stdcall thunk: a 14-byte code stub that replaces the first stack argument's
// return slot with a fixed object pointer and jumps to a static procedure. The procedure then
// sees (pThis, original argument) on the stack.
#pragma pack(push, 1)
template <class T>
class CAuxStdThunk
{
public:
    void InitThunk(DWORD_PTR proc, void* pThis)
    {
        m_mov = 0xB8;                   // mov eax, pThis
        m_this = PtrToUlong(pThis);
        m_xchgPush = 0x50240487;        // xchg eax, [esp] ; push eax
        m_jmp = 0xE9;                   // jmp rel32
        m_relproc = DWORD((INT_PTR)proc - ((INT_PTR)this + sizeof(CAuxStdThunk)));
        FlushInstructionCache(GetCurrentProcess(), this, sizeof(CAuxStdThunk));
    }

    void* GetCodeAddress()
    {
        return this;
    }

public:
    BYTE  m_mov;
    DWORD m_this;
    DWORD m_xchgPush;
    BYTE  m_jmp;
    DWORD m_relproc;
};
#pragma pack(pop)

// Thread object whose thread procedure is a per-object thunk calling the virtual Run.
// Threads are always created suspended; m_bSuspended mirrors that.
class CSimpleThread : public CAuxStdThunk<CSimpleThread>
{
public:
    CSimpleThread();
    virtual ~CSimpleThread();                       // 0

    // __stdcall member: the thunk passes this and the _beginthreadex argument on the stack.
    virtual void __stdcall Run(void* pArg);         // 1

    BOOL Create(void* pArg, unsigned nStackSize, int nPriority);
    BOOL Close();
    BOOL Suspend();
    BOOL Resume();

protected:
    static unsigned __stdcall ThunkProc(CSimpleThread* pThis, void* pArg);

public:
    HANDLE   m_hThread;         // +14
    BOOL     m_bSuspended;      // +18
    unsigned m_dwThreadID;      // +1C
};

C_ASSERT(sizeof(CAuxStdThunk<CSimpleThread>) == 14);
C_ASSERT(FIELD_OFFSET(CSimpleThread, m_mov) == 4);
C_ASSERT(FIELD_OFFSET(CSimpleThread, m_hThread) == 0x14);
C_ASSERT(FIELD_OFFSET(CSimpleThread, m_dwThreadID) == 0x1C);
C_ASSERT(sizeof(CSimpleThread) == 0x20);
