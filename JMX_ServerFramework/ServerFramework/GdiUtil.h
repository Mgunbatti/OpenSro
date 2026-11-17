#pragma once

#include <windows.h>
#include <string>

// GDI, list view and clipboard helpers of the server monitor GUI.

namespace ServerFramework
{

// Selects a new GDI object into a DC; the destructor restores the old one and deletes the new
// one (Gdi_RestorePen / GdiBrush_RestoreDelete / GdiObjectGuard_Restore).
class CGdiScopedObject
{
public:
    ~CGdiScopedObject()
    {
        SelectObject(m_hDC, m_hOld);
        DeleteObject(m_hObject);
    }

protected:
    CGdiScopedObject() {}
    void Select(HDC hDC, HGDIOBJ hObject)
    {
        m_hObject = hObject;
        m_hOld = SelectObject(hDC, hObject);
        m_hDC = hDC;
    }

private:
    HGDIOBJ m_hObject;  // +0
    HGDIOBJ m_hOld;     // +4
    HDC     m_hDC;      // +8

    CGdiScopedObject(const CGdiScopedObject&);
    CGdiScopedObject& operator=(const CGdiScopedObject&);
};

// Gdi_SelectNewPen (0x4640e0)
class CGdiScopedPen : public CGdiScopedObject
{
public:
    CGdiScopedPen(HDC hDC, int nStyle, int nWidth, COLORREF clr)
    {
        Select(hDC, CreatePen(nStyle, nWidth, clr));
    }
};

// GdiBrush_CreateSelect (0x464140)
class CGdiScopedBrush : public CGdiScopedObject
{
public:
    CGdiScopedBrush(HDC hDC, COLORREF clr) { Select(hDC, CreateSolidBrush(clr)); }
};

// GdiScopedFont_CreateArial (0x464190): "arial" of the given point size.
class CGdiScopedArialFont : public CGdiScopedObject
{
public:
    CGdiScopedArialFont(HDC hDC, int nPointSize)
    {
        int nHeight = MulDiv(nPointSize, GetDeviceCaps(hDC, LOGPIXELSY), 72);
        Select(hDC, CreateFontA(-nHeight, 0, 0, 0, 0, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0,
                                "arial"));
    }
};

// Formats into a 255-byte buffer (asserts on truncation) and draws it with the given alignment.
UINT Gdi_TextOutf(HDC hDC, int x, int y, UINT uAlign, const char* pszFormat, ...);
// Like Gdi_TextOutf with a 4 KB buffer; "^N" switches to colour N of a 10-entry table.
UINT Gdi_TextOutColoredf(HDC hDC, int x, int y, UINT uAlign, const char* pszFormat, ...);

BOOL Gdi_DrawRectFillOutline(HDC hDC, const RECT* pRect, COLORREF clrFill, COLORREF clrLine);
// 6x6 blue square at (x, y): selection marker of the architecture view.
BOOL ArchView_DrawMarker6x6(HDC hDC, int x, int y);
BOOL Gdi_DrawRoundRect(HDC hDC, int nLeft, int nTop, int nRight, int nBottom, int nWidth,
                       int nHeight, COLORREF clrFill, COLORREF clrLine, int nStyle,
                       int nLineWidth);
BOOL Gdi_DrawRoundRect_wrap(HDC hDC, const RECT* pRect, int nWidth, int nHeight,
                            COLORREF clrFill, COLORREF clrLine, int nStyle, int nLineWidth);
BOOL Gdi_DrawLine(HDC hDC, int nWidth, int x1, int y1, int x2, int y2, COLORREF clr,
                  int nStyle);
// 10x10 yellow circle with black outline.
BOOL Gdi_DrawCircleMarker10(HDC hDC, int x, int y);
BOOL Gdi_DrawEllipseOutline(HDC hDC, int x, int y, int cx, int cy, COLORREF clr);
BOOL Gdi_DrawRectOutline(HDC hDC, int nWidth, int x, int y, int cx, int cy, COLORREF clr,
                         int nStyle);
// DrawTextA_wrap (0x464220)
int Gdi_DrawText(HDC hDC, const char* pszText, RECT* pRect, UINT uFormat);

// Geom_SegmentsIntersect2D (0x456cb0): whether segment (x1,y1)-(x2,y2) meets (x3,y3)-(x4,y4).
// Float arithmetic; (nearly) parallel segments never meet.
bool Geom_SegmentsIntersect2D(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4);

// Clipboard_SetText (0x469890): CF_TEXT copy of the string.
BOOL Clipboard_SetText(const char* pszText);
// Clipboard_SetTextStripColorCodes (0x469930): drops every "^<digit>" first.
void Clipboard_SetTextStripColorCodes(const char* pszText);

// List view helpers (SysListView32 messages).
LRESULT ListView_InsertColumnA_2(HWND hList, int nColumn, const char* pszText, int nWidth,
                                 int nFormat);
LRESULT ListView_AppendItemText(HWND hList, const char* pszText, LPARAM lParam);
LPARAM ListView_GetItemParam(HWND hList, int nItem);
LRESULT ListView_GetFirstSelectedItem(HWND hList);  // SendMessageA_wrap_2 (0x466790)
LRESULT ListView_GetFirstItem(HWND hList);          // SendMessageA_wrap_3 (0x4667b0)
std::string ListView_GetItemTextString(HWND hList, int nItem, int nSubItem);
// Tab separated header line and rows, colour codes stripped, to the clipboard.
void ListView_CopyAllToClipboard(HWND hList, int nColumns);
LRESULT ListView_SetItemCheckState(HWND hList, int nItem, BOOL bChecked);
LRESULT ComboBox_GetCurSel(HWND hCombo);            // SendMessageA_wrap_5 (0x466d40)
// ComboBox_AddStringFmtWithData (0x466c70, not referenced): adds the formatted text (asserts
// beyond 259 characters) with lParam as item data.
LRESULT ComboBox_AddStringFmtWithData(HWND hCombo, LPARAM lParam, const char* pszFormat, ...);

// Color_BlendPercent (0x464980, not referenced): nPercent of clr2 and the rest of clr1, per
// channel.
COLORREF Color_BlendPercent(COLORREF clr1, COLORREF clr2, BYTE nPercent);
// MessageBoxF (0x4677a0, not referenced): message box with the formatted text (1 KB, asserts on
// truncation).
int MessageBoxF(HWND hWnd, const char* pszCaption, UINT uType, const char* pszFormat, ...);

} // namespace ServerFramework
