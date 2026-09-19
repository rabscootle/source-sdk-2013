#ifndef VGUI_PANEL_SCREEN_SIZE_SCOPE_H
#define VGUI_PANEL_SCREEN_SIZE_SCOPE_H
#pragma once

namespace vgui
{
class Panel;
// Module-local layout canvas; never changes render targets or input coordinates.
typedef bool (*PanelScreenSizeProvider)( Panel *, int &, int & );
void SetPanelScreenSizeProvider( PanelScreenSizeProvider provider );
class PanelScreenSizeScope
{
public:
    explicit PanelScreenSizeScope( Panel *panel );
    ~PanelScreenSizeScope();
private:
    bool m_bChanged, m_bWasOverridden;
    int m_nWidth, m_nHeight;
    PanelScreenSizeScope( const PanelScreenSizeScope & );
    PanelScreenSizeScope &operator=( const PanelScreenSizeScope & );
};
}
#endif
