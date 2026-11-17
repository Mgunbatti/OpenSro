#pragma once

#include <string>

#include "ServerFramework/WindowBase.h"

namespace ServerFramework
{

// View shown in the server window (architecture view, monitor view, views added by the
// server application).
class CServerChildWindowBase : public CWindowBase
{
public:
    CServerChildWindowBase() {}
    virtual ~CServerChildWindowBase() {}

    // slot 2: releases the view's resources and destroys the window. Nothing in the base.
    virtual bool Destroy() { return false; }
    // slot 3: menu text of the view.
    virtual const char* GetTitle() { return m_strTitle.c_str(); }

public:
    std::string m_strTitle;     // +112
};

C_ASSERT(FIELD_OFFSET(CServerChildWindowBase, m_strTitle) == 112);
C_ASSERT(sizeof(CServerChildWindowBase) == 140);

} // namespace ServerFramework
