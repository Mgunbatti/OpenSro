# The 2011 framework (AgentServer, GlobalManager)

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

## January 2011 (GlobalManager)

`GlobalManager.exe` was built on 2011-01-24, five months before AgentServer. Every BSLib, BSNet
and ServerFramework function it shares with AgentServer behaves like the July 2011 code above:
the comparison of its 229 framework functions that are not byte-identical to an already ported
module found only register allocation, stack layout, constant propagation (`SetTimerPeriod` with
the timer ID 35, `CTimeSpan(0, h, m, s)`, `DestroySession(g_pSession)`) and out-of-line copies
of inlined helpers. The pools are named, the engine has the 2011 layout and slot 41, messages
throw `CMsgException`, the frame window has command 0x9C48 and the suspected-user menu, the
crash dump goes through the worker thread, and so on.

The differences are in code AgentServer does not link: the BSLib ODBC layer (which the rewrite
has from the 2008 GatewayServer) and a newer copy of `TimeSpan.cpp`. They are written under
`#ifdef JMX_FRAMEWORK_2011_01`, which is only ever defined together with `JMX_FRAMEWORK_2011`
(BSDebug.h stops the build otherwise), so the July 2011, 2008 and 2007 builds are unchanged.

- `GLOBAL_DEFS` = `/DJMX_FRAMEWORK_2011 /DJMX_FRAMEWORK_2011_01
  /DSERVER_MODULE_NAME="GlobalManager" /DSERVER_CONFIG_MODULE_NAME="GlobalManager"` (the exe has
  the single string "GlobalManager": service, semaphore "Global\GlobalManager", Server.cfg
  block, StartupError file);
- objects in `build\obj\global\{BSLib,BSNet,ServerFramework}`, libraries in
  `build\lib\global` (`GLOBAL_LIBS`); everything linked with them is compiled with
  `GLOBAL_DEFS`. After a header change delete `build\obj\global` and `build\lib\global`.

No class layout differs from the July 2011 framework. `DB_PARAM` gets a `char* pszValue` union
member (same size and offsets in every build).

| Original (GlobalManager) | Function (rewrite file) | January 2011 behaviour | 2008 behaviour |
|---|---|---|---|
| 0x4c8590 | `CDBSessionMgr::Open` / `DB_SESSION_COUNT` (BSLib\DB\Database.h/.cpp) | 4 connections per pool | 30 |
| 0x4c8720, 0x4c8980, 0x4c8a40, 0x4c8810, 0x4c8920 | `CDatabase::InitEnvironment`, `CreateSession`, `DestroySession`, `DestroyAllSessions` | The pool registry is `g_pSetDBSessionMgr`, created on the heap by `InitEnvironment` (before the environment, also when that fails) and deleted by `DestroyAllSessions`; without it `DestroyAllSessions` returns at once (the environment stays) and `DestroySession` dereferences NULL (FW11-021) | Static `g_setDBSessionMgr` |
| 0x4c9a10 | `CDatabase::ExecDirectWithParams` | Parameter type 6 (`DBPARAM_VARCHAR`): `pszValue` bound as SQL_C_CHAR / SQL_VARCHAR, `cbValue` as column size, buffer length and indicator | Types 0..5; type 6 ends the call (DB-006) |
| 0x4cce90 | `CDatabase::HandleSQLError` | First line "Query Error!!! Query Result : %d, Query : %s" | The query alone |
| 0x4869e0 | `CDBQuery::Execute` (BSLib\DB\DBQuery.cpp) | Statement handle initialised to NULL and freed only when allocated (DB-002 fixed); every `HandleSQLError` call gets `m_szQuery` | Uninitialised handle; `NULL` ("[Unknown query]") |
| 0x4caa70 | `CDBTable::DescribeColumns` | Assert file `D:\WORK2005\Source\JMX_Library\BSLib\DB\Database.cpp`, lines 1571 / 1587 | Without `Source\`, lines 1524 / 1540 |
| 0x4b5310 | `CDateTime::Format` (BSLib\Util\TimeSpan.cpp) | New, no define: the local time as "%d-%02d-%02d %02d:%02d:%02d" (unreferenced in GlobalManager) | - |

Not reproduced: the second, unreferenced copy of the date constructors in GlobalManager
(0x4b5210..0x4b5520, `D:\WORK2005\Source\JMX_Library\BSLib\Util\TimeSpan.cpp`) reports range
errors to the empty trace function instead of `BSAssertFail` and does not check the `_mktime32`
result; only its `CTimeSpan(LONG, int, int, int)` (the same code) is called. The copy
GlobalManager's code can reach (0x4aedd0..) is the 2008 code.

Bugs of the January 2011 code reproduced on purpose: FW11-021.

`test\Framework201101Test.cpp` (compiled with `GLOBAL_DEFS`, linked with `GLOBAL_LIBS`) checks
the pool size, the registry life cycle, the log lines, the query object, `CDateTime::Format`
and, with the 32-bit Access ODBC driver, a string parameter and the query text in the
diagnostics. The assert path of `DescribeColumns` is not reached by a test (it opens a dialog).

## August 2011 (SR_ShardManager)

`SR_ShardManager.exe` was built on 2011-08-08, five weeks after AgentServer. Its BSNet, BSLib
and ServerFramework code is the July 2011 framework, and its DB layer is the January 2011 one of
GlobalManager (heap pool registry, `DBPARAM_VARCHAR`, "Query Error!!!" line, the `Source\` assert
path, the query object fixes). The 475 framework functions that are not byte-identical to an
already ported module were compared with their AgentServer and GlobalManager copies; nearly all
differ only in generated code: helpers that the other builds inline are called (the ID pool's
`Push` from `COverlapJob::ReleaseId`, `CQue::Push` from the pool `Free` functions, the
`hash_map` / `list` clears of `CFileSystem::Cleanup`, `ServerApp_FreeCertArchData` and the
destructors, `CFileTransMgr::Reset` from `CServerApp::Shutdown`, `ModuleVersion_FreeAllData`, the
`CMsg` readers of `_OnMsgReceivedBeforeHandshake`), constants are propagated
(`CWindowBase::Show(SW_SHOW)`), arguments move between registers and the stack
(`CSession::OpenTransferFile`, `CDBRecord::BuildUpdate`) and stack frames grow
(`CDBTable::BuildSqlTemplates`, `DescribeColumns`). `CFileSystem` has the two hash maps of the
rewrite; `SetDefaultEngineConfig` sets the same fields.

The differences are in the DB layer and in code no earlier module linked. They are written under
`#ifdef JMX_FRAMEWORK_2011_08`, which is only ever defined together with `JMX_FRAMEWORK_2011` and
`JMX_FRAMEWORK_2011_01` (BSDebug.h stops the build otherwise), so the other builds are unchanged.

- `SHARD_DEFS` = `/DJMX_FRAMEWORK_2011 /DJMX_FRAMEWORK_2011_01 /DJMX_FRAMEWORK_2011_08
  /DSERVER_MODULE_NAME="SR_ShardManager" /DSERVER_CONFIG_MODULE_NAME="SR_ShardManager"` (the exe
  has the single string "SR_ShardManager": service, semaphore, Server.cfg block, StartupError
  file);
- objects in `build\obj\shard\{BSLib,BSNet,ServerFramework}`, libraries in `build\lib\shard`
  (`SHARD_LIBS`); everything linked with them is compiled with `SHARD_DEFS` too. After a header
  change delete `build\obj\shard` and `build\lib\shard`.

Layout: `CDBTableInfo` gets `int m_nDataOffset` at +0x104 (the offset of the row data in a
record, used by the default `CDBRecord::GetData`); `CDBRecord` and `CDBRecStream` implement
vtable slots 5 and 6 instead of leaving them pure. No other class layout differs from the July
2011 framework.

| Original (SR_ShardManager) | Function (rewrite file) | August 2011 behaviour | January 2011 behaviour |
|---|---|---|---|
| 0x6b1740, 0x6b1ad0 | `CDBSessionMgr::Open(pszConnect, nCount)`, `CDatabase::CreateSession(nCount, pszConnect)` (BSLib\DB\Database.h/.cpp) | The number of connections is a parameter; 0 or less fails at once without touching the pool | 4 connections |
| 0x6b28d0 | `CDatabase::ExecDirectWithReturnValue` (Database.cpp) | New, no define: `DBEXEC_RETURN_INT` (2) / `DBEXEC_RETURN_BIGINT` (4) bind `pValue` as output parameter 1 (int / `__int64`), the remaining results are skipped (DB-007); -1 empty text, -3 no statement | - |
| 0x6b2f60 | `CDBRecord::Update(pTable, nGroup)` | Always connection 0, through `ExecDirectWithReturnValue` without an output parameter | `Update(pTable, nConn, nGroup)`, `ExecDirect` |
| 0x43f920, 0x6b52aa | `CDBRecord::ClearDirty`, `CDBRecord::BuildUpdate` | A group from 64 on asserts, a negative one clears the bit of its low byte; `BuildUpdate` asserts instead of clearing a negative group | Out of range: ignored |
| 0x404080, 0x402c70 | `CDBRecord::InitData` / `GetData` (slots 5 / 6) | Defaults: clear the dirty bits / the record plus `CDBTableInfo::m_nDataOffset` | Pure |
| 0x61a580, 0x61a560 | `CDBRecStream::InitData` / `GetData` | Clear +1C, +20, +28 / the buffer at +18 (asserts when NULL) | Pure |
| 0x5494a0.., 0x549520.., 0x55ca00 | `CCustomAllocator<T, TPool>` (BSLib\DB\CustomAllocator.h) | New, no define: the record allocator of a table on a `CChunkAllocatorMT<T>` / `CChunkAllocatorST<T>` of 1000 records per block named after the table ("Unknown DB Table"); slots 1 `AllocRecord`, 2 `FreeRecord`, 3 the pool counts | - |
| 0x41b5a0 | `CMsg::CopyPayloadFrom` (BSNet\Msg.cpp) | New, no define: appends the unread part (mode 1) or the whole payload of another message and marks that one as read | - |
| 0x688100 | `SelfDivision_IsInGroup3` (ServerFramework.cpp) | New, no define: division 3, 5, 9, 10, 26, 27 or 34 (unreferenced in AgentServer at 0x40e130) | - |

No new bug of the August code was found; `ExecDirectWithReturnValue` has the endless
`SQLMoreResults` loop of DB-007.

`test\Framework201108Test.cpp` (compiled with `SHARD_DEFS`, linked with `SHARD_LIBS`) checks
every row: the pool sizes, the record slots and dirty bits, `Update` and
`ExecDirectWithReturnValue` (with the 32-bit Access ODBC driver against a temporary .mdb), the
allocator template through the `_record_allocator` slots and `CDBTable`, `CopyPayloadFrom` and
the division group.
