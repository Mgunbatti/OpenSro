#pragma once

#include <map>
#include <string>
#include <vector>
#include <hash_map>

#include "BSLib/DB/Database.h"
#include "ServerCommon/RefRecords.h"

// The reference data of SR_ShardManager (ServerCommon\ReferenceData.cpp and
// RefInstanceGenerator.cpp of the original; both names belong to the one static object 0x89b020,
// g_pReferenceData = dword_8a8944).
//
// CShardDB::RegisterRefTables reads every _Ref table into a table entry (CDBTable + list of
// CDBRecStream rows) and calls BuildAllTables, which builds the containers of this object from
// them, table by table in slot order. A row is parsed by a reader (RefRow_Read overloads) from
// the stream buffer of its record; the containers own the parsed rows.

// ---------------------------------------------------------------------------------------------
// Table entries

// One registered _Ref table: the table and the rows CShardDB selected (records of the table's
// record class). The empty-table check of the builders reads m_listRows' size at +0xC.
struct RefTableEntry
{
    CDBTable*       m_pTable;       // +0
    CDBRecordList   m_listRows;     // +4
};

C_ASSERT(sizeof(RefTableEntry) == 16);

// The table vector of CShardDB (index = slot, NULL for a table that was not registered).
typedef std::vector<RefTableEntry*> RefTableVector;

enum
{
    REF_TABLE_SLOT_COUNT = 124,
};

// ---------------------------------------------------------------------------------------------
// Row reader

// Reads the columns of a CDBRecStream row one after the other (m_nReadPos, m_nFieldCount).
// A scalar past the end of the row asserts and is read anyway; a string asserts and terminates
// (see SM-006).
class CRefRow
{
public:
    explicit CRefRow(CDBRecStream* pRow)
        : m_pRow(pRow)
    {
    }

    template <class T>
    CRefRow& operator>>(T& value)
    {
        // BUG(SM-006): the overrun is only asserted; the value is read past the row.
        if ((int)(m_pRow->m_nReadPos + sizeof(T)) > (int)m_pRow->m_nWritePos)
            BSAssertFail();
        value = *(const T*)(m_pRow->m_pBuffer + m_pRow->m_nReadPos);
        m_pRow->m_nReadPos += sizeof(T);
        ++m_pRow->m_nFieldCount;
        return *this;
    }

    // RefDataRow_ReadString (0x4a1e30).
    CRefRow& operator>>(std::string& value);

    // The leading Service column (DWORD) of most tables: FALSE for a row out of service.
    BOOL Service()
    {
        DWORD dwService;
        *this >> dwService;
        return dwService != 0;
    }

    // The ID column: an ID of 0 asserts (the reader then returns 0 and the row is dropped).
    template <class T>
    CRefRow& Id(T& value)
    {
        *this >> value;
        if (!value)
            BSAssertFail();
        return *this;
    }

private:
    CDBRecStream* m_pRow;
};

// ---------------------------------------------------------------------------------------------
// Parsed rows without a struct in RefRecords.h

// _RefGameWorldBindTriggerCategory, _RefTriggerCategoryBindTrigger, _RefTriggerBindEvent /
// Condition / Action, _RefGameWorldBindGameWorldGroup (12 bytes, RefTriggerBind::Read 0x49a180).
struct RefTriggerBind
{
    int              ID;                         // +0
    int              ID1;                        // +4  owner (key of the multimap)
    int              ID2;                        // +8  bound row
};

C_ASSERT(sizeof(RefTriggerBind) == 12);

// _RefTriggerEvent (8 bytes, RefTriggerEvent::Read 0x49a340).
struct RefTriggerEvent
{
    int              ID;                         // +0
    int              Value;                      // +4
};

C_ASSERT(sizeof(RefTriggerEvent) == 8);

// Row readers (the address is the reader of the original). Each returns the row's ID: 0 for a
// row out of service (or with ID 0), which is then dropped.
int RefRow_Read(CRefRow& row, RefUIString& r);                   // 0x499f80
int RefRow_Read(CRefRow& row, RefTriggerCategory& r);            // 0x499ff0
int RefRow_Read(CRefRow& row, RefTrigger& r);                    // 0x49a0a0
int RefRow_Read(CRefRow& row, RefTriggerBind& r);                // 0x49a180
int RefRow_Read(CRefRow& row, RefTriggerCommon& r);              // 0x49a230
int RefRow_Read(CRefRow& row, RefTriggerEvent& r);               // 0x49a340
int RefRow_Read(CRefRow& row, RefTriggerCondition& r);           // 0x49a3c0
int RefRow_Read(CRefRow& row, RefTriggerAction& r);              // 0x49a490
int RefRow_Read(CRefRow& row, RefTriggerVariable& r);            // 0x49a550
int RefRow_Read(CRefRow& row, RefGameWorldGroup& r);             // 0x49a620
// RefKeyValue4Str::Read 0x49a680 (one function for the four tables).
int RefRow_Read(CRefRow& row, RefShardContentConfig& r);
int RefRow_Read(CRefRow& row, RefTriggerConditionParam& r);
int RefRow_Read(CRefRow& row, RefTriggerActionParam& r);
int RefRow_Read(CRefRow& row, RefGameWorldGroupConfig& r);

// Opaque _RefObjCommon row (see InstanceRecords.h).
struct RefObjCommonRow;

// ---------------------------------------------------------------------------------------------
// CReferenceData

class CReferenceData
{
public:
    typedef stdext::hash_map<int, RefObjCommonRow*>             RefObjHashMap;
    typedef std::map<std::string, RefShardContentConfig*>       RefShardContentConfigMap;
    typedef std::map<std::string, RefUIString*>                 RefUIStringMap;
    typedef std::multimap<int, RefTriggerBind*>                 RefTriggerBindMap;
    typedef std::map<int, RefTriggerCategory*>                  RefTriggerCategoryMap;
    typedef std::map<int, RefTrigger*>                          RefTriggerMap;
    typedef std::multimap<int, RefTriggerVariable*>             RefTriggerVariableMap;
    typedef std::map<int, RefTriggerCommon*>                    RefTriggerCommonMap;
    typedef std::map<int, RefTriggerEvent*>                     RefTriggerEventMap;
    typedef std::map<int, RefTriggerCondition*>                 RefTriggerConditionMap;
    typedef std::map<int, RefTriggerAction*>                    RefTriggerActionMap;
    typedef std::map<std::string, RefTriggerConditionParam*>    RefTriggerConditionParamMap;
    typedef std::map<std::string, RefTriggerActionParam*>       RefTriggerActionParamMap;
    typedef std::map<int, RefGameWorldGroup*>                   RefGameWorldGroupMap;
    typedef std::map<std::string, RefGameWorldGroupConfig*>     RefGameWorldGroupConfigMap;

    // Static object of the original (dynamic initializer 0x50a9b0): sets g_pReferenceData.
    CReferenceData();
    // Frees the rows of the containers declared here.
    ~CReferenceData();

    // GetSingleton_CRefInstanceGenerator (0x41c5c0): asserts when there is no object.
    static CReferenceData* GetInstance();

    // CRefInstanceGenerator::BuildAllTables (0x4ad5a0): builds every registered table in slot
    // order. An empty table is logged ("there is no record") and skipped; the first table that
    // fails stops the build with a message box. FALSE then, TRUE otherwise.
    BOOL BuildAllTables();
    // CRefInstanceGenerator::BuildFromTable (0x4ac550): the builder of one slot.
    int BuildFromTable(RefTableEntry* pTable, unsigned int nSlot);

    // RefData_HashFindAt108 (0x42e280): the object row of an id, NULL when there is none.
    RefObjCommonRow* FindRefObj(int nRefObjID);

public:
    RefTableVector*             m_pTables;                          // +000
    BYTE                        m_ab004[0x64];                      // +004  (not ported yet)
    RefObjHashMap               m_hashRefObj;                       // +068  _RefObjCommon by ID
    BYTE                        m_ab090[0xA08];                     // +090  (not ported yet)
    RefShardContentConfigMap    m_mapShardContentConfig;            // +A98  by CodeName128
    BYTE                        m_abAA4[0xE4];                      // +AA4  (not ported yet)
    RefUIStringMap              m_mapUIString;                      // +B88  by GroupCodeName128
    RefTriggerBindMap           m_mmapGameWorldBindTriggerCategory; // +B94
    RefTriggerCategoryMap       m_mapTriggerCategory;               // +BA0
    RefTriggerBindMap           m_mmapTriggerCategoryBindTrigger;   // +BAC
    RefTriggerMap               m_mapTrigger;                       // +BB8
    RefTriggerVariableMap       m_mmapTriggerVariable;              // +BC4  by BindTriggerID
    RefTriggerBindMap           m_mmapTriggerBindEvent;             // +BD0
    RefTriggerBindMap           m_mmapTriggerBindCondition;         // +BDC
    RefTriggerBindMap           m_mmapTriggerBindAction;            // +BE8
    RefTriggerCommonMap         m_mapTriggerCommon;                 // +BF4
    RefTriggerEventMap          m_mapTriggerEvent;                  // +C00
    RefTriggerConditionMap      m_mapTriggerCondition;              // +C0C
    RefTriggerActionMap         m_mapTriggerAction;                 // +C18
    RefTriggerConditionParamMap m_mapTriggerConditionParam;         // +C24  by GroupCodeName128
    RefTriggerActionParamMap    m_mapTriggerActionParam;            // +C30  by GroupCodeName128
    RefTriggerBindMap           m_mmapGameWorldBindGameWorldGroup;  // +C3C
    RefGameWorldGroupMap        m_mapGameWorldGroup;                // +C48
    RefGameWorldGroupConfigMap  m_mapGameWorldGroupConfig;          // +C54  by GroupCodeName128
};

C_ASSERT(FIELD_OFFSET(CReferenceData, m_hashRefObj) == 0x68);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_ab090) == 0x90);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapShardContentConfig) == 0xA98);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapUIString) == 0xB88);
C_ASSERT(FIELD_OFFSET(CReferenceData, m_mapGameWorldGroupConfig) == 0xC54);
C_ASSERT(sizeof(CReferenceData) == 0xC60);

// dword_8a8944.
extern CReferenceData* g_pReferenceData;
