#pragma once

#include <winsock2.h>
#include <windows.h>
#include <map>
#include <string>
#include <vector>

// The IP country table of the GlobalManager slave (CGlobalManagerSlave::LoadRIRFiles): a
// country code file (Server.cfg "CountryCodeFile": first line the number of lines that follow,
// then "<code>\t<name>" lines) and the delegation files of the regional internet registries
// (Server.cfg "RIRFileNum", "RIRFile1".."RIRFile<n>"):
//
//     # comment lines
//     2|apnic|20110101|...                         version line
//     apnic|*|asn|*|<count>|summary                three summary lines (asn, ipv4, ipv6)
//     apnic|*|ipv4|*|<count>|summary
//     apnic|*|ipv6|*|<count>|summary
//     apnic|JP|ipv4|1.0.16.0|4096|20110412|allocated
//
// The records of the IPv4 blocks are kept per /8, /16 or /24 block, keyed by the first address
// in network byte order. All lines end with "\r\n". The files are read whole into memory and
// parsed in place.

#pragma pack(push, 1)
// One block (12 bytes): the summary lines are parsed into the same record.
struct IP_COUNTRY_RECORD
{
    char    szCode[4];      // +0   country code (or the summary type)
    DWORD   dwIP;           // +4   first address, network byte order
    DWORD   dwCount;        // +8   addresses
};
#pragma pack(pop)

C_ASSERT(sizeof(IP_COUNTRY_RECORD) == 12);

typedef std::map<DWORD, IP_COUNTRY_RECORD*> IPCountryBlockMap;
typedef std::map<std::string, std::string> CountryNameMap;

// The table (0x1060 bytes, operator new by GetInstance, pointer 0x558C9C). No constructor or
// destructor of its own (0x45C220 / 0x45C2A0 are the members' ones); the blocks are not freed
// by the destructor.
class CIPCountryTable
{
public:
    // IPCountryTable_GetInstance (0x45C190): the table, created on the first call.
    static CIPCountryTable* GetInstance();

    // sub_461930: forgets the buffers, handles, sizes and counts and empties the maps (the
    // blocks are not freed), the summary types and the line buffer.
    void Reset();
    // sub_461A20: frees the blocks and empties the maps and the summary types.
    void Clear();
    // sub_461B30: reads the country code file; 0 when it cannot be read. A count below 240
    // shows "Probably You have Invalid CountryCodeFile..".
    int LoadCountryCodeFile(const char* pszPath);
    // sub_461BA0: LoadRIRFile and "File Loading Time : (%s) %d tick\r\n".
    int LoadRIRFileTimed(const char* pszPath);
    // sub_461C00: reads a delegation file; 0 when it cannot be read.
    int LoadRIRFile(const char* pszPath);
    // sub_461E50: frees the file buffers, closes the handles and empties the summary types.
    void CloseFile();
    // sub_462860: the code of the block of the address (network byte order) into pszOut ("**"
    // when there is none); the /24, /16 and /8 block are tried.
    void FindCountryCode(char* pszOut, DWORD dwIP);
    // sub_4628A0: the name of the country code ("**" when unknown).
    void GetCountryName(std::string strCode, std::string* pstrOut);

    // IPTable_OpenCountryCodeFile_461BE0 / IPTable_OpenRIRFile_461FF0: the file into its buffer
    // (ReadWholeFile).
    int OpenCountryCodeFile(const char* pszPath);
    int OpenRIRFile(const char* pszPath);
    // sub_462010: the next line from the cursor into m_szLine (with bSkip the cursor first
    // moves nSkip characters on); NULL when no "\r\n" follows.
    char* NextLine(int bSkip, int nSkip);
    // sub_462080: skips the comment lines and the version line and parses the three summary
    // lines.
    void ParseRIRHeader();
    // sub_462110: a summary record: the count of its type and the type in m_vecSummaryType.
    void ParseRIRSummary(const IP_COUNTRY_RECORD* pRecord);
    // sub_462270: a "<code>\t<name>" line.
    void ParseCountryLine(const char* pszLine);
    // sub_462470: a "|"-separated line into pRecord (a local record when NULL) and the blocks
    // of its addresses into m_mapBlock.
    void ParseRIRRecord(const char* pszLine, IP_COUNTRY_RECORD* pRecord);
    // sub_462980: the block at or after dwKey when it holds the address; pszOut gets its code
    // (1) or "**" (0).
    int FindInBlock(DWORD dwKey, DWORD dwIP, char* pszOut);

    // sub_4618A0: copies the text before the next pszDelimiter into pszOut and returns the
    // position after the delimiter. Without one *pbEnd is set and pszText is returned (pszOut
    // is not written).
    static const char* NextToken(const char* pszDelimiter, const char* pszText, char* pszOut,
                                 DWORD* pbEnd);
    // sub_461EE0: the whole file into a new buffer of its size (not terminated); the handle is
    // closed again. 0 when it cannot be opened.
    static int ReadWholeFile(char** ppBuffer, HANDLE* phFile, const char* pszPath,
                             LARGE_INTEGER* pliSize);
    // IPTable_IsClassfulBlock_462730: 256, 65536 or 16777216 addresses.
    static BOOL IsClassfulBlock(DWORD dwCount);
    // sub_462750: "a.b.c.d" in network byte order.
    static void ParseDottedIP(const char* pszText, DWORD* pdwIP);

public:
    char*               m_pRIRBuffer;           // +0000
    char*               m_pCountryBuffer;       // +0004
    char*               m_pCursor;              // +0008
    char                m_szLine[0x1000];       // +000C
    HANDLE              m_hRIRFile;             // +100C
    HANDLE              m_hCountryFile;         // +1010
    DWORD               m_dw1014;               // +1014
    LARGE_INTEGER       m_liRIRSize;            // +1018
    LARGE_INTEGER       m_liCountrySize;        // +1020
    int                 m_nASNCount;            // +1028
    int                 m_nIPv4Count;           // +102C
    int                 m_nIPv6Count;           // +1030
    IPCountryBlockMap   m_mapBlock;             // +1034
    CountryNameMap      m_mapCountryName;       // +1040
    std::vector<std::string> m_vecSummaryType;  // +104C
    DWORD               m_dw105C;               // +105C
};

C_ASSERT(FIELD_OFFSET(CIPCountryTable, m_szLine) == 0xC);
C_ASSERT(FIELD_OFFSET(CIPCountryTable, m_hRIRFile) == 0x100C);
C_ASSERT(FIELD_OFFSET(CIPCountryTable, m_liRIRSize) == 0x1018);
C_ASSERT(FIELD_OFFSET(CIPCountryTable, m_nASNCount) == 0x1028);
C_ASSERT(FIELD_OFFSET(CIPCountryTable, m_mapBlock) == 0x1034);
C_ASSERT(FIELD_OFFSET(CIPCountryTable, m_mapCountryName) == 0x1040);
C_ASSERT(FIELD_OFFSET(CIPCountryTable, m_vecSummaryType) == 0x104C);
C_ASSERT(sizeof(CIPCountryTable) == 0x1060);

// 0x558C9C
extern CIPCountryTable* g_pIPCountryTable;
