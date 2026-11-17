# AgentServer

AgentServer is the server a game client plays through. The GatewayServer certifies the account and
hands the login over; the client then connects to an AgentServer of its shard, and every packet of
the client passes through it on the way to and from the shard's game server.

- **Login tokens.** The gateway relays 0x6203 {client address, account, password, JID, security
  groups, extras}; the AgentServer stores it as a `CToken` and answers 0xA203 with the token ID,
  which the gateway gives the client. A token is used up by the first login attempt and dropped
  after 10 s (timer 2 of the main process, every 3 s).
- **Client login (0x6103).** Token ID, account, password, content ID and a 6-byte MAC. The content
  must be the shard's, the token must match address, account and password (case-sensitive).
  The user (`sUser`) is registered and the login goes to the GlobalManager (0x6201 relayed;
  answer 0xA201), then to the game server (0x6204; answer 0xA204), which ends with 0xA103 {1} to
  the client. Every failure answers 0xA103 {2, error} and closes the session.
- **Relays.** A user's packets get the JID appended and go to the user's target body (the game
  server; 0x6207 / 0x6208 / 0xA208 move it to another body). The game server reaches the clients
  with 0x2209 (one session), 0x220A (a session list at the end of the packet) and 0x220F + 0x220E
  (collect targets, then send).
- **GlobalManager requests.** 0x2206 kicks sessions, 0x620D and 0x6905 ban a JID, 0x6902 passes a
  billing notice to the client. Lost sessions are logged out (0x6202 to the GlobalManager, 0x6205
  to the game server); a lost cord to a game server drops its users and sets server state 4.
- **Client filter.** While in service a session without a server cord may send only the login
  opcodes (0x2001, 0x2002, 0x2110, 0x2113, 0x6103, 0x6110, 0x6314, 0x6316) and plain client
  opcodes (bit 0x1000 set, 0x8800 clear); anything else closes it. A traffic filter (Server.cfg
  `traffic_filter_activity service_on`, `traffic_filter_packet_count`,
  `traffic_filter_check_perid`, default 100 packets / 60000 ms) closes users that send too much.

Security mode 2. Monitor graphs: UserCount, TokenCount, MsgSentCount, MsgSentBytes.

## 2011 framework

`AgentServer.exe` was built on 2011-07-01, on a newer JMX_Library / JMX_ServerFramework than the
2008 servers. Those differences are in the shared sources under `#ifdef JMX_FRAMEWORK_2011`
(see `JMX_ServerFramework\FRAMEWORK_2011.md`); `nmake agent` builds the libraries a second time
with `AGENT_DEFS` into `build\lib\agent` (`AGENT_LIBS`), and the AgentServer sources and its tests
are compiled with the same defines.

## XTrap

The server uses the XTrap anti-cheat server module `XTrap4Server.Dll`, loaded at run time from the
working directory (`LoadLibraryA` + `GetProcAddress` of 13 exports, never freed). At start
(`OnLocalDataInitialized`) `map1.cs3` and `map2.cs3` (0x32C8 bytes each) are read into one block,
`XTrap_S_Start(600, 2, maps, NULL)` and `XTrap_S_SetActiveCode(0x0FFFFFC3)` are called; any
failure logs "###   XTrap Initialized Deactivated   ###" and the server runs on (calls into a
missing DLL are not guarded, so a deactivated XTrap crashes on the first login). Every login runs
`XTrap_S_SessionInit` on the user's 320-byte session buffer. Once a second the main process sends
every user whose last challenge is more than three minutes old a new one (0x2113 {1, 1,
`XTrap_CS_Step1` data}); the client's answer 0x2113 {2, 2, data} goes to `XTrap_CS_Step3`.

## The local patch (`AGENT_XTRAP_DETECTION_OFF`)

The `AgentServer.exe` this port was made from (SHA-256 03103ae0...9a72) is patched:

| Address | Original (Joymax) | Patched exe |
|---|---|---|
| 0x404590, at 0x4046e5 | `test ebx, ebx` / `jz` over the detection block: a non-zero `XTrap_CS_Step1` result logs "[For Xtrap] Hack Detect!! %s" (0x2000000 and 0x2000001) and collects the user; after the pass every collected user is acted on | `jmp` (eb 7e): the result is ignored, nothing is detected |
| 0x404590, at 0x40479b | a 5-byte call for each collected user | NOPs |
| 0x401df0, at 0x401ea2, 0x401ed2, 0x401f00, 0x401f19 | four `LogPrintf(0x2000001, "WARNING!! A SUSPECT DETECTED!!! ...")` calls for refused and faulting client messages | NOPs (the argument pushes are left) |

The default build is the original. `nmake agent AGENT_XTRAP_DETECTION_OFF=1` (after deleting
`build\obj\AgentServer`) builds the patched behaviour; `AgentTestNoDetect.exe` tests that variant
(`AgentTest.exe` the original).

### Reconstructed parts

These are not in the patched binary and were rebuilt from what is left around them; they are
marked `// RECONSTRUCTED:` in the code.

- **The branch at 0x4046e5** (`XTrap.cpp`, `XTrap_BroadcastStep1`). The patch left "eb 7e" after
  `test ebx, ebx` (ebx = the `XTrap_CS_Step1` result). The original is taken to be `jz` (74 7e):
  0 is XTrap's success value, so a non-zero result runs the "Hack Detect" block, which the patch
  made unreachable. `jnz` would mean "detected on success" and is not plausible.
- **The action for each detected user at 0x40479b** (`XTrap.cpp`). One 5-byte instruction after
  `mov esi, [node + 8]` (the user) and without argument pushes, i.e. a direct call with the user in
  a register. No helper of that kind is left in the binary (the call was its only use). The port
  closes the user's session (`DisconnectSession(session, 1)`), which is what the module does with
  every misbehaving client (traffic filter, 0x620D, 0x6905); the lost session then logs the user
  out as usual. The exact original target is not known.
- **The four log calls of 0x401df0** (`AgentMainProcess.cpp`, `AgentMainProcess_ReportSuspect`).
  The flags (0x2000001), the format strings and the arguments are still pushed and the stack
  cleanup after the NOPs (`add esp, 0x18 / 0x18 / 0x10 / 0x0c`) gives 6, 6, 4 and 3 cdecl
  arguments: `LogPrintf` (0x434400), the log function of every other line of the module.

## Files

| File | Content |
|---|---|
| `AgentServer.h/.cpp` | `CAgentServer` (0x52ABE0), monitor graphs |
| `AgentMainProcess.h/.cpp` | `CAgentMainProcess` and all packet handlers |
| `Token.h/.cpp` | `CToken`, `CTokenManager`, the token ID pool |
| `User.h/.cpp` | `sUser`, the user pool, `CUserManager` and the traffic filter |
| `XTrap.h/.cpp` | XTrap4Server.dll loading, start, challenges |
| `WinMain.cpp` | `WinMain` |

Bugs of the original: AS-001 .. AS-006 (`docs\original-bugs`).
