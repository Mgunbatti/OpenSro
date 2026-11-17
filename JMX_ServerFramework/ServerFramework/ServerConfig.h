#pragma once

#include <windows.h>
#include <map>
#include <string>

struct BSNET_INIT_PARAM;

// Process globals of the server module (0x53B400 in GatewayServer). One object in the original:
// its dynamic initializer (DynInit_ServerConfigExtraMap, 0x41d0a0) constructs the map member and
// returns the block's address; everything else starts zeroed. ServerApp_Startup fills the first
// part and the defaults of the option fields, ServerConfig_Load the rest from Server.cfg.
struct SERVER_GLOBALS
{
    BOOL        bIOCPMode;                      // +000 0x53B400 cleared by ServerApp_Startup
    HINSTANCE   hInstance;                      // +004 0x53B404
    char        szServiceName[64];              // +008 0x53B408
    char        szCommandLine[MAX_PATH];        // +048 0x53B448
    char        szModuleDir[MAX_PATH];          // +14C 0x53B54C drive and directory of the
                                                //      exe, ending in '\'
    DWORD       dwPublicIP;                     // +250 0x53B650
    char        szPublicIP[16];                 // +254 0x53B654
    DWORD       dwProcessorCount;               // +264 0x53B664
    // Server.cfg "certification <ip> , <port>" and "certification_ip_bind <ip>"
    char        szCertificationIP[16];          // +268 0x53B668
    char        szCertificationBindIP[16];      // +278 0x53B678
    int         nCertificationPort;             // +288 0x53B688
    DWORD       dw28C;                          // +28C
    DWORD       dwMainThreadID;                 // +290 0x53B690
    // BSDebug flag words (BSDBG_*), defaults 0x112 / 0x201
    DWORD       dwDebugOptionDebuggerPresent;   // +294 0x53B694
    DWORD       dwDebugOptionStandAlone;        // +298 0x53B698
    // read from Server.cfg but not used by GatewayServer
    DWORD       dwNetEngineDebugOptionDebuggerPresent;  // +29C 0x53B69C
    DWORD       dwNetEngineDebugOptionStandAlone;       // +2A0 0x53B6A0
    BOOL        bDumpMsgPool;                   // +2A4 0x53B6A4
    BOOL        bReportConnectionLog;           // +2A8 0x53B6A8
    LONG        nMaxSendQueDepth;               // +2AC 0x53B6AC default -1
    BOOL        bPromptAtClose;                 // +2B0 0x53B6B0
    DWORD       dw2B4;                          // +2B4 0x53B6B4 cleared by ServerApp_Startup
    BOOL        bWritePerfLogToText;            // +2B8 0x53B6B8
    // Keys of the module's Server.cfg block that are not options above, with their value.
    std::map<std::string, std::string> mapExtra;    // +2BC 0x53B6BC
};

C_ASSERT(FIELD_OFFSET(SERVER_GLOBALS, szModuleDir) == 0x14C);
C_ASSERT(FIELD_OFFSET(SERVER_GLOBALS, dwProcessorCount) == 0x264);
C_ASSERT(FIELD_OFFSET(SERVER_GLOBALS, szCertificationIP) == 0x268);
C_ASSERT(FIELD_OFFSET(SERVER_GLOBALS, nCertificationPort) == 0x288);
C_ASSERT(FIELD_OFFSET(SERVER_GLOBALS, dwDebugOptionDebuggerPresent) == 0x294);
C_ASSERT(FIELD_OFFSET(SERVER_GLOBALS, bWritePerfLogToText) == 0x2B8);
C_ASSERT(FIELD_OFFSET(SERVER_GLOBALS, mapExtra) == 0x2BC);

extern SERVER_GLOBALS g_ServerGlobals;

// Name of the module's own block in Server.cfg (besides "common").
#define SERVER_CONFIG_MODULE_NAME   "GatewayServer"

// Size of the token buffer ServerConfig_Load passes to Cfg_ReadNextToken.
enum { SERVER_CONFIG_TOKEN_SIZE = 1024 };

// ServerConfig_Load (0x41d910): reads <module dir>\Server.cfg. Only the "common" and the
// module's own block are evaluated, other blocks are skipped:
//
//   common { certification "1.2.3.4", 15880   certification_ip_bind "1.2.3.4" }
//   GatewayServer {
//       debug_option_debugger_present { DEBUG_OPTION_ASSERT_... ... }
//       debug_option_stand_alone { ... }                  (same for netengine_debug_option_*)
//       DumpMsgPool 1   ReportConnectionLog 1   MaxSendQueDepth 300
//       PromptAtClose 1   WritePerfLogToText 1   <any key> <value>
//   }
//
// Returns false when the file cannot be opened or a copy into a fixed buffer fails, true
// otherwise (also when parsing stops early).
bool ServerConfig_Load();

// Cfg_ReadNextToken (0x453d40): copies the next token of pszText into pszToken and returns the
// position after it, NULL at the end of the text. Skips white space (and bytes >= 0x80) and
// // and /* */ comments. A token is a "quoted string" (without the quotes), one of the
// characters { } ( ) ' : , - or a run of other characters.
char* Cfg_ReadNextToken(const char* pszText, char* pszToken);

// Cfg_InitDefaultsFromFlags_40BCF0: sets the engine parameters that depend on the engine flags
// (BSNET_FLAG_*; 0x20 sizes the file cache from the physical memory). Not called in
// GatewayServer.
void Cfg_InitDefaultsFromFlags(BSNET_INIT_PARAM* pParam, DWORD dwFlags);

// The debug option names accepted in the debug_option_* blocks and their BSDBG_* bits
// (0x4F81E0).
struct SERVER_DEBUG_OPTION
{
    const char* pszName;
    DWORD       dwFlag;
};

extern const SERVER_DEBUG_OPTION g_aServerDebugOption[9];
