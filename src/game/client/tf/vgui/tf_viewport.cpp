//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client DLL VGUI2 Viewport
//
// $Workfile:     $
// $Date:         $
//
//-----------------------------------------------------------------------------
// $Log: $
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"

#pragma warning( disable : 4800  )  // disable forcing int to bool performance warning

// VGUI panel includes
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include <KeyValues.h>
#include <vgui/Cursor.h>
#include <vgui/IScheme.h>
#include <vgui/IVGui.h>
#include <vgui/ILocalize.h>
#include <vgui/VGUI.h>
#include "tier0/icommandline.h"

// client dll/engine defines
#include "hud.h"
#include <voice_status.h>

// viewport definitions
#include <baseviewport.h>
#include "tf_viewport.h"
#include "tf_teammenu.h"

#include "vguicenterprint.h"
#include "text_message.h"
#include "tf_classmenu.h"

#include "tf_textwindow.h"
#include "tf_clientscoreboard.h"
#include "tf_spectatorgui.h"
#include "intromenu.h"
#include "tf_intromenu.h"

#include "tf_controls.h"
#include "tf_mapinfomenu.h"
#include "tf_roundinfo.h"

#include "item_pickup_panel.h"
#include "character_info_panel.h"
#include "tf_hud_arena_winpanel.h"
#include "tf_arenateammenu.h"
#include "tf_hud_pve_winpanel.h"
#include "hud_chat.h"
#include "tf_giveawayitempanel.h"
#if defined( REPLAY_ENABLED )
#include "replay/vgui/replaybrowsermainpanel.h"
#endif
#include "clientmode_tf.h"
#include "ienginevgui.h"
#include "sourcevr/isourcevirtualreality.h"
#include "vgui_controls/PanelScreenSizeScope.h"
#include "vgui_controls/BuildGroup.h"
#include "materialsystem/imaterialsystem.h"
#include "hud_crosshair.h"
#include "tf_hud_target_id.h"
#include "tf_hud_mainmenuoverride.h"
#include "c_tf_objective_resource.h"

#include "quest_log_panel.h"
#include "tf_matchmaking_dashboard.h"

//#include "tf_overview.h"

/*
CON_COMMAND( spec_help, "Show spectator help screen")
{
	if ( gViewPortInterface )
		gViewPortInterface->ShowPanel( PANEL_INFO, true );
}

CON_COMMAND( spec_menu, "Activates spectator menu")
{
	bool bShowIt = true;

	C_CSPlayer *pPlayer = C_CSPlayer::GetLocalCSPlayer();

	if ( pPlayer && !pPlayer->IsObserver() )
		return;

	if ( args.ArgC() == 2 )
	{
		bShowIt = atoi( args[ 1 ] ) == 1;
	}

	if ( gViewPortInterface )
		gViewPortInterface->ShowPanel( PANEL_SPECMENU, bShowIt );
}
*/


CON_COMMAND( showmapinfo, "Show map info panel" )
{
	if ( !gViewPortInterface )
		return;

	C_TFPlayer *pPlayer = C_TFPlayer::GetLocalTFPlayer();

	// don't let the player open the team menu themselves until they're a spectator or they're on a regular team and have picked a class
	if ( pPlayer )
	{
		if ( ( pPlayer->GetTeamNumber() == TEAM_SPECTATOR ) || 
		     ( ( pPlayer->GetTeamNumber() != TEAM_UNASSIGNED ) && ( pPlayer->GetPlayerClass()->GetClassIndex() != TF_CLASS_UNDEFINED ) ) )
		{
			// close all the other panels that could be open
			gViewPortInterface->ShowPanel( PANEL_TEAM, false );
			gViewPortInterface->ShowPanel( PANEL_ARENA_TEAM, false );
			gViewPortInterface->ShowPanel( PANEL_ARENA_WIN, false );
			gViewPortInterface->ShowPanel( PANEL_PVE_WIN, false );
			gViewPortInterface->ShowPanel( PANEL_CLASS_RED, false );
			gViewPortInterface->ShowPanel( PANEL_CLASS_BLUE, false );
			gViewPortInterface->ShowPanel( PANEL_INTRO, false );
			gViewPortInterface->ShowPanel( PANEL_ROUNDINFO, false );
			gViewPortInterface->ShowPanel( PANEL_MAPINFO, true );
			gViewPortInterface->ShowPanel( PANEL_GIVEAWAY_ITEM, false );
		}
	}
}

CON_COMMAND( changeteam, "Choose a new team" )
{
	if ( !gViewPortInterface )
		return;

	C_TFPlayer *pPlayer = C_TFPlayer::GetLocalTFPlayer();

	// don't let the player open the team menu themselves until they're on a team
	if ( pPlayer && pPlayer->CanShowTeamMenu() )
	{
		if ( TFGameRules()->IsInArenaMode() == true && tf_arena_use_queue.GetBool() == true )
		{
			gViewPortInterface->ShowPanel( PANEL_ARENA_TEAM, true );
		}
		else
		{
			gViewPortInterface->ShowPanel( PANEL_TEAM, true );
		}
	}
}

CON_COMMAND( changeclass, "Choose a new class" )
{
	if ( !gViewPortInterface )
		return;

	C_TFPlayer *pPlayer = C_TFPlayer::GetLocalTFPlayer();

	if ( pPlayer && pPlayer->CanShowClassMenu() )
	{
		if ( TFGameRules() && TFGameRules()->IsInArenaMode() && pPlayer->IsAlive() == true )
		{
			if ( pPlayer->GetTeamNumber() > LAST_SHARED_TEAM && ( tf_arena_force_class.GetBool() == true || TFGameRules()->InStalemate() == true ) )
			{
				CBaseHudChat *pHUDChat = (CBaseHudChat *)GET_HUDELEMENT( CHudChat );

				if ( pHUDChat )
				{
					char szLocalized[100];
					g_pVGuiLocalize->ConvertUnicodeToANSI( g_pVGuiLocalize->Find( "#TF_Arena_NoClassChange" ), szLocalized, sizeof(szLocalized) );

					pHUDChat->ChatPrintf( pPlayer->entindex(), CHAT_FILTER_NONE, "%s ", szLocalized );
				}

				return;
			}
		}

		if ( TFGameRules() && TFGameRules()->IsMannVsMachineMode() )
		{
			if ( !TFGameRules()->InSetup() )
			{
				CBaseHudChat *pHUDChat = (CBaseHudChat *)GET_HUDELEMENT( CHudChat );

				if ( pHUDChat )
				{
					char szLocalized[100];
					g_pVGuiLocalize->ConvertUnicodeToANSI( g_pVGuiLocalize->Find( "#TF_MVM_NoClassChangeAfterSetup" ), szLocalized, sizeof(szLocalized) );

					pHUDChat->ChatPrintf( pPlayer->entindex(), CHAT_FILTER_NONE, "%s ", szLocalized );
				}

				return;
			}
		}

		switch( pPlayer->GetTeamNumber() )
		{
		case TF_TEAM_RED:
			gViewPortInterface->ShowPanel( PANEL_CLASS_RED, true );
			break;
		case TF_TEAM_BLUE:
			gViewPortInterface->ShowPanel( PANEL_CLASS_BLUE, true );
			break;
		case TEAM_SPECTATOR:
			{
				if( TFGameRules() && TFGameRules()->IsInArenaMode() == true && tf_arena_use_queue.GetBool() == true )
				{
					gViewPortInterface->ShowPanel( PANEL_CLASS_BLUE, true );
				}
			}
			break;
		default:
			break;
		}
	}
}

CON_COMMAND( togglescores, "Toggles score panel")
{
	if ( !gViewPortInterface )
		return;

	IViewPortPanel *scoreboard = gViewPortInterface->FindPanelByName( PANEL_SCOREBOARD );

	if ( !scoreboard )
		return;

	if ( scoreboard->IsVisible() )
	{
		gViewPortInterface->ShowPanel( scoreboard, false );
		GetClientVoiceMgr()->StopSquelchMode();
	}
	else
	{
		gViewPortInterface->ShowPanel( scoreboard, true );
	}
}

// The HUD has a real 4:3 parent, while viewport dialogs and GameUI remain
// parented to the original widescreen viewport. Resource alignment, Think,
// scheme reloads, and painting all see the same logical HUD dimensions.
static vgui::PHandle s_VRHudCanvas;
static vgui::PHandle s_VRHudViewport;
struct VRHudParent_t
{
	vgui::PHandle panel;
	vgui::PHandle parent;
};
static CUtlVector<VRHudParent_t> s_VRHudParents;

struct VRHudVisibility_t
{
	vgui::PHandle panel;
	bool visible;
};

static void SaveVRHudVisibility( vgui::Panel *panel, CUtlVector<VRHudVisibility_t> &saved )
{
	VRHudVisibility_t state;
	state.panel = panel;
	// Save the stored flag, not a derived panel's transient draw eligibility.
	// Escort IsVisible() returns false until point positions arrive; restoring
	// that as SetVisible(false) prevents its OnTick initialization from running.
	// The raw flag still preserves deliberately hidden event-driven panels.
	state.visible = panel->vgui::Panel::IsVisible();
	saved.AddToTail( state );
	for ( int i = 0; i < panel->GetChildCount(); ++i )
	{
		vgui::Panel *child = panel->GetChild( i );
		if ( child )
			SaveVRHudVisibility( child, saved );
	}
}

// Reflow only affected HUD roots. A layout change must not create controls,
// reveal event-driven panels, reset gameplay state, or touch menu resources.
static void ReloadVRHudLayout( vgui::Panel *viewport, const CUtlVector<vgui::PHandle> &panels )
{
	if ( panels.Count() == 0 )
		return;
	KeyValuesAD layout( "HudLayout" );
	const bool loaded = layout->LoadFromFile( filesystem, "scripts/HudLayout.res", "GAME" );
	KeyValuesAD conditions( "conditions" );
	g_pClientMode->ComputeVguiResConditions( conditions );
	static_cast<CBaseViewport *>( viewport )->GetBuildGroup()->ProcessConditionalKeys( layout, conditions );

	FOR_EACH_VEC( panels, i )
	{
		vgui::Panel *panel = panels[i].Get();
		if ( !panel )
			continue;
		CUtlVector<VRHudVisibility_t> saved;
		SaveVRHudVisibility( panel, saved );
		vgui::PanelScreenSizeScope screenSize( panel );
		KeyValues *settings = loaded ? layout->FindKey( panel->GetName() ) : NULL;
		if ( settings )
			panel->ApplySettings( settings );
		// Match normal viewport setup: generic positions FIRST, then each
		// panel's specialized resource (Frontier Justice, ball, cleaver, etc.).
		panel->InvalidateLayout( true, true );
		FOR_EACH_VEC( saved, j )
		{
			if ( saved[j].panel.Get() )
				saved[j].panel->SetVisible( saved[j].visible );
		}
	}
}

static bool VRHudScreenSize( vgui::Panel *panel, int &width, int &height )
{
	if ( !UseVR() || !s_VRHudCanvas.Get() || !s_VRHudViewport.Get() )
		return false;
	// Do not use the viewport's cached bounds here: OnScreenSizeChanged asks
	// this provider for the NEW size while those bounds still contain the old
	// resolution. Feeding them back prevents the viewport from ever resizing.
	materials->GetBackBufferDimensions( width, height );
	if ( width <= 0 || height <= 0 )
		return false;
	for ( vgui::Panel *ancestor = panel; ancestor; ancestor = ancestor->GetParent() )
	{
		if ( ancestor == s_VRHudCanvas.Get() )
		{
			s_VRHudCanvas->GetSize( width, height );
			break;
		}
	}
	return true;
}

void UpdateTFVRHudLayout()
{
	vgui::Panel *viewport = g_pClientMode->GetViewport();
	const bool active = UseVR() && viewport && engine->IsInGame();
	if ( !active || s_VRHudViewport.Get() != viewport )
	{
		vgui::PHandle previousViewport = s_VRHudViewport;
		CUtlVector<vgui::PHandle> restored;
		FOR_EACH_VEC( s_VRHudParents, i )
		{
			vgui::Panel *panel = s_VRHudParents[i].panel.Get();
			if ( panel && panel->GetParent() == s_VRHudCanvas.Get() && s_VRHudParents[i].parent.Get() )
			{
				panel->SetParent( s_VRHudParents[i].parent.Get() );
				restored[restored.AddToTail()] = panel;
			}
		}
		s_VRHudParents.RemoveAll();
		if ( s_VRHudCanvas.Get() )
			s_VRHudCanvas->MarkForDeletion();
		s_VRHudCanvas = NULL;
		s_VRHudViewport = NULL;
		if ( previousViewport.Get() )
		{
			ReloadVRHudLayout( previousViewport.Get(), restored );
		}
		if ( !active )
			return;
	}

	int width, height;
	// Resolve the full canvas independently of deferred VGUI resize messages.
	// Menus and aim-projected panels retain this full-resolution coordinate space.
	materials->GetBackBufferDimensions( width, height );
	if ( width <= 0 || height <= 0 )
		return;
	if ( viewport->GetWide() != width || viewport->GetTall() != height )
		viewport->SetSize( width, height );
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	const bool spectator = player && player->IsObserver();
	const int hudWidth = spectator ? width : Min( width, height * 4 / 3 );
	if ( !s_VRHudCanvas.Get() )
	{
		s_VRHudViewport = viewport;
		// Plain Panel deliberately retains the children's existing HUD build
		// group, so HudLayout.res and animation scripts still find them.
		s_VRHudCanvas = new vgui::Panel( viewport, "VRGameplayHUD" );
		s_VRHudCanvas->SetPaintBackgroundEnabled( false );
		s_VRHudCanvas->SetPaintEnabled( false );
		s_VRHudCanvas->SetMouseInputEnabled( false );
		s_VRHudCanvas->SetKeyBoardInputEnabled( false );
	}
	const bool resized = s_VRHudCanvas->GetWide() != hudWidth || s_VRHudCanvas->GetTall() != height;
	s_VRHudCanvas->SetBounds( ( width - hudWidth ) / 2, 0, hudWidth, height );

	CUtlVector<vgui::PHandle> changedPanels;
	for ( int i = s_VRHudParents.Count() - 1; i >= 0; --i )
	{
		if ( !s_VRHudParents[i].panel.Get() )
			s_VRHudParents.FastRemove( i );
	}
	gHUD.ForEachHudElement( [&]( CHudElement *element )
	{
		vgui::Panel *panel = dynamic_cast<vgui::Panel *>( element );
		// Aim-projected indicators keep their original full-canvas coordinates.
		// This changes neither the aim ray nor its projection/recenter limits.
		if ( !panel || dynamic_cast<CHudCrosshair *>( element ) || dynamic_cast<CTargetID *>( element ) ||
			dynamic_cast<IViewPortPanel *>( panel ) || panel->IsPopup() )
			return;
		vgui::Panel *parent = panel->GetParent();
		if ( parent != viewport )
			return; // Nested HUD children already inherit their parent's canvas.
		VRHudParent_t original;
		original.panel = panel;
		original.parent = parent;
		s_VRHudParents.AddToTail( original );
		panel->SetParent( s_VRHudCanvas.Get() );
		changedPanels[changedPanels.AddToTail()] = panel;
	} );
	if ( resized )
	{
		// Includes observer entry and respawn. Shared objective panels inherit
		// the new canvas instead of retaining their previous 4:3 coordinates.
		changedPanels.RemoveAll();
		for ( int i = 0; i < s_VRHudCanvas->GetChildCount(); ++i )
			changedPanels[changedPanels.AddToTail()] = s_VRHudCanvas->GetChild( i );
	}
	ReloadVRHudLayout( viewport, changedPanels );
}

CON_COMMAND( vr_hud_layout_status, "Print the VR HUD and menu logical canvas sizes." )
{
	if ( !UseVR() || !s_VRHudCanvas.Get() || !s_VRHudViewport.Get() )
	{
		Msg( "VR gameplay HUD canvas is inactive.\n" );
		return;
	}
	Msg( "VR HUD: %dx%d; menus: %dx%d; aim projection: unchanged; HUD roots: %d\n",
		s_VRHudCanvas->GetWide(), s_VRHudCanvas->GetTall(),
		s_VRHudViewport->GetWide(), s_VRHudViewport->GetTall(), s_VRHudParents.Count() );
}

TFViewport::TFViewport()
{
	vgui::SetPanelScreenSizeProvider( VRHudScreenSize );
	ivgui()->AddTickSignal( GetVPanel(), 0 );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
TFViewport::~TFViewport()
{
}

//-----------------------------------------------------------------------------
// Purpose: called when the VGUI subsystem starts up
//			Creates the sub panels and initialises them
//-----------------------------------------------------------------------------
void TFViewport::Start( IGameUIFuncs *pGameUIFuncs, IGameEventManager2 * pGameEventManager )
{
	BaseClass::Start( pGameUIFuncs, pGameEventManager );
}

void TFViewport::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );

	gHUD.InitColors( pScheme );

	SetPaintBackgroundEnabled( false );

	// Precache some font characters for the 360
 	if ( IsX360() || CommandLine()->CheckParm( "-precachefontchars" ) || CommandLine()->CheckParm( "-precachefontintlchars" ) )
 	{
 		const wchar_t *pAllChars = L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789,.!:";
 		const wchar_t *pNumbers = L"0123456789";
		// Try some tricky characters and some international characters with accents, etc.
		static const wchar_t IntlChars[] = { 'a', 'b', 'c', 'i', 'o', 'y', 'A', 'B', 'C', 0xbf, 0xc1, 0xd1, 0xd3, 0x00 };

		if ( CommandLine()->CheckParm( "-precachefontintlchars" ) )
			pAllChars = IntlChars;

 		vgui::surface()->PrecacheFontCharacters( pScheme->GetFont( "ScoreboardTeamName" ), pAllChars );
 		vgui::surface()->PrecacheFontCharacters( pScheme->GetFont( "ScoreboardMedium" ), pAllChars );
 		vgui::surface()->PrecacheFontCharacters( pScheme->GetFont( "ScoreboardSmall" ), pAllChars );
 		vgui::surface()->PrecacheFontCharacters( pScheme->GetFont( "ScoreboardVerySmall" ), pAllChars );
 		vgui::surface()->PrecacheFontCharacters( pScheme->GetFont( "TFFontMedium" ), pAllChars );
 		vgui::surface()->PrecacheFontCharacters( pScheme->GetFont( "TFFontSmall" ), pAllChars );
 		vgui::surface()->PrecacheFontCharacters( pScheme->GetFont( "HudFontMedium" ), pAllChars );
 		vgui::surface()->PrecacheFontCharacters( pScheme->GetFont( "HudFontMediumSmallSecondary" ), pAllChars );
 		vgui::surface()->PrecacheFontCharacters( pScheme->GetFont( "HudFontSmall" ), pAllChars );
 		vgui::surface()->PrecacheFontCharacters( pScheme->GetFont( "DefaultSmall" ), pAllChars );
 
 		vgui::surface()->PrecacheFontCharacters( pScheme->GetFont( "ScoreboardTeamScore" ), pNumbers );
 	}
}

//
// This is the main function of the viewport. Right here is where we create our class menu, 
// team menu, and anything else that we want to turn on and off in the UI.
//
IViewPortPanel* TFViewport::CreatePanelByName(const char *szPanelName)
{
	IViewPortPanel* newpanel = NULL;

	// overwrite MOD specific panel creation

	if ( Q_strcmp( PANEL_SCOREBOARD, szPanelName ) == 0 )
	{
		newpanel = new CTFClientScoreBoardDialog( this );
	}
	else if ( Q_strcmp( PANEL_SPECGUI, szPanelName ) == 0 )
	{
		newpanel = new CTFSpectatorGUI( this );	
	}
	else if ( Q_strcmp( PANEL_SPECMENU, szPanelName ) == 0 )
	{
//		newpanel = new CTFSpectatorGUI( this );	
	}
	else if ( Q_strcmp( PANEL_OVERVIEW, szPanelName ) == 0 )
	{
//		newpanel = new CTFMapOverview( this );
	}
	else if ( Q_strcmp( PANEL_INFO, szPanelName ) == 0 )
	{
		newpanel = new CTFTextWindow( this );
	}
	else if ( Q_strcmp( PANEL_MAPINFO, szPanelName ) == 0 )
	{
		newpanel = new CTFMapInfoMenu( this );
	}
	else if ( Q_strcmp( PANEL_ROUNDINFO, szPanelName ) == 0 )
	{
		newpanel = new CTFRoundInfo( this );
	}
	else if ( Q_strcmp( PANEL_TEAM, szPanelName ) == 0 )
	{
		newpanel = new CTFTeamMenu( this );	
	}
	else if ( Q_strcmp( PANEL_CLASS_RED, szPanelName ) == 0 )
	{
		newpanel = new CTFClassMenu_Red( this );	
	}
	else if ( Q_strcmp( PANEL_CLASS_BLUE, szPanelName ) == 0 )
	{
		newpanel = new CTFClassMenu_Blue( this );	
	}
	else if ( Q_strcmp( PANEL_INTRO, szPanelName ) == 0 )
	{
		newpanel = new CTFIntroMenu( this );
	}
	else if ( Q_strcmp( PANEL_ARENA_TEAM, szPanelName ) == 0 )
	{
		newpanel = new CTFArenaTeamMenu( this );
	}
	else if ( Q_strcmp( PANEL_ARENA_WIN, szPanelName ) == 0 )
	{
		newpanel = new CTFArenaWinPanel( this );
	}
	else if ( Q_strcmp( PANEL_PVE_WIN, szPanelName ) == 0 )
	{
		newpanel = new CTFPVEWinPanel( this );
	}
	else if ( Q_strcmp( PANEL_GIVEAWAY_ITEM, szPanelName ) == 0 )
	{
		newpanel = new CTFGiveawayItemPanel( this );
	}
	else if ( Q_strcmp( PANEL_MAINMENUOVERRIDE, szPanelName ) == 0 )
	{
		newpanel = new CHudMainMenuOverride( this );
	}
	else
	{
		// create a generic base panel, don't add twice
		newpanel = BaseClass::CreatePanelByName( szPanelName );
	}

	return newpanel; 
}

void TFViewport::CreateDefaultPanels( void )
{
	AddNewPanel( CreatePanelByName( PANEL_MAPINFO ), "PANEL_MAPINFO" );
	AddNewPanel( CreatePanelByName( PANEL_TEAM ), "PANEL_TEAM" );
	AddNewPanel( CreatePanelByName( PANEL_CLASS_RED ), "PANEL_CLASS_RED" );
	AddNewPanel( CreatePanelByName( PANEL_CLASS_BLUE ), "PANEL_CLASS_BLUE" );
	AddNewPanel( CreatePanelByName( PANEL_INTRO ), "PANEL_INTRO" );
	AddNewPanel( CreatePanelByName( PANEL_ROUNDINFO ), "PANEL_ROUNDINFO" );
	AddNewPanel( CreatePanelByName( PANEL_ARENA_WIN ), "PANEL_ARENA_WIN" );
	AddNewPanel( CreatePanelByName( PANEL_ARENA_TEAM ), "PANEL_ARENA_TEAM" );
	AddNewPanel( CreatePanelByName( PANEL_PVE_WIN ), "PANEL_PVE_WIN" );
	AddNewPanel( CreatePanelByName( PANEL_GIVEAWAY_ITEM ), "PANEL_GIVEAWAY_ITEM" );

	CHudMainMenuOverride *pMMOverride = (CHudMainMenuOverride*)CreatePanelByName( PANEL_MAINMENUOVERRIDE );
	if ( pMMOverride )
	{
		AddNewPanel( pMMOverride, "PANEL_MAINMENUOVERRIDE" );
		pMMOverride->AttachToGameUI();	
	}

	BaseClass::CreateDefaultPanels();
}

int TFViewport::GetDeathMessageStartHeight( void )
{
	int y = YRES(2);

	if ( IsX360() )
	{
		y = YRES(36);
	}

	if ( g_pSpectatorGUI && g_pSpectatorGUI->IsVisible() )
	{
		y = YRES(2) + g_pSpectatorGUI->GetTopBarHeight();
	}

	return y;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void TFViewport::OnScreenSizeChanged( int iOldWide, int iOldTall )
{
	BaseClass::OnScreenSizeChanged( iOldWide, iOldTall );

	// we've changed resolution, let's try to figure out if we need to show any of our menus
	if ( !gViewPortInterface )
		return;

	C_TFPlayer *pPlayer = C_TFPlayer::GetLocalTFPlayer();

	if ( pPlayer )
	{
		// are we on a team yet?
		if ( pPlayer->GetTeamNumber() == TEAM_UNASSIGNED )
		{
			engine->ClientCmd( "show_motd" );
		}
		else if ( ( pPlayer->GetTeamNumber() != TEAM_SPECTATOR ) && ( pPlayer->m_Shared.GetDesiredPlayerClassIndex() == TF_CLASS_UNDEFINED ) )
		{
			if ( tf_arena_force_class.GetBool() == false )
			{
				switch( pPlayer->GetTeamNumber() )
				{
				case TF_TEAM_RED:
					gViewPortInterface->ShowPanel( PANEL_CLASS_RED, true );
					break;
				case TF_TEAM_BLUE:
					gViewPortInterface->ShowPanel( PANEL_CLASS_BLUE, true );
					break;
				}
			}
		}
	}

	// The dashboard can't listen for this directly because it's parenting is all
	// over the place.  Reset the dashboard so it get sized correctly.
	GetDashboardPanel().RecreateAll();
	GetMMDashboard()->Reload();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void TFViewport::OnTick()
{
	m_pAnimController->UpdateAnimations( gpGlobals->curtime );
}
