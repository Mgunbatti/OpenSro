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
| [SEC-001](SEC-001.md) | Security | wrong | Blowfish encryption does not pad buffers shorter than 7 bytes and reads 8 source bytes |
| [SEC-002](SEC-002.md) | Security | crash | Random prime search can divide by zero or return 1 for small start values |
| [SRVFW-001](SRVFW-001.md) | ServerFramework | crash | Crash-dump fallback writes its .txt only when `fopen_s` fails, through an invalid `FILE*` |
| [SRVFW-002](SRVFW-002.md) | CMassiveMsg | wrong | Reads and writes cross at most one part boundary and throw for anything larger |
| [SRVFW-003](SRVFW-003.md) | CMassiveMsg | minor | Peeking the inner opcode leaves the read cursor one byte further on two paths |
