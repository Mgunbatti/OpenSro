#include "GdiUtil.h"

#include <commctrl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strsafe.h>

#include "BSLib/BSDebug.h"

namespace ServerFramework
{

namespace
{

char s_szTextOut[255];              // 0x53DB08
char s_szColoredText[4096];         // 0x53BB00
char s_szColoredRun[4096];          // 0x53CB00

struct TEXT_COLOR
{
    COLORREF    clrText;
    COLORREF    clrBack;
    int         nBkMode;
};

// 0x4F8140
const TEXT_COLOR s_aTextColor[10] =
{
    { 0x000000, 0xFFFFFF, TRANSPARENT },
    { 0xB400B4, 0xFFFFFF, TRANSPARENT },
    { 0x0000FF, 0xFFFFFF, TRANSPARENT },
    { 0x808080, 0xFFFFFF, TRANSPARENT },
    { 0xFF0000, 0xFFFFFF, TRANSPARENT },
    { 0xFFFFFF, 0xFFFFFF, TRANSPARENT },
    { 0xFF00FF, 0xFFFFFF, TRANSPARENT },
    { 0x00FFFF, 0xFFFFFF, TRANSPARENT },
    { 0x008000, 0xFFFFFF, TRANSPARENT },
    { 0x058086, 0xFFFFFF, TRANSPARENT },
};

void Gdi_TextOutColoredRun(HDC hDC, int x, int y, int nColor, int nLen)
{
    // BUG(FWUI-007): a code below '0' gives a negative index and reads before the table.
    const TEXT_COLOR* pColor = s_aTextColor + (nColor % 10);
    SetTextColor(hDC, pColor->clrText);
    SetBkColor(hDC, pColor->clrBack);
    SetBkMode(hDC, pColor->nBkMode);
    TextOutA(hDC, x, y, s_szColoredRun, nLen);
}

} // namespace

UINT Gdi_TextOutf(HDC hDC, int x, int y, UINT uAlign, const char* pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    int nLen = _vsnprintf_s(s_szTextOut, sizeof(s_szTextOut), sizeof(s_szTextOut) - 1,
                            pszFormat, args);
    va_end(args);
    if (nLen < 0 || nLen > (int)sizeof(s_szTextOut) - 1)
    {
        s_szTextOut[sizeof(s_szTextOut) - 1] = 0;
        BSAssertFail();
    }
    else if (nLen == sizeof(s_szTextOut) - 1)
    {
        s_szTextOut[sizeof(s_szTextOut) - 1] = 0;
    }

    UINT uOldAlign = SetTextAlign(hDC, uAlign);
    TextOutA(hDC, x, y, s_szTextOut, (int)strlen(s_szTextOut));
    return SetTextAlign(hDC, uOldAlign);
}

UINT Gdi_TextOutColoredf(HDC hDC, int x, int y, UINT uAlign, const char* pszFormat, ...)
{
    va_list args;
    va_start(args, pszFormat);
    int nLen = _vsnprintf_s(s_szColoredText, sizeof(s_szColoredText),
                            sizeof(s_szColoredText) - 1, pszFormat, args);
    va_end(args);
    if (nLen < 0 || nLen > (int)sizeof(s_szColoredText) - 1)
    {
        s_szColoredText[sizeof(s_szColoredText) - 1] = 0;
        BSAssertFail();
    }
    else if (nLen == sizeof(s_szColoredText) - 1)
    {
        s_szColoredText[sizeof(s_szColoredText) - 1] = 0;
    }

    UINT uOldAlign = SetTextAlign(hDC, uAlign);
    int nOffset = 0;
    int nColor = 0;
    int nRun = 0;
    char* p = s_szColoredText;
    char* pRunStart = s_szColoredText;
    if (*p)
    {
        do
        {
            if (*p == '^')
            {
                if (nRun > 0)
                {
                    Gdi_TextOutColoredRun(hDC, nOffset + x, y, nColor, nRun);
                    SIZE size;
                    GetTextExtentPoint32A(hDC, s_szColoredRun, nRun, &size);
                    memset(s_szColoredRun, 0, nRun);
                    nOffset += size.cx;
                    p = pRunStart;
                    nRun = 0;
                }
                nColor = p[1] - '0';
                p += 2;
                pRunStart = p;
            }
            else
            {
                char* pNext = CharNextA(p);
                memcpy(&s_szColoredRun[nRun], pRunStart, pNext - pRunStart);
                p = pNext;
                nRun += (int)(pNext - pRunStart);
                pRunStart = pNext;
            }
        } while (*p);

        if (nRun > 0)
        {
            Gdi_TextOutColoredRun(hDC, x + nOffset, y, nColor, nRun);
            memset(s_szColoredRun, 0, nRun);
        }
    }
    return SetTextAlign(hDC, uOldAlign);
}

BOOL Gdi_DrawRectFillOutline(HDC hDC, const RECT* pRect, COLORREF clrFill, COLORREF clrLine)
{
    CGdiScopedBrush brush(hDC, clrFill);
    CGdiScopedPen pen(hDC, PS_SOLID, 1, clrLine);
    return Rectangle(hDC, pRect->left, pRect->top, pRect->right, pRect->bottom);
}

BOOL ArchView_DrawMarker6x6(HDC hDC, int x, int y)
{
    RECT rc;
    rc.left = x;
    rc.top = y;
    rc.right = x + 6;
    rc.bottom = y + 6;
    return Gdi_DrawRectFillOutline(hDC, &rc, 0xFF0000, 0xFF0000);
}

BOOL Gdi_DrawRoundRect(HDC hDC, int nLeft, int nTop, int nRight, int nBottom, int nWidth,
                       int nHeight, COLORREF clrFill, COLORREF clrLine, int nStyle,
                       int nLineWidth)
{
    CGdiScopedBrush brush(hDC, clrFill);
    CGdiScopedPen pen(hDC, nStyle, nLineWidth, clrLine);
    return RoundRect(hDC, nLeft, nTop, nRight, nBottom, nWidth, nHeight);
}

BOOL Gdi_DrawRoundRect_wrap(HDC hDC, const RECT* pRect, int nWidth, int nHeight,
                            COLORREF clrFill, COLORREF clrLine, int nStyle, int nLineWidth)
{
    return Gdi_DrawRoundRect(hDC, pRect->left, pRect->top, pRect->right, pRect->bottom, nWidth,
                             nHeight, clrFill, clrLine, nStyle, nLineWidth);
}

BOOL Gdi_DrawLine(HDC hDC, int nWidth, int x1, int y1, int x2, int y2, COLORREF clr, int nStyle)
{
    CGdiScopedPen pen(hDC, nStyle, nWidth, clr);
    MoveToEx(hDC, x1, y1, NULL);
    return LineTo(hDC, x2, y2);
}

BOOL Gdi_DrawCircleMarker10(HDC hDC, int x, int y)
{
    CGdiScopedBrush brush(hDC, 0xFFFF);
    CGdiScopedPen pen(hDC, PS_SOLID, 1, 0);
    return Ellipse(hDC, x, y, x + 10, y + 10);
}

BOOL Gdi_DrawEllipseOutline(HDC hDC, int x, int y, int cx, int cy, COLORREF clr)
{
    // The null brush stays selected.
    SelectObject(hDC, GetStockObject(NULL_BRUSH));
    CGdiScopedPen pen(hDC, PS_SOLID, 1, clr);
    return Ellipse(hDC, x, y, cx + x, y + cy);
}

BOOL Gdi_DrawRectOutline(HDC hDC, int nWidth, int x, int y, int cx, int cy, COLORREF clr,
                         int nStyle)
{
    CGdiScopedPen pen(hDC, nStyle, nWidth, clr);
    MoveToEx(hDC, x, y, NULL);
    LineTo(hDC, cx + x, y);
    LineTo(hDC, cx + x, y + cy);
    LineTo(hDC, x, y + cy);
    return LineTo(hDC, x, y);
}

int Gdi_DrawText(HDC hDC, const char* pszText, RECT* pRect, UINT uFormat)
{
    return DrawTextA(hDC, pszText, (int)strlen(pszText), pRect, uFormat);
}

bool Geom_SegmentsIntersect2D(int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4)
{
    float fX2 = (float)x2;
    float fY2 = (float)y2;
    float fX4 = (float)x4;
    float fY4 = (float)y4;
    float fDX1 = (float)(x1 - x2);
    float fDY1 = (float)(y1 - y2);
    float fDX2 = (float)(x3 - x4);
    float fDY2 = (float)(y3 - y4);

    float fDet = fDX1 * fDY2 - fDY1 * fDX2;
    float fLen1 = fDY1 * fDY1 + fDX1 * fDX1;
    float fLen2 = fDX2 * fDX2 + fDY2 * fDY2;
    // 2^-23: FLT_EPSILON
    if (!((double)fDet * fDet > (double)fLen1 * 1.1920928955078125e-007 * fLen2))
        return false;

    float fEX = fX4 - fX2;
    float fEY = fY4 - fY2;
    float fNum = fDY2 * fEX - fEY * fDX2;
    float fInvDet = 1.0f / fDet;
    float fT = fNum * fInvDet;
    float fPX = fX2 + fDX1 * fT;
    float fPY = fY2 + fT * fDY1;

    if ((double)(x1 < x2 ? x1 : x2) > fPX || (double)(x1 > x2 ? x1 : x2) < fPX)
        return false;
    if ((double)(x3 < x4 ? x3 : x4) > fPX || (double)(x3 > x4 ? x3 : x4) < fPX)
        return false;
    if ((double)(y1 < y2 ? y1 : y2) > fPY || (double)(y1 > y2 ? y1 : y2) < fPY)
        return false;
    if ((double)(y3 < y4 ? y3 : y4) > fPY || (double)(y3 > y4 ? y3 : y4) < fPY)
        return false;
    return true;
}

BOOL Clipboard_SetText(const char* pszText)
{
    BOOL bResult = OpenClipboard(NULL);
    if (!bResult)
        return bResult;

    EmptyClipboard();
    HGLOBAL hMem = GlobalAlloc(GMEM_DDESHARE, strlen(pszText) + 1);
    char* pDst = (char*)GlobalLock(hMem);
    size_t cch = strlen(pszText) + 1;
    if (cch > 0x7FFFFFFF || StringCchCopyA(pDst, cch, pszText) != S_OK)
        BSAssertFail();
    GlobalUnlock(hMem);
    SetClipboardData(CF_TEXT, hMem);
    return CloseClipboard();
}

void Clipboard_SetTextStripColorCodes(const char* pszText)
{
    int nLen = (int)strlen(pszText);
    char* pszCopy = (char*)malloc(nLen + 1);
    memset(pszCopy, 0, nLen + 1);
    char* pOut = pszCopy;
    for (int i = 0; i < nLen; ++i)
    {
        char c = pszText[i];
        if (c == '^' && (unsigned char)(pszText[i + 1] - '0') <= 9)
        {
            ++i;
            continue;
        }
        *pOut++ = c;
    }
    Clipboard_SetText(pszCopy);
    free(pszCopy);
}

LRESULT ListView_InsertColumnA_2(HWND hList, int nColumn, const char* pszText, int nWidth,
                                 int nFormat)
{
    LVCOLUMNA col;
    memset(&col, 0, sizeof(col));
    col.mask = LVCF_FMT | LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM;
    col.fmt = nFormat;
    col.cx = nWidth;
    col.pszText = (LPSTR)pszText;
    col.cchTextMax = 4;
    col.iSubItem = nColumn;
    return SendMessageA(hList, LVM_INSERTCOLUMNA, nColumn, (LPARAM)&col);
}

LRESULT ListView_AppendItemText(HWND hList, const char* pszText, LPARAM lParam)
{
    LVITEMA item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_TEXT | LVIF_PARAM;
    item.pszText = (LPSTR)pszText;
    item.cchTextMax = (int)strlen(pszText);
    item.lParam = lParam;
    item.iItem = (int)SendMessageA(hList, LVM_GETITEMCOUNT, 0, 0);
    return SendMessageA(hList, LVM_INSERTITEMA, 0, (LPARAM)&item);
}

LPARAM ListView_GetItemParam(HWND hList, int nItem)
{
    LVITEMA item;
    memset(&item, 0, sizeof(item));
    item.mask = LVIF_PARAM;
    item.iItem = nItem;
    SendMessageA(hList, LVM_GETITEMA, 0, (LPARAM)&item);
    return item.lParam;
}

LRESULT ListView_GetFirstSelectedItem(HWND hList)
{
    return SendMessageA(hList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_SELECTED);
}

LRESULT ListView_GetFirstItem(HWND hList)
{
    return SendMessageA(hList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_ALL);
}

std::string ListView_GetItemTextString(HWND hList, int nItem, int nSubItem)
{
    char szText[260];
    LVITEMA item;
    memset(&item, 0, sizeof(item));
    item.iSubItem = nSubItem;
    item.pszText = szText;
    szText[0] = 0;
    item.cchTextMax = sizeof(szText);
    SendMessageA(hList, LVM_GETITEMTEXTA, nItem, (LPARAM)&item);
    return std::string(szText);
}

void ListView_CopyAllToClipboard(HWND hList, int nColumns)
{
    std::string strAll;
    for (int i = 0; i < nColumns; ++i)
    {
        char szText[260];
        LVCOLUMNA col;
        memset(&col, 0, sizeof(col));
        col.mask = LVCF_TEXT;
        col.pszText = szText;
        col.cchTextMax = sizeof(szText);
        SendMessageA(hList, LVM_GETCOLUMNA, i, (LPARAM)&col);
        if (!strAll.empty())
            strAll.append("\t", 1);
        strAll.append(std::string(szText));
    }
    strAll.append("\r\n", 2);

    for (int nItem = (int)SendMessageA(hList, LVM_GETNEXTITEM, (WPARAM)-1, LVNI_ALL);
         nItem != -1;
         nItem = (int)SendMessageA(hList, LVM_GETNEXTITEM, nItem, LVNI_ALL))
    {
        std::string strLine;
        for (int nSub = 0; nSub < nColumns; ++nSub)
        {
            std::string strText = ListView_GetItemTextString(hList, nItem, nSub);
            if (!strLine.empty())
                strLine.append("\t", 1);
            strLine.append(strText);
        }
        strLine.append("\r\n", 2);
        strAll.append(strLine);
        strLine.clear();
    }
    Clipboard_SetTextStripColorCodes(strAll.c_str());
}

LRESULT ListView_SetItemCheckState(HWND hList, int nItem, BOOL bChecked)
{
    LVITEMA item;
    memset(&item, 0, sizeof(item));
    item.stateMask = LVIS_STATEIMAGEMASK;
    item.state = ((bChecked != 0) + 1) << 12;
    return SendMessageA(hList, LVM_SETITEMSTATE, nItem, (LPARAM)&item);
}

LRESULT ComboBox_GetCurSel(HWND hCombo)
{
    return SendMessageA(hCombo, CB_GETCURSEL, 0, 0);
}

} // namespace ServerFramework
