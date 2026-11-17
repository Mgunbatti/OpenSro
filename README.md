# VSRO_Rewrite

C++ rewrite of the Joymax Silkroad Online (vSRO) server modules, based on the IDA analysis in
`..\VSRO_Analysis` (function names, behaviour comments, per-module progress markdown).

Built with the same compiler as the original binaries: **Visual C++ 2005 SP1**
(`cl 14.00.50727.762`, x86, static CRT), kept portable inside `toolchain/` so nothing is
installed on Windows.

## Layout

| Path | Content |
|---|---|
| `toolchain/` | Fetch script, environment script and smoke test for the portable compiler |
| `JMX_Library/` | Shared libraries, laid out like the original `D:\WORK2005\JMX_Library` tree (BSLib, BSNet) |
| `JMX_ServerFramework/` | Server framework, laid out like `D:\WORK2005\JMX_ServerFramework` (server application base, processes, overlap jobs, server data, config, monitor GUI, NT service) |
| `JMX_CommonServers/` | Server applications: GatewayServer, DownloadServer, MachineManager, FarmManager (`nmake gateway download machine farm`). FarmManager is built on the older 2007 framework, see [its README](JMX_CommonServers/FarmManager/README.md) |
| `test/` | Unit tests, built and run by `nmake test` |
| `docs/address-map.tsv` | Original function address and IDA name for every ported function |
| `docs/original-bugs/` | Bugs found in the originals; reproduced on purpose and marked `BUG(<ID>)` in the code |
| `toolchain/test/` | Smoke test: `hello.cpp` must report `_MSC_FULL_VER=140050727` |

## Build

```bat
toolchain\env.cmd
nmake test
```

The original source paths survive in assert strings (`D:\WORK2005\JMX_Library\BSLib\...`,
`D:\WORK2005\JMX_ServerFramework\ServerFramework\...`, `D:\WORK2005\JMX_CommonServers\<Server>\...`);
the repo follows that layout. Where a file name is not recorded (the network library is only known
from the `IBSNet` interface), the name is a guess.

## Toolchain

```bat
powershell -ExecutionPolicy Bypass -File toolchain\fetch_msvc8.ps1   :: ~20 s, ~300 MB in toolchain\msvc8sp1
toolchain\env.cmd                                                   :: per shell: PATH/INCLUDE/LIB
cl /nologo /O2 /MT /EHsc toolchain\test\hello.cpp
```

Origin and checks (the toolchain is a community redistribution, not an official download):

- Archive `archaic-msvc/msvc800_sp1` pinned by SHA-256 (same pin as `jaenster/setup-msvc`).
- `cl.exe` reports `14.00.50727.762`; the original EXEs' Rich headers say build 50727.
- 24 binaries carry a valid Microsoft Authenticode signature. `c1/c1xx/c2.dll` and `mspdb80.dll`
  are unsigned (VS2005 signed them only through the MSI), so the script downloads a second,
  independent copy (`Drahsid/ffxiv_msvc8.0sp1`) and requires all common binaries to be identical.
- Static CRT check: 209 of 245 `libcmt` functions linked into the smoke test occur byte-identically
  (relocations masked) in `GatewayServer.exe`; most of the rest are not used by the server
  (`printf`, `mainCRTStartup`) or straddle the 64-byte comparison window.

## Disclaimer

This project is for educational and research purposes: studying how a mid-2000s MMO server was
built and keeping it interoperable for private study. It is not affiliated with or endorsed by
Joymax or any publisher of Silkroad Online. "Silkroad Online" and related names are trademarks of
their respective owners.

The repository contains only original code written for this project. No original binaries, game
data, client files or database content are included, and none will be accepted. To run anything
built from here you need your own legitimately obtained copy of the game files.
