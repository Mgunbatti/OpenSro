# FWDATA-001: the second route map is freed only when it is already empty

- Original: `ServerApp_FreeCertArchData` 0x422980 (GatewayServer)
- Rewrite: `ServerApp_FreeCertArchData` in `JMX_ServerFramework/ServerFramework/CertArch.cpp`
- Severity: minor

## What the original does

Besides the body route hash_map (0x53B7B8) the framework has a second hash_map at 0x53B790
whose values are heap lists of heap lists. At shutdown the code that frees those nested lists and
clears the map runs only when the map's size is 0 (`cmp [0x53B798], 0` / `jnz skip`): the test is
the wrong way round. `ServerBody_BuildRouteTable` asserts that the map is empty, and nothing in
GatewayServer inserts into it.

## Effect

None in GatewayServer, where the map always stays empty. A module that fills the map would leak
every nested list at shutdown and keep the stale entries.

## Fix

Run the loop and the `clear()` when the map is not empty (or unconditionally).
