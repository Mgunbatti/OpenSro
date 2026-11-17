# FWDATA-003: a duplicate address group leaks its address set

- Original: `CertData_BuildPrivilegedIPMaps` 0x44c570 (GatewayServer)
- Rewrite: `CertData_BuildPrivilegedIPMaps` in `JMX_ServerFramework/ServerFramework/CertData.cpp`
- Severity: minor

## What the original does

For every 289-byte group record the function allocates a new hash set (0x28 bytes), fills it with
the addresses of the group and inserts `(group ID, set)` into the hash_map at 0x53B934. `insert`
does not replace an existing key and the result is ignored, so for a second record with the same
group ID the new set is neither stored nor freed.

## Effect

Only with inconsistent certification data (two groups with one ID): the second group's addresses
are ignored and its set leaks until the process ends.

## Fix

Check the insert result and merge the addresses into (or delete) the set when the key exists.
