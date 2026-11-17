#include "Win32Dialog.h"

#include <string.h>
#include <strsafe.h>

#include "BSLib/BSDebug.h"

namespace ServerFramework
{

// ---------------------------------------------------------------------------------------------
// CWin32Dialog

CWin32Dialog::CWin32Dialog()
{
    m_hInstance = NULL;
    m_dwParam = 0;
    m_bDialog = 1;
    m_nResult = 0;
    m_hParent = NULL;
}

CWin32Dialog::CWin32Dialog(UINT nTemplateID, DWORD dwParam, CWindowBase* pParent)
{
    m_hInstance = (HINSTANCE)(UINT_PTR)nTemplateID;
    m_dwParam = dwParam;
    m_pParent = pParent;
    m_bDialog = 1;
    if (pParent)
        m_hParent = pParent->m_hWnd;
    else
        m_hParent = NULL;
    m_nResult = 0;
}

CWin32Dialog::~CWin32Dialog()
{
}

LRESULT CWin32Dialog::OnInitDialog(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    SetChildFonts(11);
    return 0;
}

LRESULT CWin32Dialog::OnOK(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    EndDialogWith(IDOK);
    return 0;
}

LRESULT CWin32Dialog::OnCancel(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    EndDialogWith(IDCANCEL);
    return 0;
}

BOOL CWin32Dialog::EndDialogWith(INT_PTR nResult)
{
    m_nResult = nResult;
    return EndDialog(m_hWnd, nResult);
}

HWND CWin32Dialog::GetItem(int nID)
{
    if (!IsWindow(m_hWnd))
        BSAssertFail();
    return GetDlgItem(m_hWnd, nID);
}

BOOL CWin32Dialog::EnableItem(int nID, bool bEnable)
{
    if (!IsWindow(m_hWnd))
        BSAssertFail();
    return EnableWindow(GetItem(nID), bEnable);
}

BOOL CWin32Dialog::SetItemInt(int nID, UINT uValue, bool bSigned)
{
    if (!IsWindow(m_hWnd))
        BSAssertFail();
    return SetDlgItemInt(m_hWnd, nID, uValue, bSigned);
}

bool CWin32Dialog::SetItemFloat(int nID, float fValue)
{
    char szText[260];
    if (StringCchPrintfA(szText, sizeof(szText), "%.2f", (double)fValue) != S_OK)
    {
        BSAssertFail();
        return false;
    }
    return SetItemText(nID, szText) != FALSE;
}

UINT CWin32Dialog::GetItemInt(int nID, bool bSigned)
{
    if (!IsWindow(m_hWnd))
        BSAssertFail();
    return GetDlgItemInt(m_hWnd, nID, NULL, bSigned);
}

BOOL CWin32Dialog::SetItemText(int nID, LPCSTR pszText)
{
    if (!IsWindow(m_hWnd))
        BSAssertFail();
    return SetDlgItemTextA(m_hWnd, nID, pszText);
}

std::string CWin32Dialog::GetItemText(int nID)
{
    char szText[4096];
    if (!IsWindow(m_hWnd))
        BSAssertFail();
    GetDlgItemTextA(m_hWnd, nID, szText, sizeof(szText));
    return std::string(szText);
}

LRESULT CWin32Dialog::SetItemCheck(int nID, BYTE bCheck)
{
    if (!IsWindow(m_hWnd))
        BSAssertFail();
    return SendMessageA(GetItem(nID), BM_SETCHECK, bCheck, 0);
}

bool CWin32Dialog::IsItemChecked(int nID)
{
    if (!IsWindow(m_hWnd))
        BSAssertFail();
    return SendMessageA(GetItem(nID), BM_GETCHECK, 0, 0) != 0;
}

BOOL CWin32Dialog::SetChildFonts(int nHeight)
{
    return EnumChildWindows(m_hWnd, CWin32Dialog_EnumChildProc_SetFont, nHeight);
}

LRESULT CWin32Dialog::ComboAddString(int nID, LPCSTR pszText)
{
    if (!IsWindow(m_hWnd))
        BSAssertFail();
    return SendMessageA(GetItem(nID), CB_ADDSTRING, 0, (LPARAM)pszText);
}

LRESULT CWin32Dialog::ComboResetContent(int nID)
{
    if (!IsWindow(m_hWnd))
        BSAssertFail();
    return SendMessageA(GetItem(nID), CB_RESETCONTENT, 0, 0);
}

LRESULT CWin32Dialog::ComboGetCurSel(int nID)
{
    if (!IsWindow(m_hWnd))
        BSAssertFail();
    return SendMessageA(GetItem(nID), CB_GETCURSEL, 0, 0);
}

BOOL CALLBACK CWin32Dialog_EnumChildProc_SetFont(HWND hWnd, LPARAM lParam)
{
    char szClass[256];
    GetClassNameA(hWnd, szClass, 255);
    if (lstrcmpiA("Button", szClass) && lstrcmpiA("Static", szClass)
        && lstrcmpiA("SysDateTimePick32", szClass))
    {
        Win32Dialog_SetLocaleFont(hWnd, (int)lParam);
    }
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// CWin32PropertyDialog

CWin32PropertyDialog::CWin32PropertyDialog(UINT nTemplateID, DWORD dwParam,
                                           CWindowBase* pParent)
    : CWin32Dialog(nTemplateID, dwParam, pParent)
{
    m_bRequireAll = FALSE;  // not set by the original constructor
}

CWin32PropertyDialog::~CWin32PropertyDialog()
{
    m_mapFieldText.clear();
}

LRESULT CWin32PropertyDialog::OnInitDialog(WPARAM wParam, LPARAM lParam)
{
    CWin32Dialog::OnInitDialog(wParam, lParam);
    for (std::list<PROPERTY_DIALOG_TEXT*>::iterator it = m_listPending.begin();
         it != m_listPending.end(); ++it)
    {
        PROPERTY_DIALOG_TEXT* pText = *it;
        HWND hCtrl = GetItem(pText->nID);
        if (hCtrl)
        {
            SetWindowTextA(hCtrl, pText->strText.c_str());
            EnableWindow(hCtrl, pText->bEnable);
        }
        // BUG(FWUI-010): the entry is freed but stays in the list.
        delete pText;
    }
    return 0;
}

LRESULT CWin32PropertyDialog::OnOK(WPARAM wParam, LPARAM lParam)
{
    m_mapFieldText.clear();
    if (!EnumChildWindows(m_hWnd, EnumChildCollectEditText, (LPARAM)this))
        return 0;
    return CWin32Dialog::OnOK(wParam, lParam);
}

BOOL CALLBACK CWin32PropertyDialog::EnumChildCollectEditText(HWND hWnd, LPARAM lParam)
{
    CWin32PropertyDialog* pDialog = (CWin32PropertyDialog*)lParam;
    char szText[260];
    LONG nID = GetWindowLongA(hWnd, GWL_ID);
    GetClassNameA(hWnd, szText, sizeof(szText));
    if (CompareStringA(LOCALE_USER_DEFAULT, NORM_IGNORECASE | NORM_IGNOREKANATYPE, szText,
                       lstrlenA(szText), "edit", lstrlenA("edit")) != CSTR_EQUAL)
        return TRUE;

    GetWindowTextA(hWnd, szText, sizeof(szText));
    if (!strlen(szText) && pDialog->m_bRequireAll)
    {
        MessageBoxA(pDialog->m_hWnd, "please type to all input field", "error", MB_ICONHAND);
        return FALSE;
    }
    pDialog->InsertFieldText((UINT)nID, szText);
    return TRUE;
}

void CWin32PropertyDialog::QueueControlText(int nID, const char* pszText, BOOL bDisable)
{
    PROPERTY_DIALOG_TEXT* pText = new PROPERTY_DIALOG_TEXT;
    pText->nID = nID;
    pText->strText.assign(pszText, strlen(pszText));
    pText->bEnable = bDisable == FALSE;
    m_listPending.push_back(pText);
}

const char* CWin32PropertyDialog::GetFieldText(UINT nID)
{
    std::map<UINT, std::string>::iterator it = m_mapFieldText.find(nID);
    if (it == m_mapFieldText.end())
        return NULL;
    return it->second.c_str();
}

void CWin32PropertyDialog::InsertFieldText(UINT nID, const char* pszText)
{
    if (m_mapFieldText.find(nID) == m_mapFieldText.end())
        m_mapFieldText.insert(std::make_pair(nID, std::string(pszText)));
}

} // namespace ServerFramework
