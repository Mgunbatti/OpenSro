# FWAPP-006: 0x6009 message leaked when the target queue refuses it

- Original: `CServerProcessBase::RegisterObjAndSendMsg6009` (0x445410), GatewayServer
- Rewrite: `CServerProcessBase::SendOverlapJob` in `JMX_ServerFramework/ServerFramework/ServerProcess.cpp`
- Severity: minor

## What the original does

The 0x6009 message is allocated from the engine, the job registered, and the message pushed to
the target task's queue. When the push fails (queue stopped) the job is unregistered and
returned to its pool, but the message is not released.

## Effect

One engine message is lost per refused job (only while the server is shutting down).

## Fix

Release the message when the push fails.
