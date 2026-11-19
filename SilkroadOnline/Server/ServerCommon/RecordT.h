#pragma once

#include <stdlib.h>
#include <string.h>

#include <string>

#include "BSLib/DB/Database.h"

// The record class template of the SR_ShardManager table records (reference and instance
// records). Every record class of the original has the same generated parts:
// - a runtime class with factory (CreateObject = new T, DeleteObject = delete);
// - slot 0 GetRuntimeClass, slot 2 GetTableName (m_strTable.c_str()), slot 3 GetTableInfo;
// - a static CDBTableInfo filled by a dynamic initializer (the StaticInit_TableInfo functions of
//   the analysis, e.g. CRefLevel 0x61cd80, CInstanceUser 0x62ffe0);
// - a constructor / destructor that only run the ones of the base and of the members.
//
// A record is CDBRecordT<T, TBase, TData>: TBase is the record base (CDBRecord, CDBRecStream or
// an instance base class), TData the struct of the columns, which therefore start right after
// the TBase part (24 / 32 / 48, CDBTableInfo::m_nDataOffset). TData zeroes in its own
// constructor exactly what the original constructor zeroes; void means no data.
//
//     struct RefLevelData { BYTE Lvl; ... };
//     SR_RECORD(CRefLevel, CDBRecord, RefLevelData)
//     SR_IMPLEMENT_RECORD(CRefLevel, CDBRecord, "_REFLEVEL", DBTABLE_04, NULL, 0)  // .cpp
//
// A record with behaviour of its own declares the class itself and adds SR_RECORD_MEMBERS(T).

// One column of a table description: the name (with nIndex > 0 the decimal index is appended,
// "MagParam" 1 -> "MagParam1"), the member offset in the record object and the field group.
struct DBRecordColumnDef
{
    const char* pszName;
    int         nIndex;
    int         nOffset;
    int         nGroup;
};

// The table description initializer: table name, flags, the empty field groups 0 and 1, one
// DBColumnInfo per declared column in its group, the record class, m_nDataOffset and the column
// offsets made relative to it. A sample record is constructed (and destroyed) first.
template <class T>
int DBRecord_InitTableInfo(CDBTableInfo& info, const char* pszTable, DWORD dwFlags,
                           const DBRecordColumnDef* pColumns, int nColumns,
                           CRuntimeClass* pRecordClass, int nDataOffset)
{
    T record;
    info.m_strTable.assign(pszTable, strlen(pszTable));
    info.m_dwFlags = dwFlags;
    for (int nGroup = DBGROUP_KEY; nGroup <= DBGROUP_ALL; ++nGroup)
    {
        DB_FIELD_GROUP group;
        info.m_mapGroups.insert(DBFieldGroupMap::value_type(nGroup, group));
    }
    DBColumnInfo column;
    for (int i = 0; i < nColumns; ++i)
    {
        const DBRecordColumnDef& def = pColumns[i];
        if (def.nIndex > 0)
        {
            char szIndex[0x200];
            _itoa_s(def.nIndex, szIndex, sizeof(szIndex), 10);
            column.strName.assign(def.pszName, strlen(def.pszName));
            column.strName.append(szIndex, strlen(szIndex));
        }
        else
        {
            column.strName.assign(def.pszName, strlen(def.pszName));
        }
        column.nOffset = def.nOffset;
        info.m_mapGroups[def.nGroup].vecColumns.push_back(column);
    }
    info.m_pRecordClass = pRecordClass;
    info.m_nDataOffset = nDataOffset;
    for (DBFieldGroupMap::iterator it = info.m_mapGroups.begin(); it != info.m_mapGroups.end();
         ++it)
    {
        DBColumnVector& columns = it->second.vecColumns;
        for (DBColumnVector::iterator col = columns.begin(); col != columns.end(); ++col)
            col->nOffset -= info.m_nDataOffset;
    }
    return 0;
}

template <class T>
struct DBRecordValue
{
    typedef T Type;
};

// The column setter. nDirtyGroup is the update group whose bit is set (below 2: none); it is the
// column's field group except where the original passes another one.
template <class T>
inline void DBRecord_SetColumn(CDBRecord* pRecord, const CDBTableInfo& info, T& member,
                               typename DBRecordValue<T>::Type value,
                               int nDirtyGroup)
{
    if (member != value)
    {
        if (info.m_dwFlags & DBTABLE_UPDATE)
        {
            if (nDirtyGroup >= 2)
                pRecord->m_qwDirty |= (unsigned __int64)1 << nDirtyGroup;
            member = value;
        }
        else
        {
            BSAssertFail();
        }
    }
}

inline void DBRecord_SetColumn(CDBRecord* pRecord, const CDBTableInfo& info,
                               std::string& member, const std::string& value,
                               int nDirtyGroup)
{
    if (member.compare(value) != 0)
    {
        if (info.m_dwFlags & DBTABLE_UPDATE)
        {
            member.assign(value, 0, std::string::npos);
            if (nDirtyGroup >= 2)
                pRecord->m_qwDirty |= (unsigned __int64)1 << nDirtyGroup;
        }
        else
        {
            BSAssertFail();
        }
    }
}

// Timestamps compare year .. second (the fraction is ignored) and are copied whole.
inline void DBRecord_SetColumn(CDBRecord* pRecord, const CDBTableInfo& info,
                               TIMESTAMP_STRUCT& member, const TIMESTAMP_STRUCT& value,
                               int nDirtyGroup)
{
    if (value.year != member.year || value.month != member.month || value.day != member.day ||
        value.hour != member.hour || value.minute != member.minute ||
        value.second != member.second)
    {
        if (info.m_dwFlags & DBTABLE_UPDATE)
        {
            if (nDirtyGroup >= 2)
                pRecord->m_qwDirty |= (unsigned __int64)1 << nDirtyGroup;
            member = value;
        }
        else
        {
            BSAssertFail();
        }
    }
}

// The field group of the column at nOffset in a column list (-1 when there is none).
inline int DBRecord_FindColumnGroup(const DBRecordColumnDef* pColumns, int nColumns, int nOffset)
{
    for (int i = 0; i < nColumns; ++i)
    {
        if (pColumns[i].nOffset == nOffset)
            return pColumns[i].nGroup;
    }
    BSAssertFail();
    return -1;
}

template <class T, class TBase, class TData>
class CDBRecordT : public TBase, public TData
{
public:
    typedef TBase BaseType;

    // The plain column setter of the original records (one inlined or out-of-line copy per
    // column there, e.g. CInstanceCOS::SetName 0x522aa0): stores the value when it differs and
    // marks the update group the table description gives the column. Columns whose setter
    // behaves otherwise have a setter of their own.
    template <class V>
    void SetColumn(V TData::* pColumn, const typename DBRecordValue<V>::Type& value)
    {
        V& member = this->*pColumn;
        int nOffset = (int)((const BYTE*)&member - (const BYTE*)static_cast<T*>(this));
        DBRecord_SetColumn(this, T::s_TableInfo, member, value,
                           DBRecord_FindColumnGroup(T::s_pColumns, T::s_nColumns, nOffset));
    }

    static CBase* __cdecl CreateObject()
    {
        return new T;
    }

    static void __stdcall DeleteObject(CBase* pObject)
    {
        if (pObject)
            delete pObject;
    }

    virtual CRuntimeClass* GetRuntimeClass() const                          // 0
    {
        return T::ThisClass();
    }

    virtual const char* GetTableName()                                      // 2
    {
        return T::s_TableInfo.m_strTable.c_str();
    }

    virtual CDBTableInfo* GetTableInfo()                                    // 3
    {
        return &T::s_TableInfo;
    }
};

// A record without data of its own.
template <class T, class TBase>
class CDBRecordT<T, TBase, void> : public TBase
{
public:
    typedef TBase BaseType;

    static CBase* __cdecl CreateObject()
    {
        return new T;
    }

    static void __stdcall DeleteObject(CBase* pObject)
    {
        if (pObject)
            delete pObject;
    }

    virtual CRuntimeClass* GetRuntimeClass() const                          // 0
    {
        return T::ThisClass();
    }

    virtual const char* GetTableName()                                      // 2
    {
        return T::s_TableInfo.m_strTable.c_str();
    }

    virtual CDBTableInfo* GetTableInfo()                                    // 3
    {
        return &T::s_TableInfo;
    }
};

// The static members every record class declares (runtime class "class<T>", table description).
#define SR_RECORD_MEMBERS(class_name)                                                      \
public:                                                                                     \
    static CRuntimeClass class##class_name;                                                 \
    static CDBTableInfo s_TableInfo;                                                        \
    static const DBRecordColumnDef* const s_pColumns;                                       \
    static const int s_nColumns;                                                            \
    static int InitTableInfo();                                                             \
    static CRuntimeClass* ThisClass()                                                       \
    {                                                                                       \
        return &class##class_name;                                                          \
    }

// A record class without behaviour of its own.
#define SR_RECORD(class_name, base_name, data_name)                                        \
    class class_name : public CDBRecordT<class_name, base_name, data_name>                  \
    {                                                                                       \
        SR_RECORD_MEMBERS(class_name)                                                       \
    };

// Runtime class, table description and its dynamic initializer.
#define SR_IMPLEMENT_RECORD(class_name, base_name, table_name, flags, columns, column_count) \
    SR_IMPLEMENT_RECORD_RTC(class_name, base_name, &base_name::class##base_name, table_name,  \
                            flags, columns, column_count)

// The same for a base without a runtime class of its own (pBaseClass: the next one up).
#define SR_IMPLEMENT_RECORD_RTC(class_name, base_name, pBaseClass, table_name, flags, columns, \
                                column_count)                                               \
    CRuntimeClass class_name::class##class_name =                                           \
        { #class_name, sizeof(class_name), class_name::CreateObject,                        \
          class_name::DeleteObject, pBaseClass };                                           \
    CDBTableInfo class_name::s_TableInfo;                                                   \
    const DBRecordColumnDef* const class_name::s_pColumns = columns;                        \
    const int class_name::s_nColumns = column_count;                                        \
    int class_name::InitTableInfo()                                                         \
    {                                                                                       \
        return DBRecord_InitTableInfo<class_name>(s_TableInfo, table_name, flags, columns,  \
                                                  column_count,                             \
                                                  &class_name::class##class_name,           \
                                                  sizeof(base_name));                       \
    }                                                                                       \
    static int s_nTableInfo##class_name = class_name::InitTableInfo();
