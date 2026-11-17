# FWAPP-001: message handler table one entry short, shared with the timer handle

- Original: `CServerProcessBase::InitDispatcher` (0x444250), `RegisterHandler` (0x444470), GatewayServer
- Rewrite: `CServerProcessBase::Initialize` / `m_aHandler` in `JMX_ServerFramework/ServerFramework/ServerProcess.cpp`
- Severity: minor

## What the original does

The handler table of a server process starts at +0x4C and the timer queue handle follows at
+0x40048, so the table has room for 0xFFFF opcodes. `InitDispatcher` fills 0x10000 entries with
the default handler (the last one lands on the handle), and the timer is created afterwards with
`CreateTimerQueueTimer(&m_hTimer, ...)`, which overwrites that entry with the handle.

## Effect

Opcode 0xFFFF is dispatched through the timer handle (crash) in processes with the timer, or to
the default handler in the others. A handler registered for 0xFFFF replaces the handle, and the
later `DeleteTimerQueueTimer` gets a function pointer.

## Fix

Declare 0x10000 entries before the handle, or fill 0xFFFF.
