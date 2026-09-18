#ifndef TF_VR_HUD_SAFEAREA_H
#define TF_VR_HUD_SAFEAREA_H
#include "vgui_controls/Panel.h"
#include "vgui/ISurface.h"
#include "sourcevr/isourcevirtualreality.h"

extern ConVar vr_hud_bottom_safearea;

// Per-panel state avoids accumulating the inset on successive HUD updates.
class CTFVRHudSafeArea
{
public:
	CTFVRHudSafeArea() : m_nOffset( 0 ), m_nAppliedX( 0 ) {}
	void Restore( vgui::Panel *panel )
	{
		int x, y;
		panel->GetPos( x, y );
		if ( m_nOffset && x == m_nAppliedX ) panel->SetPos( x - m_nOffset, y );
		m_nOffset = 0;
	}
	void Apply( vgui::Panel *panel, bool leftAnchored )
	{
		Restore( panel );
		if ( !UseVR() || !vr_hud_bottom_safearea.GetBool() ) return;
		int width, height, x, y;
		vgui::surface()->GetScreenSize( width, height );
		panel->GetPos( x, y );
		const int inset = Max( 0, ( width - height * 4 / 3 ) / 2 );
		m_nOffset = leftAnchored ? inset : -inset;
		m_nAppliedX = x + m_nOffset;
		panel->SetPos( m_nAppliedX, y );
	}
private:
	int m_nOffset, m_nAppliedX;
};
#endif
