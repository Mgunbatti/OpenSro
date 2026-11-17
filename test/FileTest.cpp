#include "BSLib/file/FileSystem.h"
#include "BSLib/file/FileLegacy.h"
#include "BSLib/file/FileMM.h"
#include "BSLib/file/FileMem.h"
#include "BSNet/NetEngine.h"
#include "BSNet/NetEngineLog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "TestCheck.h"

static char g_szLastLog[1024];
static DWORD g_dwLastLogFlags;
static int g_nLogCount;

static int CaptureLog(DWORD dwFlags, const char* pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    _vsnprintf_s(g_szLastLog, sizeof(g_szLastLog), _TRUNCATE, pszFormat, args);
    va_end(args);
    g_dwLastLogFlags = dwFlags;
    ++g_nLogCount;
    return 0;
}

static std::string g_strDir;

static std::string TempPath(const char* pszName)
{
    return g_strDir + pszName;
}

static void WriteDiskFile(const std::string& strPath, const void* pData, DWORD dwSize)
{
    HANDLE h = CreateFileA(strPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD dw = 0;
    if (dwSize != 0)
        WriteFile(h, pData, dwSize, &dw, NULL);
    CloseHandle(h);
}

// Returns the file size, -1 if missing; copies up to dwMax bytes to pData.
static int ReadDiskFile(const std::string& strPath, void* pData, DWORD dwMax)
{
    HANDLE h = CreateFileA(strPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    DWORD dwSize = GetFileSize(h, NULL);
    DWORD dw = 0;
    if (pData != NULL && dwSize != 0)
        ReadFile(h, pData, dwSize < dwMax ? dwSize : dwMax, &dw, NULL);
    CloseHandle(h);
    return (int)dwSize;
}

static void TestLowerCase()
{
    std::string str = "stale";
    StringToLowerMbcs("C:\\Dir\\ABC.TXT", str);
    CHECK(str == "c:\\dir\\abc.txt");

    // A lead byte keeps the following byte unchanged, even an upper-case letter.
    StringToLowerMbcs("X\xB0" "AY", str);
    CHECK(str == "x\xB0" "Ay");

    StringToLowerMbcs("", str);
    CHECK(str.empty());
}

static void TestLegacy()
{
    std::string strPath = TempPath("Legacy.bin");
    std::string strMissing = TempPath("missing.bin");
    char buf[8192];

    // Missing file in read mode.
    {
        CFileLegacy file;
        CHECK(file.GetType() == FILE_TYPE_LEGACY);
        CHECK(file.m_nKind == 1);
        CHECK(!file.IsOpen());
        CHECK(file.GetHandle() == INVALID_HANDLE_VALUE);
        CHECK(file.Open(strMissing, 0, 1) == FILE_ERR_CREATE_FILE);
        CHECK(!file.IsOpen());
        CHECK(file.GetUserCount() == 0);
    }
    CHECK(ReadDiskFile(strMissing, NULL, 0) == -1);

    // Write mode: the file is created with the requested size.
    {
        CFileLegacy file;
        CHECK(file.Open(strPath, 10, 5) == 0);
        CHECK(file.IsOpen());
        CHECK(file.GetMode() == 1);
        CHECK(file.GetSize() == 10);
        std::string strLower;
        StringToLowerMbcs(strPath.c_str(), strLower);
        CHECK(file.GetPath() == strLower);
        CHECK(ReadDiskFile(strPath, NULL, 0) == 10);

        CHECK(file.Write(5, "hello", 5) == 5);
        CHECK(file.GetPosition(5) == 5);
        CHECK(file.Write(5, "world!!", 7) == 5);   // clamped to the size
        CHECK(file.GetPosition(5) == 10);

        g_nLogCount = 0;
        CHECK(file.Write(5, "x", 1) == 0);
        CHECK(g_nLogCount == 1);
        CHECK(strstr(g_szLastLog, "RemainBytes == 0") != NULL);
        CHECK(strstr(g_szLastLog, ": 1)") != NULL);

        CHECK(file.Write(5, "x", 0) == 0);
        CHECK(file.Write(99, "x", 1) == 0);
        CHECK(strstr(g_szLastLog, "IFile::Write() - User Context") != NULL);

        // A file open for writing is not shared.
        CHECK(file.Open(strPath, 10, 6) == FILE_ERR_WRITE_SHARED);
        CHECK(file.Flush());
        CHECK(file.ReleaseHandle(5) == 0);
        file.Close();
        CHECK(!file.IsOpen());
        CHECK(file.GetMode() == -1);
        CHECK(file.m_nKind == -1);
        CHECK(file.GetPath().empty());
    }
    CHECK(ReadDiskFile(strPath, buf, sizeof(buf)) == 10);
    CHECK(memcmp(buf, "helloworld", 10) == 0);

    // Read mode.
    {
        CFileLegacy file;
        CHECK(file.Open(strPath, 0, 7) == 0);
        CHECK(file.GetMode() == 0);
        CHECK(file.GetSize() == 10);
        CHECK(file.GetUserCount() == 1);
        CHECK(file.Open(strPath, 0, 7) == FILE_ERR_ALREADY_OPENED_BY_USER);
        CHECK(file.Open(strPath, 0, 8) == 0);
        CHECK(file.GetUserCount() == 2);
        CHECK(file.Open(strPath, 10, 9) == FILE_ERR_MODE_MISMATCH);
        CHECK(file.m_nOpenCount == 2);

        memset(buf, 0, sizeof(buf));
        CHECK(file.Read(7, buf) == 10);
        CHECK(memcmp(buf, "helloworld", 10) == 0);
        CHECK(file.GetPosition(7) == 10);
        CHECK(file.GetPosition(8) == 0);

        g_nLogCount = 0;
        CHECK(file.Read(7, buf) == 0);
        CHECK(g_nLogCount == 1);
        CHECK(strstr(g_szLastLog, ": 4090)") != NULL);   // FILE-003

        // Each context reads independently.
        memset(buf, 0, sizeof(buf));
        CHECK(file.Read(8, buf) == 10);
        CHECK(memcmp(buf, "helloworld", 10) == 0);

        CHECK(file.Read(42, buf) == 0);
        CHECK(strstr(g_szLastLog, "IFile::Read() - User Context") != NULL);
        CHECK(g_dwLastLogFlags == 0x2000000);

        CHECK(file.Seek(7) == 0);
        CHECK(file.Seek(42) == FILE_ERR_UNKNOWN_USER);
        CHECK(strstr(g_szLastLog, "IFile::Seek() - User Context") != NULL);

        CHECK(file.ReleaseHandle(7) == 1);
        CHECK(file.ReleaseHandle(7) == 1);
        CHECK(file.GetPosition(7) == 0);
        CHECK(file.ReleaseHandle(8) == 0);
        CHECK(file.IsOpen());   // users gone, handle still open
    }

    // Empty file.
    std::string strEmpty = TempPath("empty.bin");
    WriteDiskFile(strEmpty, NULL, 0);
    {
        CFileLegacy file;
        CHECK(file.Open(strEmpty, 0, 1) == FILE_ERR_EMPTY_FILE);
        CHECK(file.IsOpen());   // left open until Close
        CHECK(file.GetUserCount() == 0);
    }
    CHECK(DeleteFileA(strEmpty.c_str()));

    // Blocks of at most 4090 bytes.
    std::string strBig = TempPath("big.bin");
    static BYTE s_big[5000];
    for (int i = 0; i < (int)sizeof(s_big); ++i)
        s_big[i] = (BYTE)(i * 7);
    WriteDiskFile(strBig, s_big, sizeof(s_big));
    {
        CFileLegacy file;
        CHECK(file.Open(strBig, 0, 1) == 0);
        CHECK(file.GetSize() == 5000);
        CHECK(file.Read(1, buf) == 4090);
        CHECK(memcmp(buf, s_big, 4090) == 0);
        CHECK(file.Read(1, buf) == 910);
        CHECK(memcmp(buf, s_big + 4090, 910) == 0);
        CHECK(file.Read(1, buf) == 0);
    }
    CHECK(DeleteFileA(strBig.c_str()));
    CHECK(DeleteFileA(strPath.c_str()));
}

static void TestMapped()
{
    std::string strPath = TempPath("mapped.bin");
    char buf[64];

    {
        CFileMM file;
        CHECK(file.GetType() == FILE_TYPE_MAPPED);
        CHECK(!file.IsOpen());
        CHECK(file.GetMemory() == NULL);
        CHECK(file.Open(strPath, 8, 1) == 0);
        CHECK(file.IsOpen());
        CHECK(file.GetMemory() != NULL);
        CHECK(file.GetHandle() != INVALID_HANDLE_VALUE);
        CHECK(file.Write(1, "ABCDEFGHIJ", 10) == 8);
        CHECK(memcmp(file.GetMemory(), "ABCDEFGH", 8) == 0);
        CHECK(file.Flush());
        file.Close();
        CHECK(!file.IsOpen());
        CHECK(file.GetMemory() == NULL);
    }
    CHECK(ReadDiskFile(strPath, buf, sizeof(buf)) == 8);
    CHECK(memcmp(buf, "ABCDEFGH", 8) == 0);

    {
        CFileMM file;
        CHECK(file.Open(strPath, 0, 3) == 0);
        CHECK(file.GetSize() == 8);
        memset(buf, 0, sizeof(buf));
        CHECK(file.Read(3, buf) == 8);
        CHECK(memcmp(buf, "ABCDEFGH", 8) == 0);
        CHECK(file.Open(strPath, 0, 4) == 0);
        CHECK(file.GetUserCount() == 2);
    }

    // FILE-002: a missing file opened for reading is created empty.
    std::string strMissing = TempPath("mm_missing.bin");
    {
        CFileMM file;
        CHECK(file.Open(strMissing, 0, 1) == FILE_ERR_EMPTY_FILE);
    }
    CHECK(ReadDiskFile(strMissing, NULL, 0) == 0);
    CHECK(DeleteFileA(strMissing.c_str()));

    // Bad path.
    {
        CFileMM file;
        CHECK(file.Open(TempPath("no\\such\\dir.bin"), 0, 1) == FILE_ERR_CREATE_FILE);
    }

    CHECK(DeleteFileA(strPath.c_str()));
}

static void TestMemory()
{
    char buf[32];
    CFileMem file;
    CHECK(file.GetType() == FILE_TYPE_MEMORY);
    CHECK(file.m_nKind == 0);
    CHECK(!file.IsOpen());
    CHECK(file.GetHandle() == NULL);
    CHECK(file.Flush());

    // Context 0: no buffer.
    CHECK(file.Open("Mem", 16, 0) == 0);
    CHECK(!file.IsOpen());
    CHECK(file.GetUserCount() == 1);
    CHECK(file.GetPath() == "mem");
    file.Close();

    CHECK(file.Open("mem", 16, 1) == 0);
    CHECK(file.IsOpen());
    CHECK(file.GetSize() == 16);
    CHECK(file.Write(1, "0123456789abcdefXYZ", 19) == 16);
    CHECK(memcmp(file.GetMemory(), "0123456789abcdef", 16) == 0);

    void* pOld = file.GetMemory();
    static char s_attach[4] = { 'w', 'x', 'y', 'z' };
    CHECK(file.AttachMemory(s_attach, 4) == pOld);
    CHECK(file.GetMemory() == s_attach);
    CHECK(file.GetSize() == 4);
    CHECK(file.AttachMemory(pOld, 16) == s_attach);
    file.ReleaseHandle(1);

    // Reading back through a second context.
    CHECK(file.Open("mem", 16, 2) == FILE_ERR_WRITE_SHARED);
    file.Close();
    CHECK(!file.IsOpen());

    CFileMem reader;
    CHECK(reader.Open("r", 8, 0) == 0);
    static char s_data[8] = { 'r', 'e', 'a', 'd', 'b', 'a', 'c', 'k' };
    reader.Close();
    CHECK(reader.Open("r", 0, 0) == 0);
    reader.AttachMemory(new BYTE[8], 8);
    memcpy(reader.GetMemory(), s_data, 8);
    CHECK(reader.Read(0, buf) == 8);
    CHECK(memcmp(buf, s_data, 8) == 0);
}

static void TestFileSystem()
{
    CFileSystem* pFS = CFileSystem::GetInstance();
    CHECK(pFS != NULL);
    CHECK(pFS->m_dwCacheLimit == 10000000);
    CHECK(!pFS->m_bInitialized);
    CHECK(pFS->Initialize(0));
    CHECK(pFS->m_bInitialized);
    CHECK(pFS->m_IDPool.m_dwGrowBy == 5000);
    CHECK(pFS->m_IDPool.m_deqFree.size() == 5000);   // ids 1..5000

    char buf[64];
    std::string strDisk = TempPath("fs.bin");
    WriteDiskFile(strDisk, "filesystem", 10);

    // Type -1 does nothing.
    DWORD dwHandle = 12345;
    std::string strPath = TempPath("FS.BIN");
    CHECK(pFS->OpenFile(&dwHandle, -1, strPath, 0, 1) == 0);
    CHECK(dwHandle == 12345);
    CHECK(strPath == TempPath("FS.BIN"));

    // Unknown type.
    g_nLogCount = 0;
    CHECK(pFS->OpenFile(&dwHandle, 3, strPath, 0, 1) == FILE_ERR_CREATE_OBJECT);
    CHECK(dwHandle == 0);
    CHECK(g_nLogCount == 1);
    CHECK(strstr(g_szLastLog, "CFileSystem::_CreateFile()") != NULL);

    // The path is lower-cased in place.
    std::string strLower;
    StringToLowerMbcs(TempPath("FS.BIN").c_str(), strLower);
    CHECK(strPath == strLower);

    // Legacy, no cache: the entry goes away with its last user.
    strPath = TempPath("FS.BIN");
    CHECK(pFS->OpenFile(&dwHandle, FILE_TYPE_LEGACY, strPath, 0, 1) == 0);
    CHECK(dwHandle == 1);
    CHECK(pFS->m_nOpenCount == 1);
    CHECK(pFS->m_nReadOpenCount == 1);
    IFile* pFile = pFS->GetFile(dwHandle);
    CHECK(pFile != NULL && pFile->GetFileHandle() == dwHandle);
    memset(buf, 0, sizeof(buf));
    CHECK(pFile != NULL && pFile->Read(1, buf) == 10);
    CHECK(memcmp(buf, "filesystem", 10) == 0);
    CHECK(pFS->m_mapCachedName.empty());

    // Not cached: a second open gets its own object.
    DWORD dwHandle2 = 0;
    CHECK(pFS->OpenFile(&dwHandle2, FILE_TYPE_LEGACY, strPath, 0, 2) == 0);
    CHECK(dwHandle2 == 2);
    CHECK(pFS->CloseFile(dwHandle2, 2) == 0);

    CHECK(pFS->CloseFile(dwHandle, 1) == 0);
    CHECK(pFS->m_nOpenCount == 0);
    g_nLogCount = 0;
    CHECK(pFS->GetFile(dwHandle) == NULL);
    CHECK(g_nLogCount == 1 && g_dwLastLogFlags == 0x1000000);
    CHECK(strstr(g_szLastLog, "[FileHandle: 1]") != NULL);
    CHECK(pFS->CloseFile(dwHandle, 1) == 0);    // unknown handle

    // Released ids go to the back of the queue.
    CHECK(pFS->m_IDPool.m_deqFree.back() == 1);

    // Open failure: logged with the error code, nothing registered.
    std::string strMissing = TempPath("NOPE.BIN");
    g_nLogCount = 0;
    CHECK(pFS->OpenFile(&dwHandle, FILE_TYPE_LEGACY, strMissing, 0, 1) == FILE_ERR_CREATE_FILE);
    CHECK(g_nLogCount == 1);
    CHECK(strstr(g_szLastLog, "[ErrorCode: -13]") != NULL);
    CHECK(pFS->m_mapOpenFile.empty());

    // Write through the file system.
    std::string strOut = TempPath("Out.bin");
    CHECK(pFS->OpenFile(&dwHandle, FILE_TYPE_LEGACY, strOut, 6, 9) == 0);
    pFile = pFS->GetFile(dwHandle);
    CHECK(pFile != NULL && pFile->Write(9, "output", 6) == 6);
    CHECK(pFS->CloseFile(dwHandle, 9) == 0);
    CHECK(pFS->GetFile(dwHandle) == NULL);
    CHECK(ReadDiskFile(strOut, buf, sizeof(buf)) == 6);
    CHECK(memcmp(buf, "output", 6) == 0);

    // Cache: mapped read-mode files stay open and are found again by path.
    pFS->m_dwCacheLimit = 1000;
    std::string strA = TempPath("a.bin");
    WriteDiskFile(strA, "0123456789", 10);
    strPath = strA;
    DWORD dwA = 0;
    CHECK(pFS->OpenFile(&dwA, FILE_TYPE_MAPPED, strPath, 0, 1) == 0);
    CHECK(pFS->m_dwCacheUsed == 10);
    CHECK(pFS->m_mapCachedName.size() == 1);
    CHECK(pFS->CloseFile(dwA, 1) == 0);
    CHECK(pFS->GetFile(dwA) != NULL);

    DWORD dwAgain = 0;
    strPath = strA;
    CHECK(pFS->OpenFile(&dwAgain, FILE_TYPE_MAPPED, strPath, 0, 2) == 0);
    CHECK(dwAgain == dwA);
    CHECK(pFS->m_dwCacheUsed == 10);
    memset(buf, 0, sizeof(buf));
    CHECK(pFS->GetFile(dwA)->Read(2, buf) == 10);
    CHECK(memcmp(buf, "0123456789", 10) == 0);

    // Same path, other implementation.
    strPath = strA;
    g_nLogCount = 0;
    CHECK(pFS->OpenFile(&dwHandle, FILE_TYPE_LEGACY, strPath, 0, 3) == FILE_ERR_TYPE_MISMATCH);
    CHECK(g_nLogCount == 1 && g_dwLastLogFlags == 0x1000000);
    CHECK(strstr(g_szLastLog, "[FileType: 2, 1]") != NULL);
    CHECK(pFS->CloseFile(dwA, 2) == 0);

    // A second file over the limit purges the idle one.
    std::string strB = TempPath("b.bin");
    static BYTE s_b[995];
    memset(s_b, 'b', sizeof(s_b));
    WriteDiskFile(strB, s_b, sizeof(s_b));
    strPath = strB;
    DWORD dwB = 0;
    CHECK(pFS->OpenFile(&dwB, FILE_TYPE_MAPPED, strPath, 0, 1) == 0);
    CHECK(pFS->m_dwCacheUsed == 995);
    g_nLogCount = 0;
    CHECK(pFS->GetFile(dwA) == NULL);
    CHECK(pFS->GetFile(dwB) != NULL);
    CHECK(pFS->m_mapCachedName.size() == 1);
    CHECK(pFS->m_mapCachedName.find(strPath) != pFS->m_mapCachedName.end());

    // Cleanup deletes every object, including ones still in use.
    pFS->Cleanup();
    CHECK(pFS->m_mapOpenFile.empty());
    CHECK(pFS->m_mapCachedName.empty());
    CHECK(pFS->m_dwCacheUsed == 0 && pFS->m_nOpenCount == 0 && !pFS->m_bInitialized);

    CHECK(DeleteFileA(strA.c_str()));
    CHECK(DeleteFileA(strB.c_str()));
    CHECK(DeleteFileA(strOut.c_str()));
    CHECK(DeleteFileA(strDisk.c_str()));
}

static void TestIDPool()
{
    CIDPool pool;
    CHECK(pool.m_dwGrowBy == 5000 && pool.m_dwNextID == 1);
    pool.m_dwGrowBy = 10;
    for (DWORD i = 1; i <= 8; ++i)
        CHECK(pool.Alloc() == i);
    CHECK(pool.m_deqFree.size() == 2);
    CHECK(pool.m_dwNextID == 11);
    // 2 free ids are fewer than 10 * 0.2f (computed as 2.0000000298), so the pool grows.
    CHECK(pool.Alloc() == 9);
    CHECK(pool.m_deqFree.size() == 11);
    CHECK(pool.m_dwNextID == 21);
    CHECK(pool.m_deqFree.back() == 20);
    pool.Free(3);
    CHECK(pool.m_deqFree.back() == 3);

    // Id 0 is never queued.
    CIDPool zero;
    zero.m_dwNextID = 0;
    zero.m_dwGrowBy = 3;
    zero.Grow();
    CHECK(zero.m_deqFree.size() == 2 && zero.m_deqFree.front() == 1);
    CHECK(zero.m_dwNextID == 3);
}

int main()
{
    char szTemp[MAX_PATH];
    GetTempPathA(MAX_PATH, szTemp);
    char szDir[MAX_PATH];
    sprintf_s(szDir, "%sFileTest_%lu\\", szTemp, GetCurrentProcessId());
    CreateDirectoryA(szDir, NULL);
    g_strDir = szDir;

    // NetEngineLog writes to the engine instance's callback.
    CNetEngine engine;
    engine.m_Config.pfnLog = CaptureLog;

    TestLowerCase();
    TestIDPool();
    TestLegacy();
    TestMapped();
    TestMemory();
    TestFileSystem();

    engine.m_Config.pfnLog = NULL;
    CHECK(RemoveDirectoryA(szDir));

    return TEST_RESULT("FileTest");
}
