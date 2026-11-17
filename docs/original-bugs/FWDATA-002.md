# FWDATA-002: status broadcasts go to session 0 for notify bodies that are not connected

- Original: `ServerBody_BroadcastToNotifySetsAndFree` 0x427740 (GatewayServer)
- Rewrite: `ServerBody_BroadcastToNotifySetsAndFree` in
  `JMX_ServerFramework/ServerFramework/ServerBody.cpp`
- Severity: minor

## What the original does

For each body ID in the notify set the function looks up the route cord (`ServerBody_Find`) and
sends the 0x2005 massive message to the cord's session unless it is the excluded session. A cord
that is not connected has session 0; it is not skipped, so unless the excluded session is also 0
the header and every part are passed to `SendMsg(0, ...)`.

## Effect

While a peer server is down every body or cord status change makes the engine look up session 0
for each message part. Session IDs are never 0, so the sends fail without effect; they only cost
time. The peer learns the states when it reconnects anyway.

## Fix

Skip routes whose `dwSessionID` is 0, as `ServerBody_SendMsg` does.
