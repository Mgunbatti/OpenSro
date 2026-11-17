# GWCERT-011: captcha failure counts per address never expire

- Original: `CaptchaMap_IncrementOrInsert` (0x417ce0), `STL_tree_IsKeyAbsent_LoginMap` (0x417d60),
  GatewayServer
- Rewrite: `CIBUVIPBlockMgr::AddFailure` / `IsBlocked` in
  `JMX_CommonServers/GatewayServer/IBUVProcess.cpp`
- Severity: medium

## What the original does

With `IBUVFailureIPTolerance` set, every wrong captcha answer adds one to the count of the client
address, and the login request (0x6102) is refused with 0xA102 {2, 6} once the count is above the
tolerance. Nothing ever lowers or removes a count: neither a right answer, a successful login nor
time.

## Effect

An address that collected more failures than tolerated (over any period, by any number of
users behind it, e.g. a NAT or internet cafe) cannot log in until the GatewayServer restarts.

## Fix

Reset the count of an address on a right answer and let counts expire after a while.
