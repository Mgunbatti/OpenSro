# GlobalManager

GlobalManager is the central server of a division. Every AgentServer logs its clients in here,
the shard managers ask it about accounts and characters, the billing server (if there is one) is
reached through it, and the service manager console (SMC) controls the division through it.

- **Content users.** An AgentServer registers a user with 0x6201 (address, JID, account, content,
  shard, AgentServer, MAC, play time of the day); the user is kept by JID and by client address
  (`CContentUserManager`; per-address limit Server.cfg `MaxUserForNonePCBangIP`, unlimited for the
  franchise addresses of `SK_gamebang_ip` and two official networks) and passed to the billing
  manager, which answers 0xA201 itself or lets the main process answer. 0x6200 checks a login
  beforehand, 0x6202 (massive) logs users out (their play time goes to `TB_USER`), 0x620C
  registers a whole shard's users after a restart, 0x2320 restarts a user's play time count.
  An AgentServer leaving state 5 loses all its users.
- **Shard requests.** Character name checks (0x620B) and character create / delete / rename
  (0x620E..0x6210) run as overlap jobs on the account database; 0x6318 answers the active
  punishments of an account; 0x6111 / 0x6112 count failed logins, image codes and OTPs per
  account and punish automatically at the configured tolerance; 0x2327 forwards a message to every
  shard manager.
- **Periodic work.** Once a second the shard user counts go to the GatewayServers; every 5 s the
  web silk changes are read and sent to the shards, every 10 s the shard states and every 5 min
  the user counts are written to the database; every 60 s punishments start and end; at
  midnight (and when a FarmManager connects after a day without one) the daily play times are
  reset and 0x231F goes to every shard manager.
- **Billing.** `./billing.ini` `[billing] type` 1 uses `CStandardBillingMgr` (the external billing
  server through `AlphegaNet.dll` / `AlphegaTimer.dll`), anything else `CDummyBillingMgr`.
- **Service manager.** `CServiceManagerProcess`, a member of the main process, serves the SMC
  sessions: module versions and patches, starting and stopping servers, service mode, shard
  settings, content user queries, security descriptions, SMC accounts, articles, punishments.
- **Slave.** `CGlobalManagerSlave` (its own task) polls the silk purchase history and loads the
  IP country tables (RIR files) for the IP statistics.

Security mode 3. Monitor graphs: ContentUser, Registered / Unregistered UserCount, Daily / Total
Visited UserCount, Occupied IP Count.

## Start-up

`CGlobalManager::OnLocalDataInitialized` (after certification), in this order: database
environment and default session (division connection string), `MaxUserForNonePCBangIP`,
`AccountDBConfig` (else the default DB), `SilkDBConfig` (else the account DB), the security
descriptions, the franchise addresses, the punishments (and their 60 s timer), the slave's main
process, the user validation history (Server.cfg `AutomatedPunisher`,
`Login/IBUV/MOTPFailureTolerance`, `...FailureBlockTimeMin`, `...PunishmentGuide`,
`...PunishmentDescription`; defaults 3 and 10 minutes), server state 4, the parent removed from
the notify set, the monitor graphs, 0x631C {division} to the parent and the time of the last play
time reset. A failing step ends the start-up with false.

## January 2011 framework

`GlobalManager.exe` was built on 2011-01-24, on the 2011 JMX_Library / JMX_ServerFramework of
AgentServer with a few older blocks in the BSLib database layer. The differences are in the
shared sources under `#ifdef JMX_FRAMEWORK_2011` and `JMX_FRAMEWORK_2011_01` (see
`JMX_ServerFramework\FRAMEWORK_2011.md`, "January 2011 (GlobalManager)"); the libraries are built a
third time with `GLOBAL_DEFS` into `build\lib\global` (`GLOBAL_LIBS`), and everything of this
folder and its tests is compiled with the same defines. `nmake global` builds
`build\bin\GlobalManager.exe` (GUI subsystem; ADVAPI32, GDI32, KERNEL32, ODBC32, USER32, WS2_32,
iphlpapi and ole32 like the original).

## Secrets

In division 2 the original inserts a password compiled into the binary after `PWD=` of
`AccountDBConfig` (as GatewayServer does). The rewrite does not contain it: it is supplied at build
time as `GLOBAL_ACCOUNT_DB_PASSWORD` (empty by default; `PWD=;` stays as configured). The SQL texts
of the silk history poll are stored encoded in the binary (XOR 0x90) and are kept that way.
There are no other credentials or keys in these sources; the billing server's address comes from
`./billing.ini` at run time.

## Files

| File | Content |
|---|---|
| `GlobalManager.h/.cpp` | `CGlobalManager` (0x599F30), start-up steps, monitor graphs |
| `GlobalManagerMainProcess.h/.cpp` | `CGlobalManagerMainProcess`: handlers, timers, billing creation |
| `WinMain.cpp` | `WinMain` |
| `ContentUser.h/.cpp` | content users, address maps, franchise addresses, shard user counts |
| `GlobalManagerJobs.h/.cpp` | overlap jobs of the main process |
| `GlobalManagerDB.h/.cpp` | the three database sessions |
| `GlobalManagerMainFwd.h` | functions of the main process and punishment manager used by the jobs |
| `PunishmentManager.h/.cpp` | active punishments (`CPunishmentMgr`) |
| `UserValidationHistory.h/.cpp` | failure counts and automated punishments |
| `ServiceManagerProcess.h/.cpp`, `ServiceManager*.cpp` | the SMC part |
| `ServiceManagerJobs.h/.cpp`, `ServiceManagerLog.cpp` | its overlap jobs and log |
| `GlobalManagerSvcFwd.h` | the job functions the service manager calls |
| `GlobalManagerSlave.h/.cpp` | the slave task |
| `IPCountryTable.h/.cpp` | IP to country table from the RIR files |
| `CASChat.h/.cpp` | CAS chat manager (only constructed; the rest of it is dead code) |
| `Billing\` | `IBillingMgr`, `CDummyBillingMgr`, `CBaseBillingMgr`, `CStandardBillingMgr`, IOCP and worker threads |

## Notable bugs of the original

GM-001 .. GM-027 (`docs\original-bugs`), among them: the shard status job divides by a shard
user limit of 0 (GM-010), security data leaks on failed queries (GM-012, GM-026), the punishment timer steps on an erased iterator
(GM-015), the billing heartbeat timeout neither closes nor reconnects (GM-017), the RIR files never
load because the summary type does not fit (GM-020), a failed silk history poll repeats on every
loop pass (GM-025), and the 0x620C billing message leaks for unknown shards (GM-027). The default
description of the OTP check is spelled "MOTPPunishmenDescription" in the original and kept.
