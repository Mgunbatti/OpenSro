# GWCERT-007: privileged address range up to 255.255.255.255 never ends

- Original: `IpRange_AddToSet` (0x40ff30), GatewayServer
- Rewrite: `CPrivilegedIPManager::AddRange` in `JMX_CommonServers/GatewayServer/PrivilegedIPManager.cpp`
- Severity: minor (configuration dependent hang)

## What the original does

Each `_PrivilegedIP` row is a range of two addresses. The function converts both to host order
and inserts every address with `for (ip = first; ip <= last; ++ip)`.

## Effect

A range whose last address is 255.255.255.255 loops forever (the counter wraps to 0, which is
again `<= last`), inserting all 2^32 addresses into the hash set until memory runs out; the
server hangs in start-up. Wide ranges in general (a /8 is 16 million entries) take long and much
memory.

## Fix

Stop after inserting `last` (test for equality before incrementing), and reject unreasonably
large ranges.
