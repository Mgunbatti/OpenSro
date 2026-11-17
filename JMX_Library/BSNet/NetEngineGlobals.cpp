// Process-wide state of the network engine. Kept apart from NetEngine.cpp so that code using
// only the sockets and proactors does not pull in the engine itself.

#include "NetEngine.h"

IBSNet* g_pNetEngine = NULL;
BOOL g_bNetEngineShutdown = FALSE;
int g_nNetIoModel = -1;
DWORD g_dwNetDatagramTaskID = NETENGINE_TASK_DATAGRAM;
NETENGINE_DATAGRAM* g_pNetEngineDatagram = NULL;

void __cdecl NetEngine_DefaultDumpMsgPool(DWORD, DWORD)
{
}
