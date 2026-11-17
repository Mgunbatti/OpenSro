# SRVFW-002: massive message reads and writes cross only one part

- Original: `CMassiveMsg::Write` 0x41f5d0, `CMassiveMsg::Read` 0x41f6a0 (GatewayServer)
- Rewrite: `CMassiveMsg::Write` / `Read` in `JMX_ServerFramework/ServerFramework/MassiveMsg.cpp`
- Severity: wrong

## What the original does

A write that does not fit the current part fills the rest of it, starts exactly one new part
and writes the remainder there. A read that runs past the current part takes the rest from the
next part only. Neither loops, so a single call larger than about one part (4089 data bytes)
that spans more than one boundary goes to `CMsg::WriteBytes` / `ReadBytes` with too many bytes
and throws `(int)0`.

`Read` also asserts with the dialog (`m_nCurrentReadMsg < m_MsgList.size()`) and then indexes
the vector anyway, which throws `std::out_of_range` when the message is shorter than expected.

## Effect

Code has to write and read big blocks in pieces; a large `operator<<` / `>>` on a struct
bigger than 4 KB fails at run time.

## Fix

Loop over parts until the whole length is done, and stop cleanly at the end of the list.
