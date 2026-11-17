#include "ServerConfig.h"

#include <stdlib.h>
#include <strsafe.h>

#include "BSLib/BSDebug.h"
#include "BSNet/IBSNet.h"

// StrCmpI_Locale (0x41f430, server application layer): 0 when the strings are equal.
int StrCmpI_Locale(const char* psz1, const char* psz2);

SERVER_GLOBALS g_ServerGlobals;

const SERVER_DEBUG_OPTION g_aServerDebugOption[9] =
{
    { "DEBUG_OPTION_ASSERT_DONOT_SHOW_MESSAGEBOX",      0x001 },
    { "DEBUG_OPTION_ASSERT_SHOW_MESSAGEBOX_OKCANCEL",   0x002 },
    { "DEBUG_OPTION_ASSERT_SHOW_MESSAGEBOX_OK",         0x004 },
    { "DEBUG_OPTION_ASSERT_SHOW_CALLSTACK",             0x008 },
    { "DEBUG_OPTION_ASSERT_WRITE_MINIDUMP",             0x200 },
    { "DEBUG_OPTION_ASSERT_ADVANCE_BREAK",              0x010 },
    { "DEBUG_OPTION_ASSERT_ADVANCE_NORMAL",             0x020 },
    { "DEBUG_OPTION_ASSERT_CANCEL_EXIT",                0x100 },
    { "DEBUG_OPTION_ASSERT_CALL_CALLBACK",              0x400 },
};

namespace
{

// The keyword comparison ServerConfig_Load inlines (the same test as StrCmpI_Locale).
inline bool CfgIs(const char* pszToken, const char* pszWord)
{
    int nWord = lstrlenA(pszWord);
    int nToken = lstrlenA(pszToken);
    return CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE | NORM_IGNOREKANATYPE, pszToken,
                          nToken, pszWord, nWord) == CSTR_EQUAL;
}

enum CFG_RESULT
{
    CFG_NEXT,           // read the next token of the block
    CFG_LEAVE_BLOCK,    // continue after the block with the next token
    CFG_FAIL,           // ServerConfig_Load returns false
};

// The "<name> { DEBUG_OPTION_... }" options. bInlineEnd / bInlineFlags: whether the "}" and
// the option names are compared with the inlined comparison or with StrCmpI_Locale, as in
// the original.
CFG_RESULT ReadDebugOptionBlock(char*& p, char* pszToken, DWORD& dwFlags, bool bInlineEnd,
                                bool bInlineFlags)
{
    dwFlags = 0;
    p = Cfg_ReadNextToken(p, pszToken);
    if (!p || StrCmpI_Locale(pszToken, "{") != 0)
        return CFG_LEAVE_BLOCK;
    p = Cfg_ReadNextToken(p, pszToken);
    while (p)
    {
        if (bInlineEnd ? CfgIs(pszToken, "}") : StrCmpI_Locale(pszToken, "}") == 0)
            return CFG_NEXT;
        for (int i = 0; i < 9; ++i)
        {
            const SERVER_DEBUG_OPTION& opt = g_aServerDebugOption[i];
            if (bInlineFlags ? CfgIs(pszToken, opt.pszName)
                             : StrCmpI_Locale(pszToken, opt.pszName) == 0)
            {
                dwFlags |= opt.dwFlag;
            }
        }
        p = Cfg_ReadNextToken(p, pszToken);
    }
    return CFG_NEXT;
}

// "<name> <value>" options whose value counts only when it is 1.
CFG_RESULT ReadBoolOption(char*& p, char* pszToken, BOOL& bValue)
{
    p = Cfg_ReadNextToken(p, pszToken);
    if (!p)
        return CFG_LEAVE_BLOCK;
    bValue = atol(pszToken) == 1;
    return CFG_NEXT;
}

// One entry of the "common" or the module's block; pszToken holds its first token.
CFG_RESULT ReadOption(char*& p, char* pszToken)
{
    SERVER_GLOBALS& g = g_ServerGlobals;

    if (CfgIs(pszToken, "certification"))
    {
        p = Cfg_ReadNextToken(p, pszToken);
        if (!p)
            return CFG_LEAVE_BLOCK;
        // BUG(FWUTIL-001): 256 characters allowed into a 16-byte field.
        if (StringCchCopyA(g.szCertificationIP, 256, pszToken) != S_OK)
        {
            BSAssertFail();
            return CFG_FAIL;
        }
        // separator, then the port
        p = Cfg_ReadNextToken(p, pszToken);
        if (!p)
            return CFG_LEAVE_BLOCK;
        p = Cfg_ReadNextToken(p, pszToken);
        if (!p)
            return CFG_LEAVE_BLOCK;
        g.nCertificationPort = atol(pszToken);
        return CFG_NEXT;
    }
    if (CfgIs(pszToken, "certification_ip_bind"))
    {
        p = Cfg_ReadNextToken(p, pszToken);
        if (!p)
            return CFG_LEAVE_BLOCK;
        // BUG(FWUTIL-001): 256 characters allowed into a 16-byte field.
        if (StringCchCopyA(g.szCertificationBindIP, 256, pszToken) != S_OK)
        {
            BSAssertFail();
            return CFG_FAIL;
        }
        return CFG_NEXT;
    }
    if (CfgIs(pszToken, "debug_option_debugger_present"))
        return ReadDebugOptionBlock(p, pszToken, g.dwDebugOptionDebuggerPresent, true, true);
    if (StrCmpI_Locale(pszToken, "debug_option_stand_alone") == 0)
        return ReadDebugOptionBlock(p, pszToken, g.dwDebugOptionStandAlone, false, true);
    if (StrCmpI_Locale(pszToken, "netengine_debug_option_debugger_present") == 0)
    {
        return ReadDebugOptionBlock(p, pszToken, g.dwNetEngineDebugOptionDebuggerPresent,
                                    false, false);
    }
    if (StrCmpI_Locale(pszToken, "netengine_debug_option_stand_alone") == 0)
    {
        return ReadDebugOptionBlock(p, pszToken, g.dwNetEngineDebugOptionStandAlone, false,
                                    false);
    }
    if (StrCmpI_Locale(pszToken, "DumpMsgPool") == 0)
        return ReadBoolOption(p, pszToken, g.bDumpMsgPool);
    if (StrCmpI_Locale(pszToken, "ReportConnectionLog") == 0)
        return ReadBoolOption(p, pszToken, g.bReportConnectionLog);
    if (StrCmpI_Locale(pszToken, "MaxSendQueDepth") == 0)
    {
        p = Cfg_ReadNextToken(p, pszToken);
        if (!p)
            return CFG_LEAVE_BLOCK;
        g.nMaxSendQueDepth = atol(pszToken);
        return CFG_NEXT;
    }
    if (StrCmpI_Locale(pszToken, "PromptAtClose") == 0)
        return ReadBoolOption(p, pszToken, g.bPromptAtClose);
#ifndef JMX_FRAMEWORK_2007
    // FarmManager 0x40a7b0 has no such key (it ends in mapExtra).
    if (StrCmpI_Locale(pszToken, "WritePerfLogToText") == 0)
        return ReadBoolOption(p, pszToken, g.bWritePerfLogToText);
#endif

    std::string strKey(pszToken);
    p = Cfg_ReadNextToken(p, pszToken);
    if (!p)
        return CFG_LEAVE_BLOCK;
    std::string strValue(pszToken);
    g.mapExtra.insert(std::pair<std::string, std::string>(strKey, strValue));
    return CFG_NEXT;
}

} // namespace

bool ServerConfig_Load()
{
    char szFileName[MAX_PATH];
    if (StringCchPrintfA(szFileName, MAX_PATH, "%s\\Server.cfg",
                         g_ServerGlobals.szModuleDir) != S_OK)
    {
        BSAssertFail();
        return false;
    }

    HANDLE hFile = CreateFileA(szFileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;
    DWORD dwSize = GetFileSize(hFile, NULL);
    // BUG(FWUTIL-003): the text buffer is never freed.
    char* pText = new char[dwSize + 1];
    pText[dwSize] = 0;
    ReadFile(hFile, pText, dwSize, &dwSize, NULL);
    CloseHandle(hFile);

    // BUG(FWUTIL-002): Cfg_ReadNextToken does not know the size of this buffer.
    char szToken[SERVER_CONFIG_TOKEN_SIZE];
    char* p = Cfg_ReadNextToken(pText, szToken);
    while (p)
    {
        if (CfgIs(szToken, "common") || CfgIs(szToken, SERVER_CONFIG_MODULE_NAME))
        {
            p = Cfg_ReadNextToken(p, szToken);
            if (!p || !CfgIs(szToken, "{"))
                return true;
            p = Cfg_ReadNextToken(p, szToken);
            while (p && !CfgIs(szToken, "}"))
            {
                CFG_RESULT nResult = ReadOption(p, szToken);
                if (nResult == CFG_FAIL)
                    return false;
                if (nResult == CFG_LEAVE_BLOCK)
                    break;
                p = Cfg_ReadNextToken(p, szToken);
            }
        }
        else
        {
            // a block of another module: skipped up to its "}"
            p = Cfg_ReadNextToken(p, szToken);
            if (!p || !CfgIs(szToken, "{"))
                return true;
            p = Cfg_ReadNextToken(p, szToken);
            while (p && !CfgIs(szToken, "}"))
                p = Cfg_ReadNextToken(p, szToken);
        }
        p = Cfg_ReadNextToken(p, szToken);
    }
    return true;
}

namespace
{

inline bool CfgIsSingleCharToken(char c)
{
    return c == '{' || c == '}' || c == ')' || c == '(' || c == '\'' || c == ':' || c == ',' ||
           c == '-';
}

} // namespace

char* Cfg_ReadNextToken(const char* p, char* pszToken)
{
    int n = 0;
    *pszToken = 0;
    if (!p)
        return NULL;

    // char is signed: bytes >= 0x80 count as white space here and end a token below
    char c;
    for (;;)
    {
        c = *p;
        if (c <= ' ')
        {
            do
            {
                if (c == 0)
                    return NULL;
                c = *++p;
            } while (c <= ' ');
        }
        if (c != '/')
            break;
        if (p[1] == '/')
        {
            // line comment, stops at the newline
            for (c = *p; c != 0; c = *++p)
            {
                if (c == '\n')
                    break;
            }
            continue;
        }
        if (p[1] != '*')
        {
            // a single '/'
            pszToken[0] = c;
            pszToken[1] = 0;
            return (char*)p + 1;
        }
        c = p[2];
        p += 2;
        while (c != 0)
        {
            if (c == '*' && p[1] == '/')
            {
                p += 2;
                break;
            }
            c = *++p;
        }
    }

    if (c == '"')
    {
        c = p[1];
        p += 2;
        while (c != 0)
        {
            if (c == '"')
            {
                pszToken[n] = 0;
                return (char*)p;
            }
            pszToken[n] = c;
            c = *p;
            ++n;
            ++p;
        }
        // unterminated string
        pszToken[n] = 0;
        return NULL;
    }

    if (CfgIsSingleCharToken(c))
    {
        pszToken[0] = c;
        pszToken[1] = 0;
        return (char*)p + 1;
    }

    do
    {
        ++p;
        pszToken[n] = c;
        c = *p;
        ++n;
    } while (!CfgIsSingleCharToken(c) && c != '/' && c > ' ');
    pszToken[n] = 0;
    return (char*)p;
}

void Cfg_InitDefaultsFromFlags(BSNET_INIT_PARAM* pParam, DWORD dwFlags)
{
    pParam->dwFlags = dwFlags;
    if (dwFlags & BSNET_FLAG_SERVER)
    {
        pParam->dwMaxConnectTime = 60;
        pParam->nAcceptCount = (dwFlags & BSNET_FLAG_LARGE) ? 100 : 10;
        pParam->aSocket[0].nSendQueueLimit = -1;
        pParam->aSocket[1].nSendQueueLimit = -1;
    }
    if (dwFlags & 0x20)
    {
        MEMORYSTATUS ms;
        GlobalMemoryStatus(&ms);
        pParam->dwFileCacheLimit = ms.dwTotalPhys >> 2;
        pParam->nSendFileType = 2;
        pParam->nRecvFileType = 1;
    }
    else
    {
        pParam->dwFileCacheLimit = 0;
        pParam->nSendFileType = 1;
        pParam->nRecvFileType = (dwFlags & BSNET_FLAG_SERVER) ? 1 : 0;
    }
    // BSDebug flag words of the engine (debugger present / stand alone)
    pParam->dw8C = 0x112;
    pParam->aSocket[1].dwSendMode = 1;
    pParam->aSocket[0].dwSendMode = 0;
    pParam->dw90 = (dwFlags & BSNET_FLAG_SERVER) ? 0x201 : 0x12A;
}
