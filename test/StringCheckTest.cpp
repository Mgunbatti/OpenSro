// CStringCheck (August 2011 library): the filter file (UTF-16 with BOM) from a temporary file,
// the allowed character table, forbidden parts and words, and the tokenizer.
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "BSLib/Util/StringCheck.h"
#include "TestCheck.h"

static std::string WriteFilterFile()
{
    char szDir[MAX_PATH];
    GetTempPathA(MAX_PATH, szDir);
    std::string strPath = std::string(szDir) + "StringCheckTest.txt";
    const char* pszText =
        "// filter test\r\n"
        "#allow_id_table\t97\t122\r\n"
        "#ALLOW_ID_TABLE\t0x30\t0x39\r\n"
        "GM\t1\t0\t0\t0\r\n"
        "admin\t2\t1\t0\t0\r\n"
        "/* block\r\n"
        "comment */\r\n"
        "noob\t0\t0\t2\t0\r\n";
    WCHAR wsz[512];
    wsz[0] = 0xFEFF;
    int n = MultiByteToWideChar(CP_ACP, 0, pszText, -1, wsz + 1, 511);
    HANDLE h = CreateFileA(strPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD dw;
    // Without the terminator.
    WriteFile(h, wsz, (DWORD)(n * sizeof(WCHAR)), &dw, NULL);
    CloseHandle(h);
    return strPath;
}

static void TestTokenizer()
{
    char sz[] = "  -42\t0x1F\t+7 word";
    CStringProcess line;
    line.m_pszLine = sz;
    line.m_nLen = (int)strlen(sz);
    CHECK(line.ReadInt() == -42);
    CHECK(line.ReadInt() == 0x1F);
    CHECK(line.ReadInt() == 7);
    CHECK(line.ReadInt() == 0);
    STRING_SEPARATORS sep;
    sep.chLast = 0;
    sep.nCount = 0;
    CHECK(StringSeparators_Add(&sep, ' '));
    CHECK(!StringSeparators_Add(&sep, ' '));
    line.SkipBlanks();
    std::string str;
    line.ReadToken(&str, &sep);
    CHECK(str == "word");

    std::string strLower;
    String_ToLowerMbcs("AbC1", &strLower);
    CHECK(strLower == "abc1");
}

static void TestFilter()
{
    std::string strPath = WriteFilterFile();
    CStringCheck check;
    CHECK(check.LoadFromFile("Z:\\no such dir\\no file.txt") == FALSE);
    CHECK(check.LoadFromFile(strPath.c_str()) == TRUE);
    // a..z and 0..9 allowed (the table holds the forbidden bits).
    CHECK((check.m_byCharTable['a' / 8] & (1 << ('a' % 8))) == 0);
    CHECK((check.m_byCharTable['_' / 8] & (1 << ('_' % 8))) != 0);
    CHECK(check.m_mapForbiddenWord[0].size() == 1);
    CHECK(check.m_mapForbiddenWord[1].size() == 1);
    CHECK(check.m_listForbiddenPart[0].size() == 1);
    CHECK(check.m_listForbiddenPart[2].size() == 1);

    CHECK(check.IsAllowedName("hello1"));
    CHECK(check.IsAllowedName("Hello"));
    CHECK(check.IsAllowedName("gm2"));
    CHECK(check.IsAllowedName("noob"));         // third column only
    CHECK(!check.IsAllowedName("he_llo"));
    CHECK(!check.IsAllowedName("superadmin"));
    CHECK(!check.IsAllowedName("SuperADMIN"));
    CHECK(!check.IsAllowedName("gm"));
    CHECK(check.IsAllowedName("comment"));      // inside the block comment
    DeleteFileA(strPath.c_str());
}

int main()
{
    TestTokenizer();
    TestFilter();
    return TEST_RESULT("StringCheckTest");
}
