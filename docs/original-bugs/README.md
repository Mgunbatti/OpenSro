# Bugs found in the original binaries

The rewrite reproduces the original behaviour, bugs included, so it can replace the original
executables one to one. Every bug found while porting is written down here so it can be fixed
later in a separate pass. In the code each one is marked with `// BUG(<ID>)`.

One file per bug: where it is (module, address, rewrite file), what the original does, what
that causes, and a suggested fix. Severity is a rough guess: **crash** (can take the process
down), **wrong** (wrong result or lost data), **minor** (cosmetic, leak, dead code).

| ID | Area | Severity | Summary |
|---|---|---|---|
| [BSLIB-001](BSLIB-001.md) | BSLib debug | minor | Symbol search path: environment variables used only when `_dupenv_s` fails; path never used |
| [BSLIB-002](BSLIB-002.md) | BSLib debug | minor | Stack frame text prints the decorated name; the undecorated one is thrown away |
| [BSLIB-003](BSLIB-003.md) | BSLib debug | minor | Call stack in the assert dialog is cut at 520 characters although the buffer holds 4096 |
| [BSLIB-004](BSLIB-004.md) | BSLib debug | minor | Minidump: `CreateFileA` failure is not checked; dump names can collide |
| [BSLIB-005](BSLIB-005.md) | BSLib debug | minor | dbghelp.dll is reloaded on every assert after a failed load; freed handle kept |
| [BSLIB-006](BSLIB-006.md) | BSLib debug | wrong | Assert dialog silently breaks into the debugger when the message is longer than 1023 characters |
| [BSNET-001](BSNET-001.md) | CMsg | wrong | String writes clamp without the 2-byte length prefix and throw instead of truncating |
| [BSNET-002](BSNET-002.md) | CMsg | crash | Reading past the end of a received packet crashes the process on purpose |
| [BSNET-003](BSNET-003.md) | CMsg | wrong | String reads in pop-back mode return without reading and leave the output unchanged |
| [BSNET-004](BSNET-004.md) | CMsg | minor | Constructor copies an uninitialised pointer into the WSABUF (overwritten right after) |
| [FILE-001](FILE-001.md) | Files | wrong | `CFileMem::Open` allocates a new buffer on every open; the old one leaks |
| [FILE-002](FILE-002.md) | Files | wrong | `CFileMM` opens for reading with `OPEN_ALWAYS`: missing files are created empty; error paths leak handles |
| [FILE-003](FILE-003.md) | Files | minor | `IFile::Read` logs the 4090-byte block limit instead of the requested size |
| [FILE-004](FILE-004.md) | Files | minor | Lower-casing reads past a string that ends with a DBCS lead byte |
| [FILE-005](FILE-005.md) | Files | wrong | Cache purge subtracts files that were never counted, so cache usage is under-reported |
| [FWDATA-001](FWDATA-001.md) | Server data | minor | The second route map is freed only when it is already empty |
| [FWDATA-002](FWDATA-002.md) | Server data | minor | Status broadcasts go to session 0 for notify bodies that are not connected |
| [FWDATA-003](FWDATA-003.md) | Server data | minor | A duplicate privileged-IP group leaks its address set |
| [FWUI-001](FWUI-001.md) | Monitor GUI | wrong | Popup menus are shown without `TPM_RETURNCMD`, so menu commands never run |
| [FWUI-002](FWUI-002.md) | Monitor GUI | crash | The monitor view is deleted while its window still exists (use after free at shutdown) |
| [FWUI-003](FWUI-003.md) | Monitor GUI | crash | Graph names copied with a 256-character limit into a 64-byte field |
| [FWUI-004](FWUI-004.md) | Monitor GUI | minor | Architecture view icons are freed with `DeleteObject`; overlay handle not cleared |
| [FWUI-005](FWUI-005.md) | Monitor GUI | crash | Cord status and body state index tables without a range check |
| [FWUI-006](FWUI-006.md) | Monitor GUI | wrong | Rebuilding the architecture view keeps old entries and duplicates children |
| [FWUI-007](FWUI-007.md) | Monitor GUI | minor | Colour codes below '0' read before the colour table |
| [FWUI-008](FWUI-008.md) | Monitor GUI | minor | The PERFMON log line is formatted into itself and starts with ", " |
| [FWUI-009](FWUI-009.md) | Monitor GUI | crash | Counters of type 0 have no type name; the legend calls strlen(NULL) |
| [FWUI-010](FWUI-010.md) | Monitor GUI | crash | Queued dialog texts are freed but stay in the list (double free) |
| [FWUTIL-001](FWUTIL-001.md) | Server config | crash | Server.cfg certification addresses copied with a 256-character limit into 16-byte fields |
| [FWUTIL-002](FWUTIL-002.md) | Server config | crash | Server.cfg tokenizer copies tokens into a 1 KB stack buffer without a size limit |
| [FWUTIL-003](FWUTIL-003.md) | Server config | minor | The Server.cfg text buffer is never freed |
| [FWUTIL-004](FWUTIL-004.md) | Monitor | wrong | Net engine counters are registered with a sample limit of 0, so their graphs stay empty |
| [FWUTIL-005](FWUTIL-005.md) | Monitor | minor | The monitor thread handle is never closed; a stale stop event is kept |
| [FWUTIL-006](FWUTIL-006.md) | Date/time | crash | `CDateTime` getters dereference NULL when `localtime` rejects the time |
| [FWUTIL-007](FWUTIL-007.md) | Date/time | wrong | Local smalldatetime ignores daylight saving time |
| [NET-001](NET-001.md) | NetEngine | minor | `Initialize` starts Winsock with the previous parameter block's version |
| [NET-002](NET-002.md) | NetEngine | minor | `GetStatistics` reports the accepted-socket pool for the connect pool too |
| [NET-003](NET-003.md) | NetEngine | wrong | `Connect` and `AddKeepAliveTarget` resolve the bind address but never use it |
| [NET-004](NET-004.md) | NetEngine | minor | MAC address lookup allocates 640 times the needed buffer |
| [NET-005](NET-005.md) | NetEngine | crash | Logging crashes when there is no engine (before construction or after `Shutdown`) |
| [NET-006](NET-006.md) | NetEngine | minor | A session event that cannot be queued is never released |
| [NET-007](NET-007.md) | NetEngine | wrong | A session whose socket cannot be attached stays registered after its connect event |
| [NET-008](NET-008.md) | NetEngine | wrong | A file transfer offset only moves the byte counter, not the file position |
| [NET-009](NET-009.md) | NetEngine | minor | `CreateNewSession` leaks the session when no ID is left |
| [NET-010](NET-010.md) | NetEngine | crash | `DumpMsgPool` walks the in-use set without the pool lock |
| [NET-011](NET-011.md) | NetEngine | wrong | Accepted sessions are announced before the peer address is stored |
| [NET-012](NET-012.md) | NetEngine | minor | Keep-alive reconnects block with the lock held, stalling session closes |
| [NET-013](NET-013.md) | NetEngine | minor | A second disconnect overwrites the close reason and posts another `TransmitFile` |
| [PROACT-001](PROACT-001.md) | Proactor | crash | A failed completion of a pooled message is released twice |
| [PROACT-002](PROACT-002.md) | Proactor | crash | Event proactor leaves NULL handles in its wait array; the worker exits |
| [PROACT-003](PROACT-003.md) | Proactor | minor | Failed stop packets are never detected |
| [PROACT-004](PROACT-004.md) | Proactor | crash | `Proactor_Create` initializes the created job without a NULL check |
| [PROACT-005](PROACT-005.md) | Proactor | wrong | Stopping the overlapped peer process does not drain its send queue; messages leak |
| [SEC-001](SEC-001.md) | Security | wrong | Blowfish encryption does not pad buffers shorter than 7 bytes and reads 8 source bytes |
| [SEC-002](SEC-002.md) | Security | crash | Random prime search can divide by zero or return 1 for small start values |
| [THREAD-001](THREAD-001.md) | Threads | crash | Thread start thunk runs from heap memory; faults when DEP is enforced |
| [THREAD-002](THREAD-002.md) | Threads | crash | `CThreadControl::Stop` uses the job pointer without a NULL check |
| [THREAD-003](THREAD-003.md) | Threads | crash | Random ideal-processor choice can divide by zero or pick invalid CPUs |
| [THREAD-004](THREAD-004.md) | Threads | minor | Stopped thread controls stay in the manager's map until shutdown |
| [THREAD-005](THREAD-005.md) | Threads | crash | Task destructor clears its thread manager first, so worker threads are never stopped |
| [THREAD-006](THREAD-006.md) | Threads | crash | Task queue stopped without a NULL check |
| [SOCK-001](SOCK-001.md) | Sockets | crash | mswsock.dll is freed after `AcceptEx`/`TransmitFile` pointers are taken from it |
| [SOCK-002](SOCK-002.md) | Sockets | wrong | Socket buffer size helper halves the size after every success |
| [SOCK-003](SOCK-003.md) | Sockets | wrong | A message rejected by the send queue limit is never released |
| [SOCK-004](SOCK-004.md) | Sockets | minor | A failed `TransmitFile` disconnect is never detected |
| [SOCK-005](SOCK-005.md) | Sockets | crash | A datagram without a message task is released twice (unreachable today) |
| [SOCK-006](SOCK-006.md) | Sockets | minor | UDP ping packets carry four uninitialised stack bytes |
| [SOCK-007](SOCK-007.md) | Sockets | crash | A UDP ping host name that does not resolve crashes the process |
| [SOCK-008](SOCK-008.md) | Sockets | minor | UDP ping receive errors are only reported when formatting fails |
| [SOCK-009](SOCK-009.md) | Sockets | wrong | Removing a UDP ping target is inverted |
| [SOCK-010](SOCK-010.md) | Sockets | wrong | The handshake state byte is never initialised |
| [SRVFW-001](SRVFW-001.md) | ServerFramework | crash | Crash-dump fallback writes its .txt only when `fopen_s` fails, through an invalid `FILE*` |
| [SRVFW-002](SRVFW-002.md) | CMassiveMsg | wrong | Reads and writes cross at most one part boundary and throw for anything larger |
| [SRVFW-003](SRVFW-003.md) | CMassiveMsg | minor | Peeking the inner opcode leaves the read cursor one byte further on two paths |
