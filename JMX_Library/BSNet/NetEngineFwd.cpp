#include "NetEngineFwd.h"

// Defined here until CNetEngine is ported.
IBSNet* g_pNetEngine = NULL;
NETENGINE_DATAGRAM* g_pNetEngineDatagram = NULL;
int g_nNetIoModel = -1;
BOOL g_bNetEngineShutdown = FALSE;
DWORD g_dwNetDatagramTaskID = 0x10000005;
