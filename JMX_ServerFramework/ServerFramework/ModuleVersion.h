#pragma once

#include <windows.h>
#include <hash_map>
#include <list>
#include <map>

#include "ServerFramework/MassiveMsg.h"

// Client module versions and patch files (rows of _ModuleVersion / _ModuleVersionFile), loaded
// from the database or received from the division manager in 0xA306.

#pragma pack(push, 1)

struct MODULE_VERSION
{
    BYTE    ab0[6];             // +0
    BYTE    byModuleID;         // +6
    DWORD   dwVersion;          // +7 latest version of the module
    BYTE    ab11[321];          // +11
};
C_ASSERT(sizeof(MODULE_VERSION) == 332);

struct MODULE_VERSION_FILE
{
    DWORD   dwID;               // +0
    DWORD   dwVersion;          // +4
    BYTE    ab8[2];             // +8
    BYTE    byModuleID;         // +10
    char    szName[256];        // +11
    char    szPath[256];        // +267 "." means the client root and is stored as ""
    DWORD   dwSize;             // +523
    BYTE    ab527[5];           // +527
    BYTE    b532;               // +532 sent to the client after the size
    BYTE    ab533[16];          // +533
    BYTE    byValid;            // +549 must be 1 for files sent to clients
};
C_ASSERT(sizeof(MODULE_VERSION_FILE) == 550);

#pragma pack(pop)

typedef std::list<MODULE_VERSION*>      ModuleVersionList;
typedef std::list<MODULE_VERSION_FILE*> ModuleVersionFileList;

extern ModuleVersionList*       g_pModuleVersionList;       // 0x4F9CD4
extern ModuleVersionFileList*   g_pModuleVersionFileList;   // 0x4F9CD8
extern std::map<BYTE, MODULE_VERSION*>                      g_mapModuleVersion;     // 0x53B800
extern stdext::hash_map<DWORD, MODULE_VERSION_FILE*>        g_hmapModuleVersionFile; // 0x53B810

// Takes ownership of both lists and indexes them.
bool ModuleVersion_StoreAndIndexLists(ModuleVersionFileList* pFiles,
                                      ModuleVersionList* pModules);
void ModuleVersion_FreeAllData();
// Latest version of the module, 0 when unknown.
DWORD ModuleVersion_GetLatest(BYTE byModuleID);
// Writes (BYTE 1, file) for every valid file of the module newer than dwVersion, then BYTE 0.
// File: DWORD ID, WORD length + name, WORD length + path, DWORD size, BYTE +532.
void ModuleVersion_WritePatchFiles(ServerFramework::CMassiveMsg* pMsg, BYTE byModuleID,
                                   DWORD dwVersion);
