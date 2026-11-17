# PROACT-004: Proactor_Create does not check the created job

- Original: `Proactor_Create` 0x495a10 (GatewayServer)
- Rewrite: `Proactor_Create` in `JMX_Library/BSNet/Proactor.cpp`
- Severity: crash

## What the original does

The job is created with `CRuntimeClass::CreateObject` (`Callback_InvokeField8` 0x487e20), which
returns NULL for a class without factory and when the factory faults. `Proactor_Create` stores the
result and calls its `Initialize` without a NULL check.

## Effect

Registering a proactor with an abstract or broken runtime class (`CNetEngine::RegisterProactor`)
crashes the process with an access violation instead of failing the registration.

## Fix

Return FALSE when `CreateObject` returns NULL.
