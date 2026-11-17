#pragma once

#include <list>
#include <map>

#include "ServerFramework/ServerBody.h"
#include "ServerFramework/ServerChildWindowBase.h"

namespace ServerFramework
{

// One server body shown by the architecture view. Positions are in world coordinates.
struct ARCH_VIEW_ENTRY
{
    RECT                            rc;         // +0
    SERVER_BODY*                    pBody;      // +16
    DWORD                           dw20;       // +20 never set
    std::list<ARCH_VIEW_ENTRY*>     listChild;  // +24 bodies certified by this one
    // +36 entry of the certifying body. Starts as rand() % 2 and is only replaced for bodies
    // with a parent ID; the node gets an overlay icon when it is not zero.
    ARCH_VIEW_ENTRY*                pParent;
};
C_ASSERT(FIELD_OFFSET(ARCH_VIEW_ENTRY, listChild) == 24);
C_ASSERT(sizeof(ARCH_VIEW_ENTRY) == 40);

typedef std::map<WORD, ARCH_VIEW_ENTRY> ArchViewNodeMap;
typedef std::map<DWORD, SERVER_CORD*>   ArchViewCordMap;

enum
{
    ARCH_VIEW_ZOOM_LEVELS = 30,
#ifdef JMX_FRAMEWORK_2007
    ARCH_VIEW_ICON_COUNT  = 8,      // FarmManager 0x441790: no icon 0x71 of state 7
#else
    ARCH_VIEW_ICON_COUNT  = 9,      // one per body state
#endif
};

// Resource IDs of the icons the application must provide (ServerFrameworkRes.dll).
enum
{
    IDI_ARCH_STATE0     = 0x70,
    IDI_ARCH_STATE1     = 0x69,
    IDI_ARCH_STATE2     = 0x6B,
    IDI_ARCH_STATE3     = 0x66,
    IDI_ARCH_STATE4     = 0x6C,
    IDI_ARCH_STATE5     = 0x6A,
    IDI_ARCH_STATE6     = 0x68,
    IDI_ARCH_STATE7     = 0x71,
    IDI_ARCH_STATE8     = 0x67,
    IDI_ARCH_OVERLAY    = 0x6F,
};

// Popup menu command IDs (the menu is shown without TPM_RETURNCMD, see FWUI-001).
enum
{
    IDM_ARCH_SHOW_NAME      = 1,
    IDM_ARCH_SHOW_CERT      = 2,
    IDM_ARCH_SHOW_CORD      = 3,
    IDM_ARCH_SHOW_DETAIL    = 4,
    IDM_ARCH_REARRANGE      = 0x100,
    IDM_ARCH_PROPERTY       = 0x101,
};

// Diagram of the server bodies (icons with labels), their certification tree and the cords
// between them. Left drag on empty space pans, on a node moves it.
class CServerArchitectureView : public CServerChildWindowBase
{
public:
    CServerArchitectureView();
    virtual ~CServerArchitectureView();

    virtual bool Destroy();                                                     // 2
    virtual bool Create(HINSTANCE hInstance, HWND hParent, RECT rc, HMENU hID); // 4
    // 5: adds every body and cord of the global lists, links the certification tree and lays
    // it out from the topmost ancestor of this server.
    virtual bool RebuildTopology();
    // 6: places pEntry at (x, y) and its children to the right, one below the other. Returns
    // the bounding rectangle in *pOut.
    virtual RECT* LayoutNodeTree(RECT* pOut, ARCH_VIEW_ENTRY* pEntry, int x, int y, int nUnused);
    // 7: stacks every node still at (0, 0) above this server's node.
    virtual RECT* StackUnplacedNodes(RECT* pOut);
    virtual BOOL DrawNode(HDC hDC, ARCH_VIEW_ENTRY* pEntry);                    // 8
    virtual void DrawLinks(HDC hDC);                                            // 9

    // message handlers
    LRESULT OnPaint(WPARAM wParam, LPARAM lParam);
    LRESULT OnLButtonDown(WPARAM wParam, LPARAM lParam);
    LRESULT OnLButtonUp(WPARAM wParam, LPARAM lParam);
    LRESULT OnMouseMove(WPARAM wParam, LPARAM lParam);
    LRESULT OnEraseBkgnd(WPARAM wParam, LPARAM lParam);
    LRESULT OnSetCursor(WPARAM wParam, LPARAM lParam);
    LRESULT OnRButtonUp(WPARAM wParam, LPARAM lParam);
    LRESULT OnArchLoaded(WPARAM wParam, LPARAM lParam);
    LRESULT OnCordChanged(WPARAM wParam, LPARAM lParam);
    LRESULT OnBodyChanged(WPARAM wParam, LPARAM lParam);

    ARCH_VIEW_ENTRY* FindItem(WORD wBodyID);
    ARCH_VIEW_ENTRY* HitTestNode(POINT pt);
    float GetZoomScale();
    POINT* WorldToScreen(POINT* pOut, int x, int y);
    POINT* ScreenToWorld(POINT* pOut, int x, int y);
    RECT* WorldRectToScreen(RECT* pOut, int nLeft, int nTop, int nRight, int nBottom);
    RECT* ScreenRectToWorldCentered(RECT* pOut, int nLeft, int nTop, int nRight, int nBottom);
    // Moves the node rectangle by *pDelta, stopped at the edges of the nodes it runs into;
    // *pDelta receives the distance actually moved.
    void MoveNodeRectWithCollision(RECT* pNode, POINT* pDelta);

public:
    ARCH_VIEW_ENTRY*    m_pRoot;                // +140
    ArchViewNodeMap     m_mapNode;              // +144 by body ID
    ArchViewCordMap     m_mapCord;              // +156 by cord ID
    int                 m_nNodeWidth;           // +168
    int                 m_nNodeHeight;          // +172
    int                 m_nGapX;                // +176
    int                 m_nGapY;                // +180
    bool                m_bShowCert;            // +184 certification tree lines
    bool                m_bShowCord;            // +185
    bool                m_bShowName;            // +186
    bool                m_bShowDetail;          // +187 IP addresses and cord IDs
    int                 m_nOriginX;             // +188
    int                 m_nOriginY;             // +192
    int                 m_nLastX;               // +196
    int                 m_nLastY;               // +200
    bool                m_bPanning;             // +204
    HDC                 m_hMemDC;               // +208
    HBITMAP             m_hMemBitmap;           // +212
    HCURSOR             m_hCursor;              // +216
    ARCH_VIEW_ENTRY*    m_pSelected;            // +220
    HICON               m_ahStateIcon[ARCH_VIEW_ICON_COUNT];    // +224
    HICON               m_hOverlayIcon;         // +260
    int                 m_nZoom;                // +264
    float               m_afZoom[ARCH_VIEW_ZOOM_LEVELS];        // +268
};

C_ASSERT(FIELD_OFFSET(CServerArchitectureView, m_pRoot) == 140);
C_ASSERT(FIELD_OFFSET(CServerArchitectureView, m_mapCord) == 156);
C_ASSERT(FIELD_OFFSET(CServerArchitectureView, m_bShowCert) == 184);
C_ASSERT(FIELD_OFFSET(CServerArchitectureView, m_nOriginX) == 188);
C_ASSERT(FIELD_OFFSET(CServerArchitectureView, m_bPanning) == 204);
C_ASSERT(FIELD_OFFSET(CServerArchitectureView, m_pSelected) == 220);
C_ASSERT(FIELD_OFFSET(CServerArchitectureView, m_ahStateIcon) == 224);
#ifdef JMX_FRAMEWORK_2007
C_ASSERT(FIELD_OFFSET(CServerArchitectureView, m_hOverlayIcon) == 256);
C_ASSERT(sizeof(CServerArchitectureView) == 0x180);
#else
C_ASSERT(FIELD_OFFSET(CServerArchitectureView, m_nZoom) == 264);
C_ASSERT(sizeof(CServerArchitectureView) == 0x184);
#endif

// Cord drawing style by cord status (0x4F88F0).
struct ARCH_CORD_STYLE
{
    COLORREF    clr;
    int         nPenStyle;
    int         nWidth;
};
extern const ARCH_CORD_STYLE g_aArchCordStyle[5];

} // namespace ServerFramework
