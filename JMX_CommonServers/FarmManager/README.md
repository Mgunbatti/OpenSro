# FarmManager

FarmManager has almost no code of its own. It has no packet handlers of its own (everything goes
through the framework), and only three things of its own:

- it runs the UDP ping echo service ("UDP Ping Service was Started!");
- when the cord to the GlobalManager comes up, it drops its patch file data and sends 0x6306 (the
  same code as DownloadServer);
- it uses security mode 3 and sets server state 5 once local data is initialised.

## Older framework

The original `FarmManager.exe` was built on 2007-06-07 (PE header time stamp). The other ported
servers were built later, and the modules that are not ported yet later still:

| Module | Built |
|---|---|
| FarmManager.exe | 2007-06-07 |
| GatewayServer.exe, DownloadServer.exe, MachineManager.exe | 2008-11-06 |
| GlobalManager.exe | 2011-01-24 |
| AgentServer.exe | 2011-07-01 |
| SR_ShardManager.exe, SR_GameServer.exe | 2011-08-08 |
| _SMM.exe | 2011-12-23 |

FarmManager therefore links an older version of JMX_Library and JMX_ServerFramework. The rewrite keeps
one copy of the library sources. The 2007 behaviour is written under `#ifdef JMX_FRAMEWORK_2007` next
to the current code, each block with a comment naming the FarmManager address. Without the define
the sources compile exactly as before, so the other servers do not see these blocks.

### How the farm build is put together

Two of the differences change class layouts (`CServerProcessMain` loses a virtual slot,
`CServerArchitectureView` is 4 bytes shorter), and the others are spread over BSLib, BSNet and the
framework. So that no object of the FarmManager link mixes the 2007 and 2008 versions of a class,
`nmake farm` builds all three libraries a second time from the same sources:

- `FARM_DEFS` = `/DJMX_FRAMEWORK_2007 /DSERVER_MODULE_NAME="FarmManager"
  /DSERVER_CONFIG_MODULE_NAME="FarmManager"`;
- objects in `build\obj\farm\{BSLib,BSNet,ServerFramework}`, libraries in
  `build\lib\farm\{BSLib,BSNet,ServerFramework}.lib` (`FARM_LIBS`);
- the FarmManager sources (`build\obj\FarmManager`) and `test\FarmTest.cpp` are compiled with
  `FARM_DEFS` too, and `FarmManager.exe` / `FarmTest.exe` link `FARM_LIBS` instead of the shared
  libraries. The module name defines reach every farm framework file; only `CertArch.cpp`,
  `ServerApp.cpp` and `ServerConfig.cpp` use them.

`SERVER_MAIN_PROCESS_631D` (in `ServerProcessMain.h`) is derived from `JMX_FRAMEWORK_2007`
(0 with it, 1 without) and does not need to be set separately.

### Differences from the 2008 framework

| Original (FarmManager) | Function (rewrite file) | 2007 behaviour | 2008 behaviour |
|---|---|---|---|
| 0x402500 | `CServerProcessMain::InitMsgHandlers` / vtable (ServerProcessMain.h/.cpp) | No `OnMsg_631D` virtual and no 0x631D registration; the main process vtable has 48 slots instead of 49 | 0x631D handled, 49 slots |
| 0x459820 | UDP ping service (`UDPPING_SERVER_PORT`, UdpPing.h) | Port **15850** | Port 15851 |
| 0x4029c0 | `OnMsg_2001` (ServerProcessMain.cpp) | The 0x6005 query lists every other server body routed through the new cord, then the cords of each (FM-002) | Only the cord's peer body and its cords |
| 0x4134f0 | `ServerCord_OnEstablished` (ServerBody.cpp) | If the cord already has a session, "server cord already established : created session overlapped : %d, %s(%d)" is logged, the *new* session is disconnected and the cord is returned anyway (FM-001) | The old session is disconnected and replaced |
| 0x4138b0 | `ServerCord_OnClosed` (ServerBody.cpp) | Every body routed through the closed cord goes to state 0 with its cords; the entry is always removed and logged | Only the peer body, nothing without `g_pSelfBody` |
| 0x466220, 0x4672b0 | `CMsgPool::Free` / `AllocFromFreeQueue` (MsgPool.cpp) | The in-use set stays empty (the member exists, the layout is the same) | Messages tracked in the set |
| 0x464800 | `CNetEngine::DumpMsgPool` (NetEngine.cpp) | Empty | Dumps the in-use set |
| 0x409a10 | `CServerApp::Shutdown` (ServerApp.cpp) | Server state 7 | State 8 |
| 0x409b50 | `OnControlMsg` 0x200A (ServerApp.cpp) | Only flags 0x2000001 go to the fatal log, and the line is always shown | 0x3000000 also to the fatal log and not shown |
| 0x40a4d0 | `Net_IsPrivateIPv4` (ServerApp.cpp) | 192.168.0.0/16 does not count as private | Counted as private |
| 0x40a630 | `LogPrintf` (LogQueue.cpp) | 0x2800-byte buffer | 0x2000 bytes |
| 0x409220 | `LogLevel_ToString` (LogQueue.cpp) | Levels above 3 are "unknown" | "" |
| 0x43f360 | `Console_WriteLogLine` (LogQueue.cpp) | Level clamped to 3 (4 colours) | 5 colours |
| 0x40a7b0 | `ServerConfig_Load` (ServerConfig.cpp) | No "WritePerfLogToText" option (the key ends in the extra map) | Option read |
| 0x441790, 0x4419c0 | Architecture view (ServerArchitectureView.h/.cpp) | 8 state icons (state 7 shows icon 0x67), overlay at +0x100, object 0x180 bytes | 9 icons, 0x184 bytes |
| 0x446e20 | Log list menu (ServerFrameWindow.cpp) | No "Copy SelectedItem To Clipboard" item | Item present |
| 0x454870 | `CServerMoniterView::SampleAndLogGraphs` (ServerMoniterView.cpp) | No PERFMON log | PERFMON log |
| 0x454970, 0x47dbd0 | CLAMP log (ServerMoniterView.cpp, ThreadManager.cpp) | `%d` values, relative file names `.\ServerMoniterView.cpp` (line 421) and `.\thread\threadmanager.cpp` | `%.3f`, `D:\WORK2005\...` paths |

These differ only in generated code and are not reproduced: inlined `CMsg` reads that call
`BSAssertFail` before the deliberate fault (no visible effect with the default debug flags),
`_vsnprintf` instead of `_vsnprintf_s`, and the older `stdext::hash_map` code.

Bugs of the 2007 code reproduced on purpose: FM-001, FM-002 (`docs/original-bugs`).

`test\FarmTest.cpp` checks the 2007 behaviour (port 15850, the cord already established, the cord
closed for every routed body, the 0x6005 lists, the private address check, the log level names,
the 0x2800 log buffer, the config key, 0x200A, the message pool, shutdown state 7); the other
tests keep checking the 2008 behaviour.

## Moving FarmManager to the current framework

To bring FarmManager up to the 2008 framework later, drop `/DJMX_FRAMEWORK_2007` from `FARM_DEFS`
(or link the shared libraries with the per-server module name files like DownloadServer). The
table above lists what changes for the servers around it. The UDP ping port is the one other
components are most likely to notice: anything that pings FarmManager on 15850 would have to use
15851.
