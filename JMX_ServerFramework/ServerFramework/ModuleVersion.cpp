#include "ModuleVersion.h"

#include "ServerDataExtern.h"

using ServerFramework::CMassiveMsg;

ModuleVersionList*      g_pModuleVersionList = NULL;
ModuleVersionFileList*  g_pModuleVersionFileList = NULL;
std::map<BYTE, MODULE_VERSION*>                 g_mapModuleVersion;
stdext::hash_map<DWORD, MODULE_VERSION_FILE*>   g_hmapModuleVersionFile;

bool ModuleVersion_StoreAndIndexLists(ModuleVersionFileList* pFiles,
                                      ModuleVersionList* pModules)
{
    g_pModuleVersionFileList = pFiles;
    g_pModuleVersionList = pModules;
    for (ModuleVersionList::iterator it = pModules->begin(); it != pModules->end(); ++it)
        g_mapModuleVersion.insert(std::make_pair((*it)->byModuleID, *it));

    for (ModuleVersionFileList::iterator it = g_pModuleVersionFileList->begin();
         it != g_pModuleVersionFileList->end(); ++it)
    {
        MODULE_VERSION_FILE* pFile = *it;
        if (CompareNoCase(pFile->szPath, ".") == CSTR_EQUAL)
            pFile->szPath[0] = 0;
        g_hmapModuleVersionFile.insert(std::make_pair(pFile->dwID, pFile));
    }
    return true;
}

template <class T>
static void FreeVersionList(std::list<T*>*& pList)
{
    if (!pList)
        return;
    for (typename std::list<T*>::iterator it = pList->begin(); it != pList->end(); ++it)
        operator delete(*it);
    pList->clear();
    delete pList;
    pList = NULL;
}

void ModuleVersion_FreeAllData()
{
    FreeVersionList(g_pModuleVersionList);
    FreeVersionList(g_pModuleVersionFileList);
    g_mapModuleVersion.clear();
    g_hmapModuleVersionFile.clear();
}

DWORD ModuleVersion_GetLatest(BYTE byModuleID)
{
    std::map<BYTE, MODULE_VERSION*>::iterator it = g_mapModuleVersion.find(byModuleID);
    if (it == g_mapModuleVersion.end())
        return 0;
    return it->second->dwVersion;
}

void ModuleVersion_WritePatchFiles(CMassiveMsg* pMsg, BYTE byModuleID, DWORD dwVersion)
{
    BYTE b;
    for (ModuleVersionFileList::iterator it = g_pModuleVersionFileList->begin();
         it != g_pModuleVersionFileList->end(); ++it)
    {
        MODULE_VERSION_FILE* pFile = *it;
        if (pFile->byModuleID != byModuleID || pFile->dwVersion <= dwVersion)
            continue;
        if (pFile->byValid != 1)
            BSAssertFail();

        b = 1;
        pMsg->Write(&b, 1);
        pMsg->Write(&pFile->dwID, 4);
        WORD wLen = (WORD)lstrlenA(pFile->szName);
        pMsg->Write(&wLen, 2);
        if (wLen)
            pMsg->Write(pFile->szName, wLen);
        wLen = (WORD)lstrlenA(pFile->szPath);
        pMsg->Write(&wLen, 2);
        if (wLen)
            pMsg->Write(pFile->szPath, wLen);
        pMsg->Write(&pFile->dwSize, 4);
        pMsg->Write(&pFile->b532, 1);
    }
    b = 0;
    pMsg->Write(&b, 1);
}
