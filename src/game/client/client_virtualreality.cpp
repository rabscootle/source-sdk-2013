//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//
#include "cbase.h"

#include "client_virtualreality.h"

#include "materialsystem/itexture.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/materialsystem_config.h"
#include "view_shared.h"
#include "view_scene.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "vgui_controls/Controls.h"
#include "sourcevr/isourcevirtualreality.h"
#include "ienginevgui.h"
#include "cdll_client_int.h"
#include "vgui/IVGui.h"
#include "vgui_controls/Controls.h"
#include "tier0/vprof_telemetry.h"
#include <time.h>
#include "steam/steam_api.h"
#ifdef TF_CLIENT_DLL
#include "c_tf_player.h"
#include "iinput.h"
#endif

static bool IsVRThirdPersonTauntCamera()
{
#ifdef TF_CLIENT_DLL
	C_TFPlayer *player = C_TFPlayer::GetLocalTFPlayer();
	// Include the camera's blend back to first person, not just the condition.
	return player && player->IsAlive() && !player->IsObserver() &&
		player->IsTauntCameraActive() && input->CAM_IsThirdPerson();
#else
	return false;
#endif
}

const char *COM_GetModDirectory(); // return the mod dir (rather than the complete -game param, which can be a path)

CClientVirtualReality g_ClientVirtualReality;

EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CClientVirtualReality, IClientVirtualReality, 
	CLIENTVIRTUALREALITY_INTERFACE_VERSION, g_ClientVirtualReality );


// --------------------------------------------------------------------
// A huge pile of VR convars
// --------------------------------------------------------------------
ConVar vr_activate_default( "vr_activate_default",		"0", FCVAR_ARCHIVE, "If this is true the game will switch to VR mode once startup is complete." );


ConVar vr_moveaim_mode      ( "vr_moveaim_mode",      "3", FCVAR_ARCHIVE, "0=move+shoot from face. 1=move with torso. 2,3,4=shoot with face+mouse cursor. 5+ are probably not that useful." );
ConVar vr_moveaim_mode_zoom ( "vr_moveaim_mode_zoom", "3", FCVAR_ARCHIVE, "0=move+shoot from face. 1=move with torso. 2,3,4=shoot with face+mouse cursor. 5+ are probably not that useful." );
ConVar vr_scope_mouse_pitch( "vr_scope_mouse_pitch", "0", FCVAR_ARCHIVE,
	"Allow vertical mouse input alongside head aiming while scoped (zoom aim modes 0, 1 and 3).",
	true, 0.0f, true, 1.0f );
ConVar vr_aim_recenter_enable( "vr_aim_recenter_enable", "1", FCVAR_ARCHIVE,
	"In vr_moveaim_mode 3, recenter off-HUD aim onto the head view until deliberate mouse/joystick input resumes.",
	true, 0.0f, true, 1.0f );
ConVar vr_mouse_recenter_vertical_margin( "vr_mouse_recenter_vertical_margin", "1.25", FCVAR_ARCHIVE,
	"Vertical head-follow trigger relative to HUD half-height: 1 is the edge, 1.25 allows 25 percent extra above/below.",
	true, 1.0f, true, 3.0f );
ConVar vr_mouse_recenter_release_threshold( "vr_mouse_recenter_release_threshold", "1.0", FCVAR_ARCHIVE,
	"Net mouse aim movement in degrees needed to release head-follow. Accumulation resets after 0.2 seconds without input. Zero restores immediate release.",
	true, 0.0f, true, 20.0f );

ConVar vr_moveaim_reticle_yaw_limit        ( "vr_moveaim_reticle_yaw_limit",        "10", FCVAR_ARCHIVE, "Beyond this number of degrees, the mouse drags the torso" );
ConVar vr_moveaim_reticle_pitch_limit      ( "vr_moveaim_reticle_pitch_limit",      "30", FCVAR_ARCHIVE, "Beyond this number of degrees, the mouse clamps" );
// Note these are scaled by the zoom factor.
ConVar vr_moveaim_reticle_yaw_limit_zoom   ( "vr_moveaim_reticle_yaw_limit_zoom",   "0", FCVAR_ARCHIVE, "Beyond this number of degrees, the mouse drags the torso" );
ConVar vr_moveaim_reticle_pitch_limit_zoom ( "vr_moveaim_reticle_pitch_limit_zoom", "-1", FCVAR_ARCHIVE, "Beyond this number of degrees, the mouse clamps" );

// This are somewhat obsolete.
ConVar vr_aim_yaw_offset( "vr_aim_yaw_offset", "90", 0, "This value is added to Yaw when returning the vehicle aim angles to Source." );

ConVar vr_stereo_swap_eyes ( "vr_stereo_swap_eyes", "0", 0, "1=swap eyes." );

// Useful for debugging wacky-projection problems, separate from multi-rendering problems.
ConVar vr_stereo_mono_set_eye ( "vr_stereo_mono_set_eye", "0", 0, "0=off, Set all eyes to 1=left, 2=right, 3=middle eye" );

// Useful for examining anims, etc.
ConVar vr_debug_remote_cam( "vr_debug_remote_cam", "0" );
ConVar vr_debug_remote_cam_pos_x( "vr_debug_remote_cam_pos_x", "150.0" );
ConVar vr_debug_remote_cam_pos_y( "vr_debug_remote_cam_pos_y", "0.0" );
ConVar vr_debug_remote_cam_pos_z( "vr_debug_remote_cam_pos_z", "0.0" );
ConVar vr_debug_remote_cam_target_x( "vr_debug_remote_cam_target_x", "0.0" );
ConVar vr_debug_remote_cam_target_y( "vr_debug_remote_cam_target_y", "0.0" );
ConVar vr_debug_remote_cam_target_z( "vr_debug_remote_cam_target_z", "-50.0" );

ConVar vr_translation_limit( "vr_translation_limit", "10.0", 0, "How far the in-game head will translate before being clamped." );

// HUD config values
ConVar vr_render_hud_in_world( "vr_render_hud_in_world", "1" );
ConVar vr_hud_max_fov( "vr_hud_max_fov", "60", FCVAR_ARCHIVE, "Max FOV of the HUD" );
ConVar vr_hud_forward( "vr_hud_forward", "500", FCVAR_ARCHIVE, "Apparent distance of the HUD in inches" );
ConVar vr_menu_scale( "vr_menu_scale", "1.25", FCVAR_ARCHIVE,
	"Apparent size of VR menus. 1 restores the original size; does not change rendering resolution or gameplay aim.",
	true, 0.5f, true, 2.0f );
ConVar vr_hud_display_ratio( "vr_hud_display_ratio", "0.95", FCVAR_ARCHIVE );
ConVar vr_hud_never_overlay( "vr_hud_never_overlay", "0" );

ConVar vr_hud_axis_lock_to_world( "vr_hud_axis_lock_to_world", "0", FCVAR_ARCHIVE, "Bitfield - locks HUD axes to the world - 0=pitch, 1=yaw, 2=roll" );

// Default distance clips through rocketlauncher, heavy's body, etc.
ConVar vr_projection_znear_multiplier( "vr_projection_znear_multiplier", "0.3", 0, "Allows moving the ZNear plane to deal with body clipping" );

// Should the viewmodel (weapon) translate with the HMD, or remain fixed to the in-world body (but still rotate with the head)? Purely a graphics effect - no effect on actual bullet aiming.
// Has no effect in aim modes where aiming is not controlled by the head.
ConVar vr_viewmodel_translate_with_head ( "vr_viewmodel_translate_with_head", "0", 0, "1=translate the viewmodel with the head motion." );

ConVar vr_zoom_multiplier ( "vr_zoom_multiplier", "2.0", FCVAR_ARCHIVE, "When zoomed, how big is the scope on your HUD?" );
ConVar vr_zoom_scope_scale ( "vr_zoom_scope_scale", "6.0", 0, "Something to do with the default scope HUD overlay size." );		// Horrible hack - should work out the math properly, but we need to ship.


ConVar vr_viewmodel_offset_forward( "vr_viewmodel_offset_forward", "-8", 0 );
ConVar vr_viewmodel_offset_forward_large( "vr_viewmodel_offset_forward_large", "-15", 0 );

ConVar vr_force_windowed ( "vr_force_windowed", "0", FCVAR_ARCHIVE );
ConVar vr_first_person_uses_world_model ( "vr_first_person_uses_world_model", "1", 0, "Causes the third person model to be drawn instead of the view model" );

// --------------------------------------------------------------------
// Purpose: Cycle through the aim & move modes.
// --------------------------------------------------------------------
void CC_VR_Cycle_Aim_Move_Mode ( const CCommand& args )
{
	int hmmCurrentMode = vr_moveaim_mode.GetInt();
	if ( g_ClientVirtualReality.CurrentlyZoomed() )
	{
		hmmCurrentMode = vr_moveaim_mode_zoom.GetInt();
	}

	hmmCurrentMode++;
	if ( hmmCurrentMode >= HMM_LAST )
	{
		hmmCurrentMode = 0;
	}

	if ( g_ClientVirtualReality.CurrentlyZoomed() )
	{
		vr_moveaim_mode_zoom.SetValue ( hmmCurrentMode );
		Warning ( "Headtrack mode (zoomed) %d\n", hmmCurrentMode );
	}
	else
	{
		vr_moveaim_mode.SetValue ( hmmCurrentMode );
		Warning ( "Headtrack mode %d\n", hmmCurrentMode );
	}
}
static ConCommand vr_cycle_aim_move_mode("vr_cycle_aim_move_mode", CC_VR_Cycle_Aim_Move_Mode, "Cycle through the aim & move modes." );


// --------------------------------------------------------------------
// Purpose:  Switch to/from VR mode.
// --------------------------------------------------------------------
CON_COMMAND( vr_activate, "Switch to VR mode" )
{
	g_ClientVirtualReality.Activate();
}
CON_COMMAND( vr_deactivate, "Switch from VR mode to normal mode" )
{
	g_ClientVirtualReality.Deactivate();
}
CON_COMMAND( vr_toggle, "Toggles VR mode" )
{
	if( g_pSourceVR )
	{
		if( g_pSourceVR->ShouldRunInVR() )
			g_ClientVirtualReality.Deactivate();
		else
			g_ClientVirtualReality.Activate();
	}
	else
	{
		Msg( "VR Mode is not enabled.\n" );
	}
}


// --------------------------------------------------------------------
// Purpose: Returns true if the matrix is orthonormal
// --------------------------------------------------------------------
bool IsOrthonormal ( VMatrix Mat, float fTolerance )
{
	float LenFwd = Mat.GetForward().Length();
	float LenUp = Mat.GetUp().Length();
	float LenLeft = Mat.GetLeft().Length();
	float DotFwdUp = Mat.GetForward().Dot ( Mat.GetUp() );
	float DotUpLeft = Mat.GetUp().Dot ( Mat.GetLeft() );
	float DotLeftFwd = Mat.GetLeft().Dot ( Mat.GetForward() );
	if ( fabsf ( LenFwd - 1.0f ) > fTolerance )
	{
		return false;
	}
	if ( fabsf ( LenUp - 1.0f ) > fTolerance )
	{
		return false;
	}
	if ( fabsf ( LenLeft - 1.0f ) > fTolerance )
	{
		return false;
	}
	if ( fabsf ( DotFwdUp ) > fTolerance )
	{
		return false;
	}
	if ( fabsf ( DotUpLeft ) > fTolerance )
	{
		return false;
	}
	if ( fabsf ( DotLeftFwd ) > fTolerance )
	{
		return false;
	}
	return true;
}


// --------------------------------------------------------------------
// Purpose: Computes the FOV from the projection matrix
// --------------------------------------------------------------------
void CalcFovFromProjection ( float *pFov, const VMatrix &proj )
{
	// The projection matrices should be of the form:
	// p0  0   z1 p1 
	// 0   p2  z2 p3
	// 0   0   z3 1
	// (p0 = X fov, p1 = X offset, p2 = Y fov, p3 = Y offset )
	// TODO: cope with more complex projection matrices?
	float xscale  = proj.m[0][0];
	Assert ( proj.m[0][1] == 0.0f );
	float xoffset = proj.m[0][2];
	Assert ( proj.m[0][3] == 0.0f );
	Assert ( proj.m[1][0] == 0.0f );
	float yscale  = proj.m[1][1];
	float yoffset = proj.m[1][2];
	Assert ( proj.m[1][3] == 0.0f );
	// Row 2 determines Z-buffer values - don't care about those for now.
	Assert ( proj.m[3][0] == 0.0f );
	Assert ( proj.m[3][1] == 0.0f );
	Assert ( proj.m[3][2] == -1.0f );
	Assert ( proj.m[3][3] == 0.0f );

	/*
	// The math here:
	// A view-space vector (x,y,z,1) is transformed by the projection matrix
	// / xscale   0     xoffset  0 \
	// |    0   yscale  yoffset  0 |
	// |    ?     ?        ?     ? |
	// \    0     0       -1     0 /
	//
	// Then the result is normalized (i.e. divide by w) and the result clipped to the [-1,+1] unit cube.
	// (ignore Z for now, and the clipping is slightly different).
	// So, we want to know what vectors produce a clip value of -1 and +1 in each direction, e.g. in the X direction:
	//    +-1 = ( xscale*x + xoffset*z ) / (-1*z)
	//        = xscale*(x/z) + xoffset            (I flipped the signs of both sides)
	// => (+-1 - xoffset)/xscale = x/z
	// ...and x/z is tan(theta), and theta is the half-FOV.
	*/

	float fov_px = 2.0f * RAD2DEG ( atanf ( fabsf ( (  1.0f - xoffset ) / xscale ) ) );
	float fov_nx = 2.0f * RAD2DEG ( atanf ( fabsf ( ( -1.0f - xoffset ) / xscale ) ) );
	float fov_py = 2.0f * RAD2DEG ( atanf ( fabsf ( (  1.0f - yoffset ) / yscale ) ) );
	float fov_ny = 2.0f * RAD2DEG ( atanf ( fabsf ( ( -1.0f - yoffset ) / yscale ) ) );

	*pFov = Max ( Max ( fov_px, fov_nx ), Max ( fov_py, fov_ny ) );
	// FIXME: hey you know, I could do the Max() series before I call all those expensive atanf()s...
}


// --------------------------------------------------------------------
// construction/destruction
// --------------------------------------------------------------------
CClientVirtualReality::CClientVirtualReality()
{
	m_PlayerTorsoOrigin.Init();
	m_PlayerTorsoAngle.Init();
	m_WorldFromWeapon.Identity();
	m_WorldFromMidEye.Identity();
	
	m_bOverrideTorsoAngle = false;
	m_OverrideTorsoOffset.Init();

	// Also reset our model of the player's torso orientation
	m_PlayerTorsoAngle.Init ( 0.0f, 0.0f, 0.0f );

	m_WorldZoomScale = 1.0f;
	m_hmmMovementActual = HMM_SHOOTFACE_MOVEFACE;
	m_iAlignTorsoAndViewToWeaponCountdown = 0;

	m_rtLastMotionSample = 0;
	m_bMotionUpdated = false;
	m_bAimFollowingHead = false;
	m_flScopedMousePitch = 0.0f;
	m_AimFollowMouseAccum.Init();
	m_flAimFollowLastMouseTime = 0;
	m_LastAimFollowViewAngle.Init();

#if defined( USE_SDL )
    m_nNonVRSDLDisplayIndex = 0;
#endif
}

CClientVirtualReality::~CClientVirtualReality()
{
}


// --------------------------------------------------------------------
// Purpose: 
// --------------------------------------------------------------------
bool			CClientVirtualReality::Connect( CreateInterfaceFn factory )
{
	if ( !factory )
		return false;

	if ( !BaseClass::Connect( factory ) )
		return false;

	return true;
}


// --------------------------------------------------------------------
// Purpose: 
// --------------------------------------------------------------------
void			CClientVirtualReality::Disconnect()
{
	BaseClass::Disconnect();
}


// --------------------------------------------------------------------
// Purpose: 
// --------------------------------------------------------------------
void *			CClientVirtualReality::QueryInterface( const char *pInterfaceName )
{
	CreateInterfaceFn factory = Sys_GetFactoryThis();	// This silly construction is necessary
	return factory( pInterfaceName, NULL );				// to prevent the LTCG compiler from crashing.
}


// --------------------------------------------------------------------
// Purpose: 
// --------------------------------------------------------------------
InitReturnVal_t	CClientVirtualReality::Init()
{
	InitReturnVal_t nRetVal = BaseClass::Init();
	if ( nRetVal != INIT_OK )
		return nRetVal;

	return INIT_OK;
}


// --------------------------------------------------------------------
// Purpose: 
// --------------------------------------------------------------------
void			CClientVirtualReality::Shutdown()
{
	m_InWorldUIMaterial.Shutdown();
	m_InWorldUIOpaqueMaterial.Shutdown();
	m_BlackMaterial.Shutdown();
	m_MirrorMaterial.Shutdown();
	BaseClass::Shutdown();
}


// --------------------------------------------------------------------
// Purpose: Draws the main menu in Stereo
// --------------------------------------------------------------------
void CClientVirtualReality::UpdateMenuViewport()
{
	if ( !UseVR() || !CommandLine()->CheckParm( "-openvr" ) )
		return;
	int width, height;
	const bool inMap = engine->IsInGame();
	// GameUI retains its desktop-sized layout across map transitions. The
	// in-game UI target follows the same video mode; only the HUD child canvas
	// uses a narrower 4:3 layout, not the global VGUI surface.
	materials->GetBackBufferDimensions( width, height );
	if ( width <= 0 || height <= 0 )
		return;
	int previousWidth, previousHeight;
	vgui::surface()->GetScreenSize( previousWidth, previousHeight );
	if ( previousWidth != width || previousHeight != height )
	{
		// Set this before VGUI layout/painting, never midway through a capture.
		g_pMatSystemSurface->ForceScreenSizeOverride( true, width, height );
		// Updating the override alone leaves GameUI's cached scheme/layout at
		// the activation resolution. Notify VGUI only on actual size changes.
		vgui::surface()->OnScreenSizeChanged( previousWidth, previousHeight );
		vgui::VPANEL gameUIRoot = enginevgui->GetPanel( PANEL_GAMEUIDLL );
		if ( gameUIRoot ) vgui::ipanel()->SetSize( gameUIRoot, width, height );
		vgui::VPANEL root = enginevgui->GetPanel( PANEL_CLIENTDLL );
		if ( root ) vgui::ipanel()->SetSize( root, width, height );
		root = enginevgui->GetPanel( PANEL_CLIENTDLL_TOOLS );
		if ( root ) vgui::ipanel()->SetSize( root, width, height );
	}
	// Model previews must use the same full desktop viewport as the surrounding
	// 2D menu. In-map rendering retains its existing target/viewport handling.
	if ( !inMap )
		g_pMatSystemSurface->SetFullscreenViewportAndRenderTarget( 0, 0, width, height, NULL );
}

void CClientVirtualReality::DrawMainMenu()
{
	// have to draw the UI in stereo via the render texture or it won't fuse properly

	// Draw it into the render target first
	ITexture *pTexture = materials->FindTexture( "_rt_OpenVRMenu", NULL, false );
	Assert( pTexture );
	if( !pTexture) 
		return;

	CMatRenderContextPtr pRenderContext( materials );
	int viewActualWidth = pTexture->GetActualWidth();
	int viewActualHeight = pTexture->GetActualHeight();

	// VGUI always renders at its own stable logical resolution. HMD resolution
	// belongs to the eye targets and must not alter panel layout or model views.
	// GameUI has already produced the authoritative menu image on the companion
	// backbuffer by FRAME_RENDER_END. Capturing it avoids a second VGui_Paint,
	// which otherwise advances model panels twice and produces visible temporal
	// disagreement/flicker in the headset.
	{
		// GameUI has already painted the companion buffer. Reuse that image
		// instead of traversing every VGUI panel a second time late in the frame.
		// The second traversal was especially harmful to model panels and caused
		// their two renders to fight each other in the headset.
		int backBufferWidth = 0, backBufferHeight = 0;
		materials->GetBackBufferDimensions( backBufferWidth, backBufferHeight );
		if ( backBufferWidth <= 0 || backBufferHeight <= 0 )
			return;
		Rect_t sourceRect = { 0, 0, backBufferWidth, backBufferHeight };
		Rect_t destinationRect = { 0, 0, viewActualWidth, viewActualHeight };
		// Model panels render through their own off-screen target. Do not inherit
		// that target as the copy source when a loadout/model preview was the last
		// UI operation this frame; explicitly select the companion backbuffer.
		pRenderContext->PushRenderTargetAndViewport( NULL, 0, 0, backBufferWidth, backBufferHeight );
		pRenderContext->CopyRenderTargetToTextureEx( pTexture, 0, &sourceRect, &destinationRect );
		pRenderContext->PopRenderTargetAndViewport();
		pRenderContext->Flush( true );
	}
	int leftX, leftY, leftW, leftH, rightX, rightY, rightW, rightH;
	g_pSourceVR->GetViewportBounds( ISourceVirtualReality::VREye_Left, &leftX, &leftY, &leftW, &leftH );
	g_pSourceVR->GetViewportBounds( ISourceVirtualReality::VREye_Right, &rightX, &rightY, &rightW, &rightH );


	// render the main view
	CViewSetup viewEye[STEREO_EYE_MAX];
	viewEye[ STEREO_EYE_MONO ].zNear = 0.1;
	viewEye[ STEREO_EYE_MONO ].zFar = 10000.f;
	viewEye[ STEREO_EYE_MONO ].angles.Init();
	viewEye[ STEREO_EYE_MONO ].origin.Zero();
	viewEye[ STEREO_EYE_MONO ].x = viewEye[ STEREO_EYE_MONO ].m_nUnscaledX =  leftX;
	viewEye[ STEREO_EYE_MONO ].y = viewEye[ STEREO_EYE_MONO ].m_nUnscaledY = leftY;
	viewEye[ STEREO_EYE_MONO ].width = viewEye[ STEREO_EYE_MONO ].m_nUnscaledWidth = leftW;
	viewEye[ STEREO_EYE_MONO ].height = viewEye[ STEREO_EYE_MONO ].m_nUnscaledHeight = leftH;

	viewEye[STEREO_EYE_LEFT] = viewEye[STEREO_EYE_RIGHT] = viewEye[ STEREO_EYE_MONO ] ;
	viewEye[STEREO_EYE_LEFT].m_eStereoEye = STEREO_EYE_LEFT;
	viewEye[STEREO_EYE_RIGHT].x = rightX;
	viewEye[STEREO_EYE_RIGHT].y = rightY;
	viewEye[STEREO_EYE_RIGHT].m_eStereoEye = STEREO_EYE_RIGHT;

	// let sourcevr.dll tell us where to put the cameras
	ProcessCurrentTrackingState( 0 );
	Vector vViewModelOrigin;
	QAngle qViewModelAngles;
	OverrideView( &viewEye[ STEREO_EYE_MONO ] , &vViewModelOrigin, &qViewModelAngles, HMM_NOOVERRIDE );
	g_ClientVirtualReality.OverrideStereoView( &viewEye[ STEREO_EYE_MONO ] , &viewEye[STEREO_EYE_LEFT], &viewEye[STEREO_EYE_RIGHT] );

	// render both eyes
	for( int nView = STEREO_EYE_LEFT; nView <= STEREO_EYE_RIGHT; nView++ )
	{
		CMatRenderContextPtr pRenderContextMat( materials );
		PIXEvent pixEvent( pRenderContextMat, nView == STEREO_EYE_LEFT ? "left eye" : "right eye" );

		ITexture *pColor = g_pSourceVR->GetRenderTarget( (ISourceVirtualReality::VREye)(nView-1), ISourceVirtualReality::RT_Color );
		ITexture *pDepth = g_pSourceVR->GetRenderTarget( (ISourceVirtualReality::VREye)(nView-1), ISourceVirtualReality::RT_Depth );
		render->Push3DView( viewEye[nView], VIEW_CLEAR_DEPTH|VIEW_CLEAR_COLOR, pColor, NULL, pDepth );
		RenderHUDQuad( false,  false );
		render->PopView( NULL );
		PostProcessFrame( (StereoEye_t)nView );

		OverlayHUDQuadWithUndistort( viewEye[nView], true, true, false );
	}

}


// --------------------------------------------------------------------
// Purpose:
//		Offset the incoming view appropriately.
//		Set up the "middle eye" from that.
// --------------------------------------------------------------------
bool CClientVirtualReality::OverrideView ( CViewSetup *pViewMiddle, Vector *pViewModelOrigin, QAngle *pViewModelAngles, HeadtrackMovementMode_t hmmMovementOverride )
{
	if( !UseVR() )
	{
		return false;
	}

	if ( hmmMovementOverride == HMM_NOOVERRIDE )
	{
		if ( CurrentlyZoomed() )
		{
			m_hmmMovementActual = static_cast<HeadtrackMovementMode_t>( vr_moveaim_mode_zoom.GetInt() );
			// A bounded, world-fixed mouse aim can leave the magnified frustum
			// after a tiny head turn. Scoped mode 3 uses the existing head-aim
			// path so camera, weapon direction and scope remain together.
			if ( m_hmmMovementActual == HMM_SHOOTBOUNDEDMOUSE_LOOKFACE_MOVEFACE )
				m_hmmMovementActual = HMM_SHOOTFACE_MOVEFACE;
		}
		else
		{
			m_hmmMovementActual = static_cast<HeadtrackMovementMode_t>( vr_moveaim_mode.GetInt() );
		}
	}
	else
	{
		m_hmmMovementActual = hmmMovementOverride;
	}


	// Incoming data may or may not be useful - it is the origin and aim of the "player", i.e. where bullets come from.
	// In some modes it is an independent thing, guided by the mouse & keyboard = useful.
	// In other modes it's just where the HMD was pointed last frame, modified slightly by kbd+mouse.
	// In those cases, we should use our internal reference (which keeps track thanks to OverridePlayerMotion)
	QAngle originalMiddleAngles = pViewMiddle->angles;
	Vector originalMiddleOrigin = pViewMiddle->origin;

	// Figure out the in-game "torso" concept, which corresponds to the player's physical torso.
	m_PlayerTorsoOrigin = pViewMiddle->origin;

	// Ignore what was passed in - it's just the direction the weapon is pointing, which was determined by last frame's HMD orientation!
	// Instead use our cached value.
	QAngle torsoAngles = m_PlayerTorsoAngle;
	if ( CurrentlyZoomed() && vr_scope_mouse_pitch.GetBool() &&
		( m_hmmMovementActual == HMM_SHOOTFACE_MOVEFACE || m_hmmMovementActual == HMM_SHOOTFACE_MOVETORSO ) )
	{
		torsoAngles[PITCH] += m_flScopedMousePitch;
	}
	else
	{
		// Scope-only offset must not leak into ordinary mouse aiming.
		m_flScopedMousePitch = 0.0f;
	}

	VMatrix worldFromTorso;
	worldFromTorso.SetupMatrixOrgAngles( m_PlayerTorsoOrigin, torsoAngles );

	//// Scale translation e.g. to allow big in-game leans with only a small head movement.
	//// Clamp HMD movement to a reasonable amount to avoid wallhacks, vis problems, etc.
	float limit = vr_translation_limit.GetFloat();
	VMatrix matMideyeZeroFromMideyeCurrent = g_pSourceVR->GetMideyePose();
	Vector viewTranslation = matMideyeZeroFromMideyeCurrent.GetTranslation();
	if ( viewTranslation.IsLengthGreaterThan ( limit ) )
	{
		viewTranslation.NormalizeInPlace();
		viewTranslation *= limit;
		matMideyeZeroFromMideyeCurrent.SetTranslation( viewTranslation );
	}

	// Now figure out the three principal matrices: m_TorsoFromMideye, m_WorldFromMidEye, m_WorldFromWeapon
	// m_TorsoFromMideye is done so that OverridePlayerMotion knows what to do with WASD.

	if ( IsVRThirdPersonTauntCamera() )
	{
		// TF already calculated the orbit, distance and collision-adjusted origin.
		// Add tracking to that camera without feeding it back into mouse input.
		worldFromTorso.SetupMatrixOrgAngles( originalMiddleOrigin, originalMiddleAngles );
		m_TorsoFromMideye = matMideyeZeroFromMideyeCurrent;
		m_WorldFromMidEye = worldFromTorso * matMideyeZeroFromMideyeCurrent;
		m_WorldFromWeapon = worldFromTorso;
	}
	else switch ( m_hmmMovementActual )
	{
	case HMM_SHOOTFACE_MOVEFACE:
	case HMM_SHOOTFACE_MOVETORSO:
		// Aim point is down your nose, i.e. same as the view angles.
		m_TorsoFromMideye = matMideyeZeroFromMideyeCurrent;
		m_WorldFromMidEye = worldFromTorso * matMideyeZeroFromMideyeCurrent;
		m_WorldFromWeapon = m_WorldFromMidEye;
		break;

	case HMM_SHOOTBOUNDEDMOUSE_LOOKFACE_MOVEFACE:
	case HMM_SHOOTBOUNDEDMOUSE_LOOKFACE_MOVEMOUSE:
	case HMM_SHOOTMOUSE_MOVEFACE:
	case HMM_SHOOTMOVEMOUSE_LOOKFACE:
		// Aim point is independent of view - leave it as it was, just copy it into m_WorldFromWeapon for our use.
		m_TorsoFromMideye = matMideyeZeroFromMideyeCurrent;
		m_WorldFromMidEye = worldFromTorso * matMideyeZeroFromMideyeCurrent;
		m_WorldFromWeapon.SetupMatrixOrgAngles( originalMiddleOrigin, originalMiddleAngles );
		break;

	case HMM_SHOOTMOVELOOKMOUSE:
		// HMD is ignored completely, mouse does everything.
		m_PlayerTorsoAngle = originalMiddleAngles;

		worldFromTorso.SetupMatrixOrgAngles( m_PlayerTorsoOrigin, originalMiddleAngles );

		m_TorsoFromMideye.Identity();
		m_WorldFromMidEye = worldFromTorso;
		m_WorldFromWeapon = worldFromTorso;
		break;

	case HMM_SHOOTMOVELOOKMOUSEFACE:
		// mouse does everything, and then we add head tracking on top of that
		worldFromTorso = worldFromTorso * matMideyeZeroFromMideyeCurrent; 

		m_TorsoFromMideye = matMideyeZeroFromMideyeCurrent;
		m_WorldFromWeapon = worldFromTorso;
		m_WorldFromMidEye = worldFromTorso;
		break;

	default: Assert ( false ); break;
	}

	// Finally convert back to origin+angles that the game understands.
	pViewMiddle->origin = m_WorldFromMidEye.GetTranslation();
	VectorAngles ( m_WorldFromMidEye.GetForward(), m_WorldFromMidEye.GetUp(), pViewMiddle->angles );
	// VectorAngles may represent a small negative HMD pitch as (for example)
	// 353 degrees. TF's replicated eye-angle sendprops use the signed Source
	// ranges, so leaving that representation intact floods the console with
	// clamping warnings every frame and can make loading appear frozen.
	pViewMiddle->angles[PITCH] = AngleNormalize( pViewMiddle->angles[PITCH] );
	pViewMiddle->angles[YAW] = AngleNormalize( pViewMiddle->angles[YAW] );
	pViewMiddle->angles[ROLL] = AngleNormalize( pViewMiddle->angles[ROLL] );
	pViewMiddle->angles[PITCH] = clamp( pViewMiddle->angles[PITCH], -89.0f, 89.0f );

	*pViewModelAngles = pViewMiddle->angles;
	if ( vr_viewmodel_translate_with_head.GetBool() )
	{
		*pViewModelOrigin = pViewMiddle->origin;
	}
	else
	{
		*pViewModelOrigin = originalMiddleOrigin;
	}

	m_WorldFromMidEyeNoDebugCam = m_WorldFromMidEye;
	if ( vr_debug_remote_cam.GetBool() )
	{
		Vector vOffset ( vr_debug_remote_cam_pos_x.GetFloat(), vr_debug_remote_cam_pos_y.GetFloat(), vr_debug_remote_cam_pos_z.GetFloat() );
		Vector vLookat ( vr_debug_remote_cam_target_x.GetFloat(), vr_debug_remote_cam_target_y.GetFloat(), vr_debug_remote_cam_target_z.GetFloat() );
		pViewMiddle->origin += vOffset;
		Vector vView = vLookat - vOffset;
		VectorAngles ( vView, m_WorldFromMidEye.GetUp(), pViewMiddle->angles );

		m_WorldFromMidEye.SetupMatrixOrgAngles( pViewMiddle->origin, pViewMiddle->angles );

		m_TorsoFromMideye.Identity();
	}

	// set the near clip plane so the local player clips less
	pViewMiddle->zNear *= vr_projection_znear_multiplier.GetFloat();

	return true;
}


// --------------------------------------------------------------------
// Purpose:
//		In some aim/move modes, the HUD aim reticle lags because it's
//		using slightly stale data. This will feed it the newest data. 
// --------------------------------------------------------------------
bool CClientVirtualReality::OverrideWeaponHudAimVectors ( Vector *pAimOrigin, Vector *pAimDirection )
{
	if( !UseVR() )
	{
		return false;
	}

	Assert ( pAimOrigin != NULL );
	Assert ( pAimDirection != NULL );

	// So give it some nice high-fps numbers, not the low-fps ones we get from the game.
	*pAimOrigin = m_WorldFromWeapon.GetTranslation();
	*pAimDirection = m_WorldFromWeapon.GetForward();

	return true;
}


// --------------------------------------------------------------------
// Purpose:
//		Set up the left and right eyes from the middle eye if stereo is on.
//		Advise calling soonish after OverrideView().
// --------------------------------------------------------------------
bool CClientVirtualReality::OverrideStereoView( CViewSetup *pViewMiddle, CViewSetup *pViewLeft, CViewSetup *pViewRight  )
{
	// Everything in here is in Source coordinate space.
	if( !UseVR() )
	{
		return false;
	}

	VMatrix matOffsetLeft = g_pSourceVR->GetMidEyeFromEye( ISourceVirtualReality::VREye_Left );
	VMatrix matOffsetRight = g_pSourceVR->GetMidEyeFromEye( ISourceVirtualReality::VREye_Right );

	// Move eyes to IPD positions.
	VMatrix worldFromLeftEye  = m_WorldFromMidEye * matOffsetLeft;
	VMatrix worldFromRightEye = m_WorldFromMidEye * matOffsetRight;

	Assert ( IsOrthonormal ( worldFromLeftEye, 0.001f ) );
	Assert ( IsOrthonormal ( worldFromRightEye, 0.001f ) );

	// Finally convert back to origin+angles.
	MatrixAngles( worldFromLeftEye.As3x4(),  pViewLeft->angles, pViewLeft->origin );
	MatrixAngles( worldFromRightEye.As3x4(),  pViewRight->angles, pViewRight->origin );

	// Find the projection matrices.

	// TODO: this isn't the fastest thing in the world. Cache them?
	float headtrackFovScale = m_WorldZoomScale;
	pViewLeft->m_bViewToProjectionOverride = true;
	pViewRight->m_bViewToProjectionOverride = true;
	g_pSourceVR->GetEyeProjectionMatrix (  &pViewLeft->m_ViewToProjection, ISourceVirtualReality::VREye_Left,  pViewMiddle->zNear, pViewMiddle->zFar, 1.0f/headtrackFovScale );
	g_pSourceVR->GetEyeProjectionMatrix ( &pViewRight->m_ViewToProjection, ISourceVirtualReality::VREye_Right, pViewMiddle->zNear, pViewMiddle->zFar, 1.0f/headtrackFovScale );

	// And bodge together some sort of average for our cyclops friends.
	pViewMiddle->m_bViewToProjectionOverride = true;
	for ( int i = 0; i < 4; i++ )
	{
		for ( int j = 0; j < 4; j++ )
		{
			pViewMiddle->m_ViewToProjection.m[i][j] = (pViewLeft->m_ViewToProjection.m[i][j] + pViewRight->m_ViewToProjection.m[i][j] ) * 0.5f;
		}
	}

	switch ( vr_stereo_mono_set_eye.GetInt() )
	{
	case 0:
		// ... nothing.
		break;
	case 1:
		// Override all eyes with left
		*pViewMiddle = *pViewLeft;
		*pViewRight = *pViewLeft;
		pViewRight->m_eStereoEye = STEREO_EYE_RIGHT;
		break;
	case 2:
		// Override all eyes with right
		*pViewMiddle = *pViewRight;
		*pViewLeft = *pViewRight;
		pViewLeft->m_eStereoEye = STEREO_EYE_LEFT;
		break;
	case 3:
		// Override all eyes with middle
		*pViewRight = *pViewMiddle;
		*pViewLeft = *pViewMiddle;
		pViewLeft->m_eStereoEye = STEREO_EYE_LEFT;
		pViewRight->m_eStereoEye = STEREO_EYE_RIGHT;
		break;
	}

	// To make culling work correctly, calculate the widest FOV of each projection matrix.
	CalcFovFromProjection ( &(pViewLeft  ->fov), pViewLeft  ->m_ViewToProjection );
	CalcFovFromProjection ( &(pViewRight ->fov), pViewRight ->m_ViewToProjection );
	CalcFovFromProjection ( &(pViewMiddle->fov), pViewMiddle->m_ViewToProjection );

	// if we don't know the HUD FOV, figure that out now
	if( m_fHudHorizontalFov == 0.f )
	{
		// Figure out the current HUD FOV.
		m_fHudHorizontalFov = pViewLeft->fov * vr_hud_display_ratio.GetFloat();
		if( m_fHudHorizontalFov > vr_hud_max_fov.GetFloat() )
		{
			m_fHudHorizontalFov = vr_hud_max_fov.GetFloat();
		}
	}

	// remember the view angles so we can limit the weapon to something near those
	m_PlayerViewAngle = pViewMiddle->angles;
	m_PlayerViewOrigin = pViewMiddle->origin;



	// Figure out the HUD vectors and frustum.

	// The aspect ratio of the HMD may be something bizarre (e.g. Rift is 640x800), and the pixels may not be square, so don't use that!
	static const float fAspectRatio = 4.f/3.f;
	float fHFOV = m_fHudHorizontalFov;
	float fVFOV = m_fHudHorizontalFov / fAspectRatio;

	const float fHudForward = vr_hud_forward.GetFloat();
	m_fHudHalfWidth = tan( DEG2RAD( fHFOV * 0.5f ) ) * fHudForward * m_WorldZoomScale;
	m_fHudHalfHeight = tan( DEG2RAD( fVFOV * 0.5f ) ) * fHudForward * m_WorldZoomScale;

	QAngle HudAngles;
	switch ( m_hmmMovementActual )
	{
	case HMM_SHOOTFACE_MOVETORSO:
		// Put the HUD in front of the player's torso.
		// This helps keep you oriented about where "forwards" is, which is otherwise surprisingly tricky!
		// TODO: try preserving roll and/or pitch from the view?
		HudAngles = m_PlayerTorsoAngle;
		break;
	case HMM_SHOOTFACE_MOVEFACE:
	case HMM_SHOOTMOUSE_MOVEFACE:
	case HMM_SHOOTMOVEMOUSE_LOOKFACE:
	case HMM_SHOOTMOVELOOKMOUSE:
	case HMM_SHOOTMOVELOOKMOUSEFACE:
	case HMM_SHOOTBOUNDEDMOUSE_LOOKFACE_MOVEFACE:
	case HMM_SHOOTBOUNDEDMOUSE_LOOKFACE_MOVEMOUSE:
		// Put the HUD in front of wherever the player is looking.
		HudAngles = m_PlayerViewAngle;
		break;
	default: Assert ( false ); break;
	}

	// This is a bitfield. A set bit means lock to the world, a clear bit means don't.
	int iVrHudAxisLockToWorld = vr_hud_axis_lock_to_world.GetInt();
	if ( ( iVrHudAxisLockToWorld & (1<<ROLL) ) != 0 )
	{
		HudAngles[ROLL] = 0.0f;
	}
	if ( ( iVrHudAxisLockToWorld & (1<<PITCH) ) != 0 )
	{
		HudAngles[PITCH] = 0.0f;
	}
	if ( ( iVrHudAxisLockToWorld & (1<<YAW) ) != 0 )
	{
		// Locking the yaw to the world is not particularly helpful, so what it actually means is lock it to the weapon.
		QAngle aimAngles;
		MatrixAngles( m_WorldFromWeapon.As3x4(), aimAngles );
		HudAngles[YAW] = aimAngles[YAW];
	}
	m_WorldFromHud.SetupMatrixOrgAngles( m_PlayerViewOrigin, HudAngles );

	// Remember in source X forwards, Y left, Z up.
	// We need to transform to a more conventional X right, Y up, Z backwards before doing the projection.
	VMatrix WorldFromHudView;
	WorldFromHudView./*X vector*/SetForward ( -m_WorldFromHud.GetLeft() );
	WorldFromHudView./*Y vector*/SetLeft    ( m_WorldFromHud.GetUp() );
	WorldFromHudView./*Z vector*/SetUp      ( -m_WorldFromHud.GetForward() );
	WorldFromHudView.SetTranslation         ( m_PlayerViewOrigin );

	VMatrix HudProjection;
	HudProjection.Identity();
	HudProjection.m[0][0] = fHudForward / m_fHudHalfWidth;
	HudProjection.m[1][1] = fHudForward / m_fHudHalfHeight;
	// Z vector is not used/valid, but w is for projection.
	HudProjection.m[3][2] = -1.0f;

	// This will transform a world point into a homogeneous vector that
	//  when projected (i.e. divide by w) maps to HUD space [-1,1]
	m_HudProjectionFromWorld = HudProjection * WorldFromHudView.InverseTR();

	return true;
}


// --------------------------------------------------------------------
// Purpose: Updates player orientation, position and motion according
//			to HMD status.
// --------------------------------------------------------------------
bool CClientVirtualReality::OverridePlayerMotion( float flInputSampleFrametime, const QAngle &oldAngles, const QAngle &curAngles, const Vector &curMotion, QAngle *pNewAngles, Vector *pNewMotion )
{
	Assert ( pNewAngles != NULL );
	Assert ( pNewMotion != NULL );
	*pNewAngles = curAngles;
	*pNewMotion = curMotion;

	if ( !UseVR() )
	{
		return false;
	}


	m_bMotionUpdated = true;
	if ( IsVRThirdPersonTauntCamera() )
	{
		// Preserve standard TF mouse/taunt steering. Neither bounded aim nor
		// head-follow recentering may rewrite the third-person orbit input.
		m_bAimFollowingHead = false;
		m_AimFollowMouseAccum.Init();
		m_flScopedMousePitch = 0.0f;
		m_PlayerTorsoAngle.Init( 0.0f, curAngles[YAW], 0.0f );
		m_WorldFromWeapon.SetupMatrixOrgAngles( m_WorldFromWeapon.GetTranslation(), curAngles );
		m_PlayerLastMovement = curMotion;
		return true;
	}

	// originalAngles tells us what the weapon angles were before whatever mouse, joystick, etc thing changed them - called "old"
	// curAngles holds the new weapon angles after mouse, joystick, etc. applied.
	// We need to compute what weapon angles WE want and return them in *pNewAngles - called "new"



	VMatrix worldFromTorso;

	// Whatever position is already here (set up by OverrideView) needs to be preserved.
	Vector vWeaponOrigin = m_WorldFromWeapon.GetTranslation();
	const QAngle torsoBeforeInput = m_PlayerTorsoAngle;
	bool recenterAim = false;
	const float mouseYaw = AngleDiff( curAngles[YAW], oldAngles[YAW] );
	const float mousePitch = AngleDiff( curAngles[PITCH], oldAngles[PITCH] );
	const bool mouseMoved = fabsf( mouseYaw ) > 0.001f || fabsf( mousePitch ) > 0.001f;
	const double mouseTime = Plat_FloatTime();
	if ( !m_bAimFollowingHead || mouseTime - m_flAimFollowLastMouseTime > 0.2 )
		m_AimFollowMouseAccum.Init();
	const bool viewMoved = fabsf( AngleDiff( m_PlayerViewAngle[YAW], m_LastAimFollowViewAngle[YAW] ) ) > 0.001f ||
		fabsf( AngleDiff( m_PlayerViewAngle[PITCH], m_LastAimFollowViewAngle[PITCH] ) ) > 0.001f;
	m_LastAimFollowViewAngle = m_PlayerViewAngle;
	if ( vr_aim_recenter_enable.GetBool() && !enginevgui->IsGameUIVisible() &&
		!g_pMatSystemSurface->IsCursorVisible() && !CurrentlyZoomed() &&
		m_hmmMovementActual == HMM_SHOOTBOUNDEDMOUSE_LOOKFACE_MOVEFACE ) // Mode 3 only.
	{
		if ( mouseMoved )
		{
			if ( m_bAimFollowingHead )
			{
				// Net displacement lets opposite-direction click jitter cancel,
				// while deliberate slow movement can accumulate across samples.
				m_AimFollowMouseAccum[YAW] += mouseYaw;
				m_AimFollowMouseAccum[PITCH] += mousePitch;
				m_flAimFollowLastMouseTime = mouseTime;
				const float threshold = vr_mouse_recenter_release_threshold.GetFloat();
				const float movementSquared = m_AimFollowMouseAccum[YAW] * m_AimFollowMouseAccum[YAW] +
					m_AimFollowMouseAccum[PITCH] * m_AimFollowMouseAccum[PITCH];
				if ( movementSquared >= threshold * threshold )
				{
					m_bAimFollowingHead = false;
					m_AimFollowMouseAccum.Init();
				}
			}
		}
		else if ( !m_bAimFollowingHead && viewMoved )
		{
			Vector direction, hudPoint;
			AngleVectors( oldAngles, &direction );
			const Vector aimPoint = vWeaponOrigin + direction * MAX_TRACE_LENGTH;
			m_bAimFollowingHead = HudTransform( aimPoint, hudPoint ) != 0 ||
				fabsf( hudPoint.x ) > 1.0f || fabsf( hudPoint.y ) > vr_mouse_recenter_vertical_margin.GetFloat();
		}
		recenterAim = m_bAimFollowingHead;
	}
	else
	{
		m_bAimFollowingHead = false;
		m_AimFollowMouseAccum.Init();
	}

	switch ( m_hmmMovementActual )
	{
	case HMM_SHOOTFACE_MOVEFACE:
	case HMM_SHOOTFACE_MOVETORSO:
		{
			// Figure out what changes were made to the WEAPON by mouse/joystick/etc
			VMatrix worldFromOldWeapon, worldFromCurWeapon;
			worldFromOldWeapon.SetupMatrixAngles( oldAngles );
			worldFromCurWeapon.SetupMatrixAngles( curAngles );

			// Keep torso level; optional scoped pitch is a separate view/aim offset.
			if( !m_bOverrideTorsoAngle )
			{
				m_PlayerTorsoAngle[YAW] += curAngles[YAW] - oldAngles[YAW];
				m_PlayerTorsoAngle[ROLL] = 0.0f;
				m_PlayerTorsoAngle[PITCH] = 0.0f;
			}

			QAngle scopedTorsoAngles = m_PlayerTorsoAngle;
			if ( CurrentlyZoomed() && vr_scope_mouse_pitch.GetBool() )
			{
				if ( !m_bOverrideTorsoAngle )
					m_flScopedMousePitch = clamp( m_flScopedMousePitch + AngleDiff( curAngles[PITCH], oldAngles[PITCH] ), -80.0f, 80.0f );
				scopedTorsoAngles[PITCH] += m_flScopedMousePitch;
			}
			else
			{
				m_flScopedMousePitch = 0.0f;
			}
			worldFromTorso.SetupMatrixAngles( scopedTorsoAngles );

			// Weapon view = mideye view, so apply that to the torso to find the world view direction.
			m_WorldFromWeapon = worldFromTorso * m_TorsoFromMideye;

			// ...and we return this new weapon direction as the player's orientation.
			MatrixAngles( m_WorldFromWeapon.As3x4(), *pNewAngles );

			// Restore the translation.
			m_WorldFromWeapon.SetTranslation ( vWeaponOrigin );
		}
		break;
	case HMM_SHOOTMOVELOOKMOUSEFACE:
	case HMM_SHOOTMOVEMOUSE_LOOKFACE:
	case HMM_SHOOTMOVELOOKMOUSE:
		{
			// The mouse just controls the weapon directly.
			*pNewAngles = curAngles;
			*pNewMotion = curMotion;

			// Move the torso by the yaw angles - torso should not have roll or pitch or you'll make folks ill.
			if( !m_bOverrideTorsoAngle && m_hmmMovementActual != HMM_SHOOTMOVELOOKMOUSEFACE )
			{
				m_PlayerTorsoAngle[YAW] = curAngles[YAW];
				m_PlayerTorsoAngle[ROLL] = 0.0f;
				m_PlayerTorsoAngle[PITCH] = 0.0f;
			}

			// Let every other system know.
			m_WorldFromWeapon.SetupMatrixOrgAngles( vWeaponOrigin, *pNewAngles );
			worldFromTorso.SetupMatrixAngles( m_PlayerTorsoAngle );
		}
		break;
	case HMM_SHOOTBOUNDEDMOUSE_LOOKFACE_MOVEFACE:
	case HMM_SHOOTBOUNDEDMOUSE_LOOKFACE_MOVEMOUSE:
		{
			// The mouse controls the weapon directly.
			*pNewAngles = curAngles;
			*pNewMotion = curMotion;

			float fReticleYawLimit = vr_moveaim_reticle_yaw_limit.GetFloat();
			float fReticlePitchLimit = vr_moveaim_reticle_pitch_limit.GetFloat();

			if ( CurrentlyZoomed() )
			{
				fReticleYawLimit = vr_moveaim_reticle_yaw_limit_zoom.GetFloat() * m_WorldZoomScale;
				fReticlePitchLimit = vr_moveaim_reticle_pitch_limit_zoom.GetFloat() * m_WorldZoomScale;
				if ( fReticleYawLimit > 180.0f )
				{
					fReticleYawLimit = 180.0f;
				}
				if ( fReticlePitchLimit > 180.0f )
				{
					fReticlePitchLimit = 180.0f;
				}
			}

			if ( fReticlePitchLimit >= 0.0f )
			{
				// Clamp pitch to within the limits.
				(*pNewAngles)[PITCH] = Clamp ( curAngles[PITCH], m_PlayerViewAngle[PITCH] - fReticlePitchLimit, m_PlayerViewAngle[PITCH] + fReticlePitchLimit );
			}

			// For yaw the concept here is the torso stays within a set number of degrees of the weapon in yaw.
			// However, with drifty tracking systems (e.g. IMUs) the concept of "torso" is hazy.
			// Really it's just a mechanism to turn the view without moving the head - its absolute
			// orientation is not that useful.
			// So... if the mouse is to the right greater than the chosen angle from the view, and then
			// moves more right, it will drag the torso (and thus the view) right, so it stays on the edge of the view.
			// But if it moves left towards the view, it does no dragging.
			// Note that if the mouse does not move, but the view moves, it will NOT drag at all.
			// This allows people to mouse-aim within their view, but also to flick-turn with the mouse,
			// and to flick-glance with the head.
			if ( fReticleYawLimit >= 0.0f )
			{
				float fViewToWeaponYaw = AngleDiff ( curAngles[YAW], m_PlayerViewAngle[YAW] );
				float fWeaponYawMovement = AngleDiff ( curAngles[YAW], oldAngles[YAW] );
				if ( fViewToWeaponYaw > fReticleYawLimit )
				{
					if ( fWeaponYawMovement > 0.0f )
					{
						m_PlayerTorsoAngle[YAW] += fWeaponYawMovement;
					}
				}
				else if ( fViewToWeaponYaw < -fReticleYawLimit )
				{
					if ( fWeaponYawMovement < 0.0f )
					{
						m_PlayerTorsoAngle[YAW] += fWeaponYawMovement;
					}
				}
			}

			// Let every other system know.
			m_WorldFromWeapon.SetupMatrixOrgAngles( vWeaponOrigin, *pNewAngles );
			worldFromTorso.SetupMatrixAngles( m_PlayerTorsoAngle );
		}
		break;
	case HMM_SHOOTMOUSE_MOVEFACE:
		{
			(*pNewAngles)[PITCH] = clamp( (*pNewAngles)[PITCH], m_PlayerViewAngle[PITCH]-15.f, m_PlayerViewAngle[PITCH]+15.f );

			float fDiff = AngleDiff( (*pNewAngles)[YAW], m_PlayerViewAngle[YAW] );

			if( fDiff > 15.f )
			{
				(*pNewAngles)[YAW] = AngleNormalize( m_PlayerViewAngle[YAW] + 15.f );
				if( !m_bOverrideTorsoAngle )
					m_PlayerTorsoAngle[ YAW ] += fDiff - 15.f;
			}
			else if( fDiff < -15.f )
			{
				(*pNewAngles)[YAW] = AngleNormalize( m_PlayerViewAngle[YAW] - 15.f );
				if( !m_bOverrideTorsoAngle )
					m_PlayerTorsoAngle[ YAW ] += fDiff + 15.f;
			}
			else
			{
				m_PlayerTorsoAngle[ YAW ] += AngleDiff( curAngles[YAW], oldAngles[YAW] ) /2.f;
			}

			m_WorldFromWeapon.SetupMatrixOrgAngles( vWeaponOrigin, *pNewAngles );
			worldFromTorso.SetupMatrixAngles( m_PlayerTorsoAngle );
		}
		break;
	default: Assert ( false ); break;
	}

	if ( recenterAim )
	{
		// Follow the head without turning the torso; mouse input releases this.
		*pNewAngles = m_PlayerViewAngle;
		(*pNewAngles)[PITCH] = clamp( AngleNormalize( (*pNewAngles)[PITCH] ), -89.0f, 89.0f );
		(*pNewAngles)[YAW] = AngleNormalize( (*pNewAngles)[YAW] );
		(*pNewAngles)[ROLL] = 0;
		m_PlayerTorsoAngle = torsoBeforeInput;
		worldFromTorso.SetupMatrixAngles( m_PlayerTorsoAngle );
		m_WorldFromWeapon.SetupMatrixOrgAngles( vWeaponOrigin, *pNewAngles );
	}

	// Figure out player motion.
	switch ( m_hmmMovementActual )
	{
	case HMM_SHOOTBOUNDEDMOUSE_LOOKFACE_MOVEFACE:
		{
			// The motion passed in is meant to be relative to the face, so jimmy it to be relative to the new weapon aim.
			VMatrix mideyeFromWorld = m_WorldFromMidEye.InverseTR();
			VMatrix newMidEyeFromWeapon = mideyeFromWorld * m_WorldFromWeapon;
			newMidEyeFromWeapon.SetTranslation ( Vector ( 0.0f, 0.0f, 0.0f ) );
			*pNewMotion = newMidEyeFromWeapon * curMotion;
		}
		break;
	case HMM_SHOOTFACE_MOVETORSO:
		{
			// The motion passed in is meant to be relative to the torso, so jimmy it to be relative to the new weapon aim.
			VMatrix torsoFromWorld = worldFromTorso.InverseTR();
			VMatrix newTorsoFromWeapon = torsoFromWorld * m_WorldFromWeapon;
			newTorsoFromWeapon.SetTranslation ( Vector ( 0.0f, 0.0f, 0.0f ) );
			*pNewMotion = newTorsoFromWeapon * curMotion;
		}
		break;
	case HMM_SHOOTBOUNDEDMOUSE_LOOKFACE_MOVEMOUSE:
	case HMM_SHOOTMOVELOOKMOUSEFACE:
	case HMM_SHOOTFACE_MOVEFACE:
	case HMM_SHOOTMOUSE_MOVEFACE:
	case HMM_SHOOTMOVEMOUSE_LOOKFACE:
	case HMM_SHOOTMOVELOOKMOUSE:
		// Motion is meant to be relative to the weapon, so we're fine.
		*pNewMotion = curMotion;
		break;
	default: Assert ( false ); break;
	}

	// If the game told us to, recenter the torso yaw to match the weapon
	if ( m_iAlignTorsoAndViewToWeaponCountdown > 0 )
	{
		m_iAlignTorsoAndViewToWeaponCountdown--;

		// figure out the angles from the torso to the head
		QAngle torsoFromHeadAngles;
		MatrixAngles( m_TorsoFromMideye.As3x4(), torsoFromHeadAngles );

		QAngle weaponAngles;
		MatrixAngles( m_WorldFromWeapon.As3x4(), weaponAngles );
		m_PlayerTorsoAngle[ YAW ] = weaponAngles[ YAW ] - torsoFromHeadAngles[ YAW ] ;
		NormalizeAngles( m_PlayerTorsoAngle );
	}

	// remember the motion for stat tracking
	m_PlayerLastMovement = *pNewMotion;



	return true;
}

// --------------------------------------------------------------------
// Purpose: Returns true if the world is zoomed
// --------------------------------------------------------------------
bool CClientVirtualReality::CurrentlyZoomed()
{
	return ( m_WorldZoomScale != 1.0f );
}


// --------------------------------------------------------------------
// Purpose: Tells the headtracker to keep the torso angle of the player
//			fixed at this point until the game tells us something 
//			different.
// --------------------------------------------------------------------
void CClientVirtualReality::OverrideTorsoTransform( const Vector & position, const QAngle & angles )
{
	if( m_iAlignTorsoAndViewToWeaponCountdown > 0 )
	{
		m_iAlignTorsoAndViewToWeaponCountdown--;

		// figure out the angles from the torso to the head
		QAngle torsoFromHeadAngles;
		MatrixAngles( m_TorsoFromMideye.As3x4(), torsoFromHeadAngles );

		// this is how far off the torso we actually set will need to be to keep the current "forward"
		// vector while the torso angle is being overridden.
		m_OverrideTorsoOffset[ YAW ] = -torsoFromHeadAngles[ YAW ];
	}

	m_bOverrideTorsoAngle = true;
	m_OverrideTorsoAngle = angles + m_OverrideTorsoOffset;

	// overriding pitch and roll isn't allowed to avoid making people sick
	m_OverrideTorsoAngle[ PITCH ] = 0;
	m_OverrideTorsoAngle[ ROLL ] = 0;

	NormalizeAngles( m_OverrideTorsoAngle );
	
	m_PlayerTorsoAngle = m_OverrideTorsoAngle;
}


// --------------------------------------------------------------------
// Purpose: Tells the headtracker to resume using its own notion of 
//			where the torso is pointed.
// --------------------------------------------------------------------
void CClientVirtualReality::CancelTorsoTransformOverride()
{
	m_bOverrideTorsoAngle = false;
}


bool CClientVirtualReality::CanOverlayHudQuad()
{
	bool bCanOverlay = true;

	bCanOverlay = bCanOverlay && vr_render_hud_in_world.GetBool();
	bCanOverlay = bCanOverlay && ( ! vr_hud_never_overlay.GetBool() );
	bCanOverlay = bCanOverlay && ( vr_hud_axis_lock_to_world.GetInt() == 0 );
	bCanOverlay = bCanOverlay && ( m_hmmMovementActual != HMM_SHOOTFACE_MOVETORSO );

	return bCanOverlay;
}


// --------------------------------------------------------------------
// Purpose: Returns the bounds in world space where the game should 
//			position the HUD.
// --------------------------------------------------------------------
void CClientVirtualReality::GetHUDBounds( Vector *pViewer, Vector *pUL, Vector *pUR, Vector *pLL, Vector *pLR )
{
	// Enlarge the completed menu image, including its software cursor, without
	// changing VGUI layout/input coordinates or the gameplay aim projection.
	// Gameplay returns to its normal HUD size as soon as the menu closes.
	const bool menuVisible = !engine->IsInGame() || enginevgui->IsGameUIVisible();
	const float menuScale = menuVisible ? vr_menu_scale.GetFloat() : 1.0f;
	Vector vHalfWidth = m_WorldFromHud.GetLeft() * ( -m_fHudHalfWidth * menuScale );
	Vector vHalfHeight = m_WorldFromHud.GetUp() * ( m_fHudHalfHeight * menuScale );
	Vector vHUDOrigin = m_PlayerViewOrigin + m_WorldFromHud.GetForward() * vr_hud_forward.GetFloat();

	*pViewer = m_PlayerViewOrigin;
	*pUL = vHUDOrigin - vHalfWidth + vHalfHeight;
	*pUR = vHUDOrigin + vHalfWidth + vHalfHeight;
	*pLL = vHUDOrigin - vHalfWidth - vHalfHeight;
	*pLR = vHUDOrigin + vHalfWidth - vHalfHeight;
}


// --------------------------------------------------------------------
// Purpose: Renders the HUD in the world.
// --------------------------------------------------------------------
void CClientVirtualReality::RenderHUDQuad( bool bBlackout, bool bTranslucent )
{
	// If we can overlay the HUD directly onto the target later, we'll do that instead (higher image quality).
	if ( CanOverlayHudQuad() )
		return;

	Vector vHead, vUL, vUR, vLL, vLR;
	GetHUDBounds ( &vHead, &vUL, &vUR, &vLL, &vLR );

	CMatRenderContextPtr pRenderContext( materials );

	{
		IMaterial *mymat = NULL;
		if ( bTranslucent )
		{
			if ( !m_InWorldUIMaterial.IsValid() )
				m_InWorldUIMaterial.Init( "vgui/inworldui", TEXTURE_GROUP_VGUI );
			mymat = m_InWorldUIMaterial;
		}
		else
		{
			if ( !m_InWorldUIOpaqueMaterial.IsValid() )
				m_InWorldUIOpaqueMaterial.Init( "vgui/inworldui_opaque", TEXTURE_GROUP_VGUI );
			mymat = m_InWorldUIOpaqueMaterial;
		}
		Assert( !mymat->IsErrorMaterial() );

		// mat_reset_rendertargets recreates _rt_gui after this material may have
		// cached its original texture object.  Bind the current named target
		// explicitly so the quad never samples the stale checker allocation.
		ITexture *currentGUITexture = materials->FindTexture(
			engine->IsInGame() ? "_rt_OpenVRUI" : "_rt_OpenVRMenu", TEXTURE_GROUP_RENDER_TARGET, false );
		bool foundCurrentBaseTexture = false;
		IMaterialVar *currentBaseTextureVar = mymat->FindVar( "$basetexture", &foundCurrentBaseTexture, false );
		if ( currentGUITexture && !currentGUITexture->IsError() && foundCurrentBaseTexture && currentBaseTextureVar )
		{
			currentBaseTextureVar->SetTextureValue( currentGUITexture );
		}

		IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, mymat );

		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_TRIANGLE_STRIP, 2 );

		meshBuilder.Position3fv (vLR.Base() );
		meshBuilder.TexCoord2f( 0, 1, 1 );
		// The inworldui materials enable vertex color/alpha. Initialize every
		// vertex instead of sampling data left in the dynamic mesh by other UI.
		meshBuilder.Color4ub( 255, 255, 255, 255 );
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

		meshBuilder.Position3fv (vLL.Base());
		meshBuilder.TexCoord2f( 0, 0, 1 );
		meshBuilder.Color4ub( 255, 255, 255, 255 );
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

		meshBuilder.Position3fv (vUR.Base());
		meshBuilder.TexCoord2f( 0, 1, 0 );
		meshBuilder.Color4ub( 255, 255, 255, 255 );
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

		meshBuilder.Position3fv (vUL.Base());
		meshBuilder.TexCoord2f( 0, 0, 0 );
		meshBuilder.Color4ub( 255, 255, 255, 255 );
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS | VTX_HAVECOLOR, 1>();

		meshBuilder.End();
		pMesh->Draw();
	}

	if( bBlackout )
	{
		Vector vbUL, vbUR, vbLL, vbLR;
		// "Reflect" the HUD bounds through the viewer to find the ones behind the head.
		vbUL = 2 * vHead - vLR;
		vbUR = 2 * vHead - vLL;
		vbLL = 2 * vHead - vUR;
		vbLR = 2 * vHead - vUL;

		if ( !m_BlackMaterial.IsValid() )
			m_BlackMaterial.Init( "vgui/black", TEXTURE_GROUP_VGUI );
		IMaterial *mymat = m_BlackMaterial;
		IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, mymat );

		// Tube around the outside.
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_TRIANGLE_STRIP, 8 );

		meshBuilder.Position3fv (vLR.Base());
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

		meshBuilder.Position3fv (vbLR.Base() );
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

		meshBuilder.Position3fv (vLL.Base());
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

		meshBuilder.Position3fv (vbLL.Base());
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

		meshBuilder.Position3fv (vUL.Base());
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

		meshBuilder.Position3fv (vbUL.Base());
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

		meshBuilder.Position3fv (vUR.Base());
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

		meshBuilder.Position3fv (vbUR.Base());
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

		meshBuilder.Position3fv (vLR.Base());
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

		meshBuilder.Position3fv (vbLR.Base());
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

		meshBuilder.End();
		pMesh->Draw();

		// Cap behind the viewer.
		meshBuilder.Begin( pMesh, MATERIAL_TRIANGLE_STRIP, 2 );

		meshBuilder.Position3fv (vbUR.Base() );
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

		meshBuilder.Position3fv (vbUL.Base());
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

		meshBuilder.Position3fv (vbLR.Base());
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

		meshBuilder.Position3fv (vbLL.Base());
		meshBuilder.AdvanceVertexF<VTX_HAVEPOS, 0>();

		meshBuilder.End();
		pMesh->Draw();
	}
}


// --------------------------------------------------------------------
// Purpose: Gets the amount of zoom to apply
// --------------------------------------------------------------------
float CClientVirtualReality::GetZoomedModeMagnification()
{
	return m_WorldZoomScale * vr_zoom_scope_scale.GetFloat();
}


// --------------------------------------------------------------------
// Purpose: Does some client-side tracking work and then tells headtrack
//			to do its own work.
// --------------------------------------------------------------------
bool CClientVirtualReality::ProcessCurrentTrackingState( float fGameFOV )
{
	m_WorldZoomScale = 1.0f;
	if ( fGameFOV != 0.0f )
	{
		// To compensate for the lack of pixels on most HUDs, let's grow this a bit.
		// Remember that MORE zoom equals LESS fov!
		fGameFOV *= ( 1.0f / vr_zoom_multiplier.GetFloat() );
		fGameFOV = Min ( fGameFOV, 170.0f );

		// The game has overridden the FOV, e.g. because of a sniper scope. So we need to match this view with whatever actual FOV the HUD has.
		float wantedGameTanfov = tanf ( DEG2RAD ( fGameFOV * 0.5f ) );
		// OK, so now in stereo mode, we're going to also draw an overlay, but that overlay usually covers more of the screen (because in a good HMD usually our actual FOV is much wider)
		float overlayActualPhysicalTanfov = tanf ( DEG2RAD ( m_fHudHorizontalFov * 0.5f ) );
		// Therefore... (remembering that a zoom > 1.0 means you zoom *out*)
		m_WorldZoomScale = wantedGameTanfov / overlayActualPhysicalTanfov;
	}

	return g_pSourceVR->SampleTrackingState( fGameFOV, 0.f /* seconds to predict */ );
}


// --------------------------------------------------------------------
// Purpose: Returns the projection matrix to use for the HUD
// --------------------------------------------------------------------
const VMatrix &CClientVirtualReality::GetHudProjectionFromWorld()
{
	// This matrix will transform a world-space position into a homogenous HUD-space vector.
	// So if you divide x+y by w, you will get the position on the HUD in [-1,1] space.
	return m_HudProjectionFromWorld;
}


// --------------------------------------------------------------------
// Purpose: Returns the aim vector relative to the torso
// --------------------------------------------------------------------
void CClientVirtualReality::GetTorsoRelativeAim( Vector *pPosition, QAngle *pAngles )
{
	MatrixAngles( m_TorsoFromMideye.As3x4(), *pAngles, *pPosition );
	pAngles->y += vr_aim_yaw_offset.GetFloat();
}


// --------------------------------------------------------------------
// Purpose: Returns distance of the HUD in front of the eyes.
// --------------------------------------------------------------------
float CClientVirtualReality::GetHUDDistance()
{
	return vr_hud_forward.GetFloat();
}


// --------------------------------------------------------------------
// Purpose: Returns true if the HUD should be rendered into a render 
//			target and then into the world on a quad.
// --------------------------------------------------------------------
bool CClientVirtualReality::ShouldRenderHUDInWorld()
{
	return UseVR() && vr_render_hud_in_world.GetBool();
}


// --------------------------------------------------------------------
// Purpose: Lets headtrack tweak the view model origin and angles to match 
//			aim angles and handle strange viewmode FOV stuff
// --------------------------------------------------------------------
void CClientVirtualReality::OverrideViewModelTransform( Vector & vmorigin, QAngle & vmangles, bool bUseLargeOverride ) 
{
	Vector vForward, vRight, vUp;
	AngleVectors( vmangles, &vForward, &vRight, &vUp );

	float fForward = bUseLargeOverride ? vr_viewmodel_offset_forward_large.GetFloat() : vr_viewmodel_offset_forward.GetFloat();

	vmorigin += vForward * fForward;
	MatrixAngles( m_WorldFromWeapon.As3x4(), vmangles );
}


// --------------------------------------------------------------------
// Purpose: Tells the head tracker to reset the torso position in case
//			we're on a drifty tracker.
// --------------------------------------------------------------------
void CClientVirtualReality::AlignTorsoAndViewToWeapon()
{
	if( !UseVR() )
		return;

	if( g_pSourceVR->WillDriftInYaw() )
	{
		m_iAlignTorsoAndViewToWeaponCountdown = 2;
	}
}


// --------------------------------------------------------------------
// Purpose: Lets VR do stuff at the very end of the rendering process
// --------------------------------------------------------------------
void CClientVirtualReality::PostProcessFrame( StereoEye_t eEye )
{
	if( !UseVR() )
		return;

	g_pSourceVR->DoDistortionProcessing( eEye == STEREO_EYE_LEFT ? ISourceVirtualReality::VREye_Left : ISourceVirtualReality::VREye_Right );
}


// --------------------------------------------------------------------
// Pastes the HUD directly onto the backbuffer / render target.
// (higher quality than the RenderHUDQuad() path but can't always be used)
// --------------------------------------------------------------------
void CClientVirtualReality::OverlayHUDQuadWithUndistort( const CViewSetup &eyeView, bool bDoUndistort, bool bBlackout, bool bTranslucent )
{
	if ( ! UseVR() )
		return;

	// If we can't overlay the HUD, it will be handled on another path (rendered into the scene with RenderHUDQuad()).
	if ( ! CanOverlayHudQuad() )
		return;

	// Get the position of the HUD quad in world space as used by RenderHUDQuad().  Then convert to a rectangle in normalized
	// device coordinates.

	Vector vHead, vUL, vUR, vLL, vLR;
	GetHUDBounds ( &vHead, &vUL, &vUR, &vLL, &vLR );

	VMatrix worldToView, viewToProjection, worldToProjection, worldToPixels;
	render->GetMatricesForView( eyeView, &worldToView, &viewToProjection, &worldToProjection, &worldToPixels );

	Vector pUL, pUR, pLL, pLR;

	worldToProjection.V3Mul( vUL, pUL );
	worldToProjection.V3Mul( vUR, pUR );
	worldToProjection.V3Mul( vLL, pLL );
	worldToProjection.V3Mul( vLR, pLR );

	float ndcHudBounds[4];
	ndcHudBounds[0] = Min ( Min( pUL.x, pUR.x ), Min( pLL.x, pLR.x ) );
	ndcHudBounds[1] = Min ( Min( pUL.y, pUR.y ), Min( pLL.y, pLR.y ) );
	ndcHudBounds[2] = Max ( Max( pUL.x, pUR.x ), Max( pLL.x, pLR.x ) );
	ndcHudBounds[3] = Max ( Max( pUL.y, pUR.y ), Max( pLL.y, pLR.y ) );

	ISourceVirtualReality::VREye sourceVrEye = ( eyeView.m_eStereoEye == STEREO_EYE_LEFT ) ? ISourceVirtualReality::VREye_Left : ISourceVirtualReality::VREye_Right;

	g_pSourceVR->CompositeHud ( sourceVrEye, ndcHudBounds, bDoUndistort, bBlackout, bTranslucent );
}


// --------------------------------------------------------------------
// Purpose: Switches to VR mode
// --------------------------------------------------------------------
void CClientVirtualReality::Activate()
{

	// we can only do this if a headtrack DLL is loaded
	if( !g_pSourceVR )
		return;

	// These checks don't apply if we're in VR mode because Steam said so.
	if ( !ShouldForceVRActive() )
	{
		// see if VR mode is even enabled
		if ( materials->GetCurrentConfigForVideoCard().m_nVRModeAdapter == -1 )
		{
			Warning( "Enable VR mode in the video options before trying to use it.\n" );
			return;
		}

		// See if we have an actual adapter
		int32 nVRModeAdapter = g_pSourceVR->GetVRModeAdapter();
		if ( nVRModeAdapter == -1 )
		{
			Warning( "Unable to get VRMode adapter from OpenVR. VR mode cannot be enabled. Try restarting and then enabling VR again.\n" );
			return;
		}

		// we can only activate if we've got a VR device
		if ( materials->GetCurrentConfigForVideoCard().m_nVRModeAdapter != nVRModeAdapter )
		{
			Warning( "VR Mode expects adapter %d which is different from %d which we are currently using. Try restarting and enabling VR mode again.\n",
				nVRModeAdapter, materials->GetCurrentConfigForVideoCard().m_nVRModeAdapter );
			engine->ExecuteClientCmd( "mat_enable_vrmode 0\n" );
			return;
		}
	}


	// can't activate twice
	if( UseVR() )
		return;

	// remember where we were
	m_bNonVRWindowed = g_pMaterialSystem->GetCurrentConfigForVideoCard().Windowed();
	vgui::surface()->GetScreenSize( m_nNonVRWidth, m_nNonVRHeight );
#if defined( USE_SDL )
    static ConVarRef sdl_displayindex( "sdl_displayindex" );
    m_nNonVRSDLDisplayIndex = sdl_displayindex.GetInt();
#endif

	if( !g_pSourceVR->Activate() )
	{
		// we couldn't activate, so just punt on this whole thing
		return;
	}

	// general all-game stuff
	engine->ExecuteClientCmd( "mat_reset_rendertargets\n" );

	// game specific VR config
	CUtlString sCmd;
	sCmd.Format( "exec sourcevr_%s.cfg\n", COM_GetModDirectory() );
	engine->ExecuteClientCmd( sCmd.Get() );

    vgui::surface()->SetSoftwareCursor( true );

#if defined(POSIX)
	ConVarRef m_rawinput( "m_rawinput" );
    m_bNonVRRawInput = m_rawinput.GetBool();
    m_rawinput.SetValue( 1 );

	ConVarRef mat_vsync( "mat_vsync" );
	mat_vsync.SetValue( 0 );
#endif

	const bool clientOwnedOpenVR = CommandLine()->CheckParm( "-openvr" ) != NULL;
	int uiWidth = 640, uiHeight = 480;
	if ( clientOwnedOpenVR )
		materials->GetBackBufferDimensions( uiWidth, uiHeight );
	g_pMatSystemSurface->ForceScreenSizeOverride( true, uiWidth, uiHeight );
	int nViewportWidth, nViewportHeight;

	g_pSourceVR->GetViewportBounds( ISourceVirtualReality::VREye_Left, NULL, NULL, &nViewportWidth, &nViewportHeight );
	ITexture *fullscreenTarget = g_pSourceVR->GetRenderTarget( ISourceVirtualReality::VREye_Left, ISourceVirtualReality::RT_Color );
	if ( clientOwnedOpenVR )
	{
		// Keep ordinary TF GameUI rendering on the companion backbuffer. The
		// dedicated DrawMainMenu pass selects _rt_gui only while capturing VR UI;
		// globally redirecting VGUI breaks nested panels and model previews.
		nViewportWidth = uiWidth;
		nViewportHeight = uiHeight;
		fullscreenTarget = NULL;
	}
	g_pMatSystemSurface->SetFullscreenViewportAndRenderTarget( 0, 0, nViewportWidth, nViewportHeight, fullscreenTarget );
	UpdateMenuViewport();

	vgui::ivgui()->SetVRMode( true );

	// we can skip this extra mode change if we've always been in VR mode
	if ( !ShouldForceVRActive() )
	{
		VRRect_t rect;
		if ( g_pSourceVR->GetDisplayBounds( &rect ) )
		{

			// set mode
			char szCmd[256];
			Q_snprintf( szCmd, sizeof(szCmd), "mat_setvideomode %i %i %i\n", rect.nWidth, rect.nHeight, vr_force_windowed.GetBool() ? 1 : 0 );
			engine->ClientCmd_Unrestricted( szCmd );
		}
	}
}


void CClientVirtualReality::Deactivate()
{
	// can't deactivate when we aren't active
	if( !UseVR() )
		return;

	g_pSourceVR->Deactivate();

	g_pMatSystemSurface->ForceScreenSizeOverride(false, 0, 0 );
	g_pMaterialSystem->GetRenderContext()->Viewport( 0, 0, m_nNonVRWidth, m_nNonVRHeight );
	g_pMatSystemSurface->SetFullscreenViewportAndRenderTarget( 0, 0, m_nNonVRWidth, m_nNonVRHeight, NULL );

    static ConVarRef cl_software_cursor( "cl_software_cursor" );
    vgui::surface()->SetSoftwareCursor( cl_software_cursor.GetBool() );

#if defined( USE_SDL )
    static ConVarRef sdl_displayindex( "sdl_displayindex" );
    sdl_displayindex.SetValue( m_nNonVRSDLDisplayIndex );
#endif

#if defined(POSIX)
    ConVarRef m_rawinput( "m_rawinput" );
    m_rawinput.SetValue( m_bNonVRRawInput );
#endif

    // Make sure the client .dll root panel is at the proper point before doing the "SolveTraverse" calls
	vgui::VPANEL root = enginevgui->GetPanel( PANEL_CLIENTDLL );
	if ( root != 0 )
	{
		vgui::ipanel()->SetSize( root, m_nNonVRWidth, m_nNonVRHeight );
	}
	// Same for client .dll tools
	root = enginevgui->GetPanel( PANEL_CLIENTDLL_TOOLS );
	if ( root != 0 )
	{
		vgui::ipanel()->SetSize( root, m_nNonVRWidth, m_nNonVRHeight );
	}

	int viewWidth, viewHeight;
	vgui::surface()->GetScreenSize( viewWidth, viewHeight );

	engine->ExecuteClientCmd( "mat_reset_rendertargets\n" );

	// set mode
	char szCmd[ 256 ];
	Q_snprintf( szCmd, sizeof( szCmd ), "mat_setvideomode %i %i %i\n", m_nNonVRWidth, m_nNonVRHeight, m_bNonVRWindowed ? 1 : 0 );
	engine->ClientCmd_Unrestricted( szCmd );

}


// Called when startup is complete
void CClientVirtualReality::StartupComplete()
{
	// The restored client-owned OpenVR backend is registered during startup but
	// activated manually once the menu is stable. This keeps fatal render/device
	// errors observable in the console instead of terminating initial startup.
	if ( CommandLine()->CheckParm( "-openvr" ) )
		return;

	if ( vr_activate_default.GetBool() || ShouldForceVRActive() )
		Activate();
}

void CClientVirtualReality::MirrorEyeToDesktop( StereoEye_t eEye )
{
	if ( !UseVR() || eEye != STEREO_EYE_LEFT || !CommandLine()->CheckParm( "-openvr" ) )
		return;

	ITexture *eyeTexture = g_pSourceVR->GetRenderTarget( ISourceVirtualReality::VREye_Left,
		ISourceVirtualReality::RT_Color );
	if ( !eyeTexture || eyeTexture->IsError() )
		return;

	if ( !m_MirrorMaterial.IsValid() )
		m_MirrorMaterial.Init( "vgui/openvr_mirror", TEXTURE_GROUP_VGUI );
	if ( !m_MirrorMaterial.IsValid() || m_MirrorMaterial->IsErrorMaterial() )
		return;

	bool foundTexture = false;
	IMaterialVar *textureVar = m_MirrorMaterial->FindVar( "$basetexture", &foundTexture, false );
	if ( !foundTexture || !textureVar )
		return;
	textureVar->SetTextureValue( eyeTexture );

	int width = 0, height = 0;
	materials->GetBackBufferDimensions( width, height );
	if ( width <= 0 || height <= 0 )
		return;

	const int eyeWidth = eyeTexture->GetActualWidth();
	const int eyeHeight = eyeTexture->GetActualHeight();
	if ( eyeWidth <= 0 || eyeHeight <= 0 )
		return;
	// Center-crop the existing eye to 4:3 for spectators. Do not change the
	// headset projection, eye texture, or desktop/VGUI resolution.
	const double aspect = 4.0 / 3.0;
	const double cropWidth = Min( (double)eyeWidth, eyeHeight * aspect );
	const double cropHeight = cropWidth / aspect;
	const float sourceX = (float)( ( eyeWidth - cropWidth ) * 0.5 );
	const float sourceY = (float)( ( eyeHeight - cropHeight ) * 0.5 );
	const double scale = Min( width / cropWidth, height / cropHeight );
	const int mirrorWidth = clamp( (int)( cropWidth * scale + 0.5 ), 1, width );
	const int mirrorHeight = clamp( (int)( cropHeight * scale + 0.5 ), 1, height );
	const int mirrorX = ( width - mirrorWidth ) / 2;
	const int mirrorY = ( height - mirrorHeight ) / 2;
	CMatRenderContextPtr context( materials );
	context->PushRenderTargetAndViewport( NULL, 0, 0, width, height );
	context->ClearColor4ub( 0, 0, 0, 255 );
	context->ClearBuffers( true, false );
	context->DrawScreenSpaceRectangle( m_MirrorMaterial, mirrorX, mirrorY, mirrorWidth, mirrorHeight,
		sourceX, sourceY, sourceX + (float)cropWidth - 1, sourceY + (float)cropHeight - 1,
		eyeWidth, eyeHeight );
	context->PopRenderTargetAndViewport();
}

