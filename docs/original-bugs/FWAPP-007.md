# FWAPP-007: massive relay leaves its job registered when the copy fails

- Original: `ServerFramework::RelayMassiveMsgToBody_6008` (0x4450e0), GatewayServer
- Rewrite: `RelayMassiveMsgToBody_6008` in `JMX_ServerFramework/ServerFramework/ServerProcess.cpp`
- Severity: minor

## What the original does

The relay job is set up and registered before the data of the massive message is copied into
the 0x6008 message. When the copy fails (a part without payload), both massive messages are
freed and false is returned, but the job stays in the job map; the CMsg variant
(`RelayMsgToBody_6008`) removes it in the corresponding case.

## Effect

The job and its ID are lost until the route's session closes (only then are relay jobs of the
cord failed and freed); the caller additionally answers 0xA008 failure for the same relay.

## Fix

Remove and release the job (`RemoveAndRelease`) when the copy fails.
