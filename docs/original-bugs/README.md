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
| [SRVFW-001](SRVFW-001.md) | ServerFramework | crash | Crash-dump fallback writes its .txt only when `fopen_s` fails, through an invalid `FILE*` |
| [SRVFW-002](SRVFW-002.md) | CMassiveMsg | wrong | Reads and writes cross at most one part boundary and throw for anything larger |
| [SRVFW-003](SRVFW-003.md) | CMassiveMsg | minor | Peeking the inner opcode leaves the read cursor one byte further on two paths |
