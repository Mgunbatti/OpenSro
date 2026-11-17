# The 2011 framework (AgentServer)

`AgentServer.exe` was built on 2011-07-01 (PE header time stamp), two and a half years after the
2008 GatewayServer the shared library sources were written from. It links a newer version of
JMX_Library (BSLib, BSNet) and JMX_ServerFramework. The rewrite keeps one copy of the sources:
the 2011 behaviour is written under `#ifdef JMX_FRAMEWORK_2011` next to the 2008 code, each block
with a comment naming the AgentServer address, like the 2007 blocks of FarmManager
(`JMX_CommonServers\FarmManager\README.md`). Without the define the sources compile exactly as
before, so the other servers do not see these blocks.

Code the 2008 servers never linked (the registry class, the UTF-8 helpers, a few lookups) is
added without a define.

## How the agent build is put together

Several differences change class layouts (the chunk allocators get a name, the engine parameter
block and the engine grow, `CServerApp`, `CServerFrameWindow` and `IBSNet` get new virtual
slots). So that no object of an AgentServer link mixes the 2008 and 2011 versions of a class, the
three libraries are built a second time from the same sources:

- `AGENT_DEFS` = `/DJMX_FRAMEWORK_2011 /DSERVER_MODULE_NAME="AgentServer"
  /DSERVER_CONFIG_MODULE_NAME="AgentServer"` (the exe uses the single string "AgentServer" for
  the service name, the Server.cfg block and the StartupError file);
- objects in `build\obj\agent\{BSLib,BSNet,ServerFramework}`, libraries in
  `build\lib\agent\{BSLib,BSNet,ServerFramework}.lib` (`AGENT_LIBS`);
- everything linked with `AGENT_LIBS` (the AgentServer sources, `test\AgentTest.cpp`,
  `test\Framework2011Test.cpp`) is compiled with `AGENT_DEFS` too.

`nmake` does not track header dependencies: after changing a header, delete `build\obj\agent`
and `build\lib\agent`.

## Layout changes

| Class | 2008 | 2011 |
|---|---|---|
| `CChunkAllocatorST<T>` | 0x40 bytes | 0x5C: `std::string m_strName` at +0x40 (constructor takes a trailing name; NULL or empty gives "Unknown") |
| `CChunkAllocatorMT<T>` | 0x7C, `m_cs` +0x40 | 0x98, `m_cs` +0x5C |
| `CMsgPool` | 0x88, in-use set +0x7C | 0xA4, set +0x98 |
| `CSessionTable` | 0xAC, slot vector +0x9C | 0xC8, vector +0xB8 |
| `BSNET_INIT_PARAM` | 0x94 | 0x114: `char szMsgPoolName[0x80]` at +0x94, zeroed by the constructor |
| `CNetEngine` | 0x3D4 | 0x4E0: locks +0x11C / +0x158, listener +0x194, socket pools +0x198 / +0x230 / +0x2C8, session table +0x360, datagram +0x428, message pool +0x430, keep-alive list +0x4D4 |
| `IBSNet` / `CNetEngine` vtable | 41 slots | 42: slot 41 `SetWinInetMaxConnections(DWORD)` after the destructor |
| `CServerApp` | 18 slots, 0x494 bytes | 20 slots, 0x620 bytes: new slot 16 `OnFrameCommand()`, slot 17 `OnSuspectedUserCommand(DWORD, DWORD)`; `OnControlMsg` / `SetServerState` move to 18 / 19; `m_nPendingState` +0x13C, `m_NetEngine` +0x140 |
| `CServerFrameWindow` | 20 slots | 21: new slot 17 `OnFrameCommand(WPARAM, LPARAM)` (command 0x9C48); the view commands and `OnMessage9000` move to 18..20; size 0xC8 unchanged |
| new `CInternalException` / `CMsgException` | - | 8 / 0xC bytes, `GetName()` in slot 1 |
| new `CRegistry` | - | 8 bytes (vftable, HKEY) |

Unchanged: `CServerProcessMain` (49 slots, 0x400C8), `CServerProcessBase`, `CSockStream`,
`CSockListener`, `CSockDatagram`, the queues, `CSession`, `CMsg`, `SERVER_GLOBALS`,
`CServerArchitectureView` (0x184), `CServerMoniterView`.

## Differences from the 2008 framework

| Original (AgentServer) | Function (rewrite file) | 2011 behaviour | 2008 behaviour |
|---|---|---|---|
| 0x46da30, 0x46dd20, 0x46e980, ... | `CChunkAllocatorST/MT` constructors, `SetName` (BSLib\ChunkAllocator.h) | Pool name member; pools named "NetEngine::ActiveSocketPool", "NetEngine::PassiveSocketPool", "NetEngine::SessionPool", "NetEngine::MsgPool", "CMassiveMsg", "COverlapJob_RelayMsg", "COverlapJob_InitializeLocalData"; the datagram pool is "Unknown" | No names |
| 0x468170, 0x483650, 0x439880, ... | `CChunkAllocatorST::AllocItemBlock` (ChunkAllocator.h) | Failure logged with 0x2000000 | 0x2000001 |
| 0x4693f0..0x469630 | `CMsg::Read<T>` / `PopBack<T>` (BSNet\Msg.h) | A short read throws `CMsgException` {1, 1}; the process message loop's `__except` disconnects the session as before, but destructors run while unwinding | Deliberate fault (BSNET-002) |
| 0x430f40 | `BSNET_INIT_PARAM` (BSNet\IBSNet.h) | `szMsgPoolName[0x80]` | - |
| 0x46b510 | `CNetEngine::CNetEngine` (BSNet\NetEngine.cpp) | 2011 layout, named pools | - |
| 0x46b920 | `CNetEngine::Initialize` | In server mode the message pool is renamed from `szMsgPoolName` ("Unknown" when empty) | - |
| 0x46c110 | `CNetEngine::AllocMsg` | NULL when the pool gives no message | Result dereferenced |
| 0x46c6c0 | `CNetEngine::DumpMsgPool` | Second argument passed to the pool dump as threshold | Ignored |
| 0x46d380 | `CNetEngine::SetWinInetMaxConnections` (slot 41) | Sets HKCU `...\Internet Settings` "MaxConnectionsPerServer" and "MaxConnectionsPer1_OServer" (sic) to 100 unless already 100; not called by AgentServer | - |
| 0x46e470 | `CMsgPool::DumpEntries` (BSNet\MsgPool.cpp) | With a threshold above 10000, the texts of 0x200A messages of an opcode counted above half the threshold go to `<MM-DD-hh-mm>_LogContent.txt` (at most 1000 lines of 500 characters) | Counts only |
| 0x471650 | `CMsgPool::AllocItemBlock` | Own failure text "CChunkAllocatorMT_For_CMsg::AllocItemBlock() Failed"; `catch(...)` writes the in-use messages per opcode to MsgAllocatorException.log | Generic, exception propagates |
| 0x46fcc0, 0x46e2f0 | `CMsgPool::AllocFromFreeQueue` / `Free` | "someone try to allocate msg which is already in use", "someone try to free msg already freed" | "someone about to ..." |
| 0x46b080 | `CSessionTable` (BSNet\SessionTable.cpp) | Named pool, vector +0xB8 | - |
| 0x46cff0 | `NetEngineLog` (BSNet\NetEngineLog.cpp) | Returns without an engine (NET-005 fixed) | Dereferences `g_pNetEngine` |
| 0x464ef0 | `CSockStream::SendQueuedWithLimit` (BSNet\SockStream.cpp) | NERR_PEER_DOESNT_RESPONSE with 0x2000001 | 0x1000000 |
| 0x462240 | `BSDebugWriteMiniDump` (BSLib\BSDebug.cpp) | Dumps numbered with a counter, handle checked (BSLIB-004 fixed), a failed write logged to very_fatal_log.txt | `rand() % 10`, unchecked |
| 0x4624e0 | `WalkStack` | Concatenation limit 4096 | 520 (BSLIB-003) |
| 0x462960, 0x462d10 | `BSAssertDialogW`, `BSAssertDialog_ShowMessage` | Wide assert dialog sharing the message part with `BSAssertDialog` | - |
| 0x4619f0 | `File_AppendString` (BSDebug.cpp) | New: appends a string to a file | - |
| 0x48a100 | `CThreadManager::CreateThreads` (BSLib\thread\ThreadManager.cpp) | CLAMP log path `D:\WORK2005\Source\JMX_Library\...`, lines 94/95 | Without `Source\`, 92/93 |
| 0x430880 | `CServerApp::InitNetEngine` (ServerApp.cpp) | Copies the service name to `szMsgPoolName` | - |
| 0x430a00, 0x430b20 | `RegisterServerTasks`, `RegisterMainProcessTask` | Message box for every failed step ("It has been failed to register task", ...) | Silent false |
| 0x430d20 | `CServerApp::OnControlMsg` 0x200C (slot 18) | Ends with "%s is initialized successfully" (0x2000001) | No line |
| 0x401010, 0x401020 | `CServerApp::OnFrameCommand`, `OnSuspectedUserCommand` | New empty slots 16 / 17 | - |
| 0x433e40, 0x44f1e0 | `ServerApp_Startup`, `SingleInstance_CreateSemaphore(name)` | Service name copied with 64; semaphore "Global\AgentServer" | 256; semaphore named like the module |
| 0x44d960 | `ServerApp_RunWindowMode` | Completion port created before `RegisterServerTasks`; message boxes "It has been failed to Initialize Server Task" / "... Request Certification" | Port after the tasks, no boxes |
| 0x430570 | `FatalLog_AppendLine` (LogQueue.cpp) | `date\ttime\t[module]\ttext` | Without `[module]` |
| 0x42b800 | `LogFile_AppendTimestampedLine` (LogQueue.cpp) | New: `date\ttime\ttext` appended to a file | - |
| 0x42b520, 0x42b790, 0x4315d0 | `Msg_FormatDebugString` (ServerProcess.cpp), `CheckMsgFullyConsumed`, `OnUnhandledMsg` | "IP:%s %s:0x%04X (0x%04X) (MsgType:%d), %d - %d" with the peer address | `CMsg::FormatDebugString` "Req:0x%x (%d), %d - %d" |
| 0x431720 | `CServerProcessBase::RegisterTimer` | New public function (also used by AgentServer) | Inlined |
| 0x42c690 | `CServerProcessMain::OnSessionLost` | "Session Lost" lines with 0x2000001 | 0x1000000 / 0 |
| 0x42d630 | `CServerProcessMain::OnMsg_2311` | Shard name copied with 32 | 256 |
| 0x4122e0, 0x4126c0 | `ServerCord_OnEstablished` / `OnClosed` (ServerBody.cpp) | "server cord established" / "closed" with 0x2000001 | 0 |
| 0x4100d0 | `ServerCordMap_FindBySession` (ServerBody.cpp) | New: cord of a session | Inlined |
| 0x434570 | `ServerConfig_Load` (ServerConfig.cpp) | `certification` IP and bind IP copied with 16; a longer token fails the load (FWUTIL-001 fixed) | 256 |
| 0x40d840 | `ServerConfig_FindExtraValue` (ServerConfig.cpp) | Defined by the framework (the 2011 monitor uses it) | Defined by each module |
| 0x4360e0, 0x436130, 0x4367e0 | `MonitorGraph_ReadMsgDumpConfig`, `StartUpdateThread`, `ReadNetEngineCounter` (MonitorGraph.cpp) | Server.cfg "MsgCountOfDumpTrigger" / "MsgDumpTriggerElapseTick" (10000): when the messages in use reach the trigger, "Msg Usage" header and `DumpMsgPool(1, trigger)` under `__try` ("It has been failed to write Msg Dump!") | No dump |
| 0x44f8c0, 0x44f2a0 | `CrashDump_TopLevelExceptionFilter`, `CrashDump_WriteDump` (CrashDump.cpp) | Filter passes {thread, pointers} to a worker, on a new thread with a 0x19000 stack after a stack overflow (FW11-020); failures logged to ExceptionHandlerInfo.log instead of asserting | Dump written in the filter |
| 0x457170, 0x457a80 | `CServerFrameWindow::CreateFrame`, `OnFrameCommand` (ServerFrameWindow.cpp) | Command 0x9C48 calls the app's slot 16 | - |
| 0x4579f0 | `CServerFrameWindow::OnDump` | Header lines with 0x2000001 | 0 |
| 0x457aa0 | `CServerFrameWindow::OnNotify` | Log list menu adds "Register Suspected User", "Unregister Suspected User", "Show Registered Suspected Users" (0x104..0x106, app slot 17 with 0, 1, 2) | 4 items |
| 0x450b60, 0x450a50 | `Win32_GetLocaleFontName`, `Win32Dialog_SetLocaleFont` (WindowBase.cpp) | 0x419 "Tahoma", 0x41E "Microsoft Sans Serif"; a font that cannot be created is reported in a message box | "Gulim"; nothing |
| 0x45dfb0 | `CMoniterGraphData::Init` (ServerMoniterView.cpp) | Names of 64 characters or more assert; copy limit 64 (FWUI-003 fixed) | 256 |
| 0x45eb00 | `CServerMoniterView::RenderGridBackground` | No `GetDeviceCaps`; CLAMP path with `Source\`, line 439 | 434 |

Code added without a define: `CRegistry` (BSLib\Util\Registry.h, 0x489760..0x4899d0), the UTF-8
and wide string helpers (BSLib\Util\StringUtil.h, 0x475a10..0x4767b0; FW11-001),
`SelfDivision_IsInGroup1/2` (0x40e170 / 0x40e1a0, unreferenced also in GatewayServer),
`ServerCordMap_FindBySession`, `CServerProcessBase::RegisterTimer`.

These differ only in generated code and are not reproduced: the 2011 compiler's /GS code and
inlining, out-of-line CMsg readers, the inline `CSingleLockBS` (no visible difference unless the
sync object is NULL), and the assert file names (`D:\WORK2005\Source\Common\Framework\...`).

Bugs of the 2011 code reproduced on purpose: FW11-001, FW11-002, FW11-020 (`docs/original-bugs`).

`test\Framework2011Test.cpp` checks every row above that a test can reach (real engine, log files
and packets byte by byte, IAT hooks for message boxes, menus and fonts, a temporary HKCU key for
the registry code); the other tests keep checking the 2008 behaviour.
