#pragma once

#include <windows.h>

#include <hash_map>
#include <list>
#include <string>

// Name filter of the August 2011 library (SR_ShardManager 0x6df010..0x6e1df0): a table of the
// characters allowed in names (the "#allow_id_table" lines of a UTF-16 filter file) and per
// column of the file a list of forbidden parts and a set of forbidden words. The text is read
// with a small line and token reader (CStringProcess).

// Separator set of CStringProcess::ReadToken (0xD0 bytes).
struct STRING_SEPARATORS
{
    char    chLast;         // +00 the separator ReadToken stopped at
    char    szChar[100];    // +01
    BYTE    byInclude[100]; // +65 nonzero: the separator ends the token and belongs to it
    int     nCount;         // +CC
};

C_ASSERT(sizeof(STRING_SEPARATORS) == 0xD0);

// Adds ch unless the set is full or has it already (then chLast = ch); FALSE in both cases.
bool StringSeparators_Add(STRING_SEPARATORS* pSeparators, char ch);            // 0x6e1760

// A line of text with a read position (vftable 0x79ad94).
class CStringProcess
{
public:
    CStringProcess() : m_pszLine(NULL), m_nPos(0), m_nLen(0) {}                 // 0x6e1810
    virtual ~CStringProcess() {}                                                // 0

    // Steps over blanks (space and 1..0x10); TRUE when it moved.
    bool SkipBlanks();                                                          // 0x6e1880
    // A number follows the blanks (an optional sign, then a digit); the position is kept.
    bool IsNumberAhead();                                                       // 0x6e18b0
    // A decimal or 0x hexadecimal number with an optional sign; 0 when none follows.
    int ReadInt();                                                              // 0x6e1970
    // Characters up to a separator (or the end) into *pstrToken when it is not NULL.
    BOOL ReadToken(std::string* pstrToken, STRING_SEPARATORS* pSeparators);     // 0x6e1ac0
    // Steps over nCount tab-separated columns (inline in CStringCheck::LoadFromFile).
    void SkipColumns(int nCount);

    static bool IsBlank(char ch);                                               // 0x6e1860
    static BOOL IsDigit(char ch, bool bHex);                                    // 0x6e1930

public:
    char*   m_pszLine;      // +04
    int     m_nPos;         // +08
    int     m_nLen;         // +0C
    bool    m_bPositive;    // +10 sign of the last ReadInt
};

C_ASSERT(sizeof(CStringProcess) == 0x14);

// Splits a text buffer into lines in place (no vftable; the name is not known).
class CStringLineReader
{
public:
    CStringLineReader();                                                        // 0x6e17a0

    // The buffer (nSize characters plus a terminator) is changed while reading.
    void Attach(char* pszText, int nSize);
    // The next line with "//" comments and "/* */" blocks removed; NULL at the end.
    CStringProcess* ReadLine();                                                 // 0x6e1be0
    // The next line with something on it.
    CStringProcess* NextNonEmptyLine();                                         // 0x6e1df0

private:
    char* NextLineEnd(char* psz);                                               // 0x6e1b70

public:
    char*           m_pszText;          // +00
    char*           m_pszCursor;        // +04
    int             m_nSize;            // +08
    int             m_nRemain;          // +0C
    DWORD           m_dw10;             // +10
    CStringProcess  m_Line;             // +14
    bool            m_bEnd;             // +28
    bool            m_bInComment;       // +29 inside "/* */"
    int             m_nLine;            // +2C
    bool            m_bCommentAtStart;  // +30 "//" only before anything else on the line
    BOOL            m_bSingleBreaks;    // +34 a lone CR or LF ends a line, CRLF / LFCR is a tab
    BOOL            m_bSkipBlanks;      // +38 NextNonEmptyLine ignores leading blanks
};

C_ASSERT(FIELD_OFFSET(CStringLineReader, m_Line) == 0x14);
C_ASSERT(FIELD_OFFSET(CStringLineReader, m_bEnd) == 0x28);
C_ASSERT(sizeof(CStringLineReader) == 0x3C);

// Lower-cases A..Z of a multibyte string into *pstrOut, double-byte characters as they are.
void String_ToLowerMbcs(const char* psz, std::string* pstrOut);                // 0x6df010

// CStringCheck (vftable 0x79ad8c).
class CStringCheck
{
public:
    // The words of a name split at spaces.
    class CWordList
    {
    public:
        CWordList() { Clear(); }                                                // 0x6df080
        void Clear();                                                           // 0x6df0e0

    public:
        int                     m_nCount;       // +0
        std::list<std::string>  m_listWord;     // +4
    };

    CStringCheck();                                                             // 0x6df100
    virtual ~CStringCheck();                                                    // 0 0x6df2d0

    // Clears the word list, the first two part lists and the first two word sets.
    void Reset();                                                               // 0x6df400
    // Reads the filter file (UTF-16 with a BOM, converted to the ANSI code page). A line is
    // "#allow_id_table <first> <last>" (allowed character codes) or "<word> <c1> <c2> <c3> <c4>"
    // (1: the word is forbidden, 2: names may not contain it, per column); m_nLanguage
    // skips 6 columns per language first. FALSE when the file cannot be opened.
    BOOL LoadFromFile(const char* pszFile);                                     // 0x6dfa00
    // Allowed characters only, no forbidden part, no forbidden word (first column).
    bool IsAllowedName(const std::string& strName);                             // 0x6df4c0

    BOOL AreCharsAllowed(const char* psz);                                      // 0x6df7f0
    bool ContainsForbiddenPart(std::list<std::string>& listPart,
                               const std::string& strName);                     // 0x6df850
    bool ContainsForbiddenWord(const std::string& strName,
                               stdext::hash_map<std::string, bool>& mapWord);   // 0x6df970
    // Clears m_WordList and splits the text into it.
    CWordList* SetWords(const std::string& strText);                            // 0x6df620
    // The words of psz into *pList (m_WordList for NULL).
    BOOL ParseWords(const char* psz, CWordList* pList);                         // 0x6df660

public:
    CWordList                           m_WordList;                 // +0004
    int                                 m_nLanguage;                // +0014
    BYTE                                m_byCharTable[0x2000];      // +0018 set = not allowed
    std::list<std::string>              m_listForbiddenPart[4];     // +2018
    stdext::hash_map<std::string, bool> m_mapForbiddenWord[4];      // +2048
};

C_ASSERT(sizeof(CStringCheck::CWordList) == 0x10);
C_ASSERT(FIELD_OFFSET(CStringCheck, m_byCharTable) == 0x18);
C_ASSERT(FIELD_OFFSET(CStringCheck, m_listForbiddenPart) == 0x2018);
C_ASSERT(FIELD_OFFSET(CStringCheck, m_mapForbiddenWord) == 0x2048);
C_ASSERT(sizeof(CStringCheck) == 0x20E8);
