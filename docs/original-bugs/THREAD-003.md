# THREAD-003: random ideal processor can divide by zero

- Original: `CThreadManager::CreateThreadsWithAffinity`, GatewayServer 0x49a890
- Rewrite: `CThreadManager::CreateThreads` in `JMX_Library/BSLib/thread/ThreadManager.cpp`
- Severity: minor

## What the original does

Both ends of the CPU range are clamped to `[0, processors - 1]` independently, but their order is
not checked. In random mode the ideal processor is `rand() % (nMaxCPU - nMinCPU + 1) + nMinCPU`.

## Effect

With `nMaxCPU == nMinCPU - 1` the modulus is zero and the process dies with an integer
divide-by-zero. With `nMaxCPU` further below `nMinCPU` the result can fall outside the requested
range (and below zero, in which case no ideal processor is set).

## Fix

Swap or reject the bounds when `nMaxCPU < nMinCPU`.
