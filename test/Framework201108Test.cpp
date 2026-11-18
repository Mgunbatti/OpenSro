// The August 2011 framework of SR_ShardManager (JMX_FRAMEWORK_2011_08, the "August 2011
// (SR_ShardManager)" table of JMX_ServerFramework\FRAMEWORK_2011.md). The test is compiled with
// SHARD_DEFS and links the August builds of the libraries (SHARD_LIBS); it checks the behaviour
// that differs from the July and January 2011 code Framework2011Test and Framework201101Test
// check.

#include "BSLib/ChunkAllocator.h"
#include "ServerFramework/CertArch.h"

#include <string.h>

#include "TestCheck.h"

#ifndef JMX_FRAMEWORK_2011_08
#error Framework201108Test is compiled with JMX_FRAMEWORK_2011_08 (see SHARD_DEFS in the Makefile)
#endif

// ---------------------------------------------------------------------------------------------
// build

struct POOL_ITEM
{
    DWORD dw;
};

static void TestBuild()
{
    // The exe has the single module string "SR_ShardManager".
    CHECK(strcmp(SERVER_MODULE_NAME, "SR_ShardManager") == 0);
    // The July 2011 pools (named, 0x5C / 0x98 bytes).
    CChunkAllocatorST<POOL_ITEM> st(NULL, 0, 1000, "pool");
    CHECK(st.m_strName == "pool" && st.m_nItemsPerBlock == 1000);
    C_ASSERT(sizeof(CChunkAllocatorST<POOL_ITEM>) == 0x5C);
    C_ASSERT(sizeof(CChunkAllocatorMT<POOL_ITEM>) == 0x98);
}

int main()
{
    TestBuild();
    return TEST_RESULT("Framework201108Test");
}
