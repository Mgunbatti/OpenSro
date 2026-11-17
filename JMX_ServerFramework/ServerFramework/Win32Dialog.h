#pragma once

#include <list>
#include <map>
#include <string>

#include "ServerFramework/WindowBase.h"

namespace ServerFramework
{

// Dialog driven by StaticWndProc: the dialog procedure must be CWindowBase::StaticWndProc with
// the object as creation parameter (WM_INITDIALOG registers it).
class CWin32Dialog : public CWindowBase
{
public:
    CWin32Dialog();
    CWin32Dialog(UINT nTemplateID, DWORD dwParam, CWindowBase* pParent);
    virtual ~CWin32Dialog();

    virtual LRESULT OnInitDialog(WPARAM wParam, LPARAM lParam);            // 2  font of 11
    virtual LRESULT OnOK(WPARAM wParam, LPARAM lParam);                    // 3  EndDialog(1)
    virtual LRESULT OnCancel(WPARAM wParam, LPARAM lParam);                // 4  EndDialog(2)
    virtual HWND GetItem(int nID);                                         // 5
    virtual BOOL EnableItem(int nID, bool bEnable);                        // 6
    virtual BOOL SetItemInt(int nID, UINT uValue, bool bSigned);           // 7
    virtual bool SetItemFloat(int nID, float fValue);                      // 8  "%.2f"
    virtual UINT GetItemInt(int nID, bool bSigned);                        // 9
    virtual BOOL SetItemText(int nID, LPCSTR pszText);                     // 10
    virtual std::string GetItemText(int nID);                              // 11 up to 4096
    virtual LRESULT SetItemCheck(int nID, BYTE bCheck);                    // 12
    virtual bool IsItemChecked(int nID);                                   // 13
    // 14: font for the user's language on every child except buttons, statics and date
    // pickers.
    virtual BOOL SetChildFonts(int nHeight);
    virtual LRESULT ComboAddString(int nID, LPCSTR pszText);               // 15
    virtual LRESULT ComboResetContent(int nID);                            // 16
    virtual LRESULT ComboGetCurSel(int nID);                               // 17

    // EndDialog_wrap (0x46d0a0)
    BOOL EndDialogWith(INT_PTR nResult);

public:
    DWORD   m_dwParam;      // +112
    INT_PTR m_nResult;      // +116
    HWND    m_hParent;      // +120
};

C_ASSERT(FIELD_OFFSET(CWin32Dialog, m_dwParam) == 112);
C_ASSERT(sizeof(CWin32Dialog) == 124);

// Pending text of a dialog control, applied in OnInitDialog.
struct PROPERTY_DIALOG_TEXT
{
    int         nID;        // +0
    std::string strText;    // +4
    BOOL        bEnable;    // +32
};
C_ASSERT(sizeof(PROPERTY_DIALOG_TEXT) == 0x24);

// Dialog with edit fields: queued texts are set when it opens; OK collects the edit texts by
// control ID.
class CWin32PropertyDialog : public CWin32Dialog
{
public:
    CWin32PropertyDialog(UINT nTemplateID, DWORD dwParam, CWindowBase* pParent);
    virtual ~CWin32PropertyDialog();

    // 2: applies and frees the queued texts (the list keeps the freed pointers, FWUI-010).
    virtual LRESULT OnInitDialog(WPARAM wParam, LPARAM lParam);
    // 3: collects the text of every edit control; closes the dialog when all were read.
    virtual LRESULT OnOK(WPARAM wParam, LPARAM lParam);

    // PropertyDialog_QueueControlText (0x466420)
    void QueueControlText(int nID, const char* pszText, BOOL bDisable);
    // WindowNameMap_GetNameCStr (0x4660b0): collected text of the control, NULL if none.
    const char* GetFieldText(UINT nID);
    // WindowNameMap_InsertIfAbsent (0x466120)
    void InsertFieldText(UINT nID, const char* pszText);

    static BOOL CALLBACK EnumChildCollectEditText(HWND hWnd, LPARAM lParam);

public:
    std::map<UINT, std::string>         m_mapFieldText;     // +124
    BOOL                                m_bRequireAll;      // +136 empty field: message box
    std::list<PROPERTY_DIALOG_TEXT*>    m_listPending;      // +140
};

C_ASSERT(FIELD_OFFSET(CWin32PropertyDialog, m_mapFieldText) == 124);
C_ASSERT(FIELD_OFFSET(CWin32PropertyDialog, m_bRequireAll) == 136);
C_ASSERT(FIELD_OFFSET(CWin32PropertyDialog, m_listPending) == 140);
C_ASSERT(sizeof(CWin32PropertyDialog) == 152);

BOOL CALLBACK CWin32Dialog_EnumChildProc_SetFont(HWND hWnd, LPARAM lParam);

} // namespace ServerFramework
