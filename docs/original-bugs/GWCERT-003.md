# GWCERT-003: captcha codes come from an unseeded random generator

- Original: `CIBUVProcess::Initialize` (0x4196b0), `IBUVImage_ResetAndGenerateCode` (0x419250),
  `IBUV_CreateImage` (0x419a90), GatewayServer
- Rewrite: `CIBUVProcess::Initialize`, `IBUVData::ResetAndGenerateCode`,
  `CIBUVProcess::CreateImage` in `JMX_CommonServers/GatewayServer/IBUVProcess.cpp`
- Severity: high (captcha predictable)

## What the original does

`Initialize` calls `srand(time(NULL))` while the application initialises its local data, on a
thread of the main or overlap process. The images themselves are made on the IBUV thread (the
message loop's refill every 100 ms and the on-demand image of 0x6321; the 20000 images
`Initialize` means to make are never made, GWCERT-004). The static CRT keeps the `rand()` state
per thread and starts every thread at seed 1, so `srand` on the initialising thread has no
effect on the IBUV thread.

## Effect

The captcha codes and the distortion parameters follow the same sequence after every server
start: the first image of each run has the same code, the second the same, and so on. Anyone who
records the sequence once can answer the captchas of every later run without reading the images.

## Fix

Seed on the IBUV thread (at the start of the message loop), or use a proper random source
(`CryptGenRandom`) for the codes.
