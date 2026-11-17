# THREAD-001: thread thunk executes from heap memory

- Original: `CSimpleThread::InitThunk`, GatewayServer 0x49ca70 (thread start through
  `CThread_CreateSuspended` 0x49cf20)
- Rewrite: `CSimpleThread::CSimpleThread` / `CSimpleThread::Create` in
  `JMX_Library/BSLib/thread/SimpleThread.cpp`
- Severity: minor (environment dependent)

## What the original does

Every `CSimpleThread` writes a 14-byte ATL-style x86 thunk into its own object (`+4`) and passes
that address to `_beginthreadex` as the thread procedure. The objects are allocated with `operator
new`, so the thread starts by executing code in non-executable heap memory.

## Effect

Works only while DEP is not enforced for the process (the executables are not marked NX compatible).
With DEP set to AlwaysOn, or if the binary is ever linked with `/NXCOMPAT`, every worker thread
faults on its first instruction and the server crashes at start-up.

## Fix

Use a static thread procedure that receives the object pointer as the `_beginthreadex` argument, or
allocate thunks from `VirtualAlloc(PAGE_EXECUTE_READWRITE)` memory.
