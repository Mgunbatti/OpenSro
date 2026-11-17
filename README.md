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
