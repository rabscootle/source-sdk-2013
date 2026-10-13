//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: OpenVR backend for the legacy SourceVR client rendering path.
//
//=============================================================================

#include "cbase.h"
#include "tf_openvr.h"

#include "sourcevr/isourcevirtualreality.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/itexture.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "tier1/utlvector.h"

#include <d3d11.h>
#include <d3d9.h>
#include <dxgi.h>
#include <openvr.h>

extern ConVar vr_profile_menu;

namespace
{

const float kSourceUnitsPerMeter = 39.3700787f;
const int kGPUBridgeBufferCount = 3;
ConVar vr_render_scale( "vr_render_scale", "1.0", FCVAR_ARCHIVE,
	"Per-eye dimension multiplier relative to SteamVR recommendation (0.25-2). Re-activate VR after changing.",
	true, 0.25f, true, 2.0f );
ConVar vr_copy_interval( "vr_copy_interval", "1", FCVAR_ARCHIVE,
	"Copy eye pixels every N submitted frames on the CPU fallback only; GPU submission always copies every frame.",
	true, 1.0f, true, 8.0f );
ConVar vr_gpu_bridge( "vr_gpu_bridge", "1", FCVAR_ARCHIVE,
	"Use GPU-shared D3D9Ex submission, with CPU fallback if unavailable. Re-activate VR after changing." );

void OpenVRTrace( const char *message )
{
	FILE *file = fopen( "openvr_trace.log", "a" );
	if ( !file )
		return;

	fprintf( file, "%s\n", message );
	fflush( file );
	fclose( file );
}

VMatrix OpenVRMatrixToSource( const vr::HmdMatrix34_t &matrix )
{
	const float ( *m )[4] = matrix.m;
	return VMatrix(
		 m[2][2],  m[2][0], -m[2][1], -m[2][3] * kSourceUnitsPerMeter,
		 m[0][2],  m[0][0], -m[0][1], -m[0][3] * kSourceUnitsPerMeter,
		-m[1][2], -m[1][0],  m[1][1],  m[1][3] * kSourceUnitsPerMeter,
		0.0f, 0.0f, 0.0f, 1.0f );
}

void BuildProjectionMatrix( VMatrix &result, float left, float right, float top, float bottom,
	float zNear, float zFar, float fovScale )
{
	if ( fovScale > 0.0f && fovScale != 1.0f )
	{
		const float reciprocalScale = 1.0f / fovScale;
		left *= reciprocalScale;
		right *= reciprocalScale;
		top *= reciprocalScale;
		bottom *= reciprocalScale;
	}

	const float inverseWidth = 1.0f / ( right - left );
	const float inverseHeight = 1.0f / ( bottom - top );
	const float inverseDepth = 1.0f / ( zFar - zNear );

	result.Init(
		2.0f * inverseWidth, 0.0f, ( right + left ) * inverseWidth, 0.0f,
		0.0f, 2.0f * inverseHeight, ( bottom + top ) * inverseHeight, 0.0f,
		0.0f, 0.0f, -( zFar + zNear ) * inverseDepth, -2.0f * zFar * zNear * inverseDepth,
		0.0f, 0.0f, -1.0f, 0.0f );
}

template <typename T>
void SafeRelease( T *&object )
{
	if ( object )
	{
		object->Release();
		object = NULL;
	}
}

class CTFOpenVR final : public ISourceVirtualReality
{
public:
	CTFOpenVR();
	virtual ~CTFOpenVR();

	virtual bool Connect( CreateInterfaceFn factory ) OVERRIDE;
	virtual void Disconnect() OVERRIDE;
	virtual void *QueryInterface( const char *pInterfaceName ) OVERRIDE;
	virtual InitReturnVal_t Init() OVERRIDE;
	virtual void Shutdown() OVERRIDE;

	virtual bool ShouldRunInVR() OVERRIDE;
	virtual bool IsHmdConnected() OVERRIDE;
	virtual void GetViewportBounds( VREye eye, int *x, int *y, int *width, int *height ) OVERRIDE;
	virtual bool DoDistortionProcessing( VREye eye ) OVERRIDE;
	virtual bool CompositeHud( VREye eye, float ndcHudBounds[4], bool doUndistort, bool blackout, bool translucent ) OVERRIDE;
	virtual VMatrix GetMideyePose() OVERRIDE;
	virtual bool SampleTrackingState( float playerGameFov, float predictionSeconds ) OVERRIDE;
	virtual bool GetDisplayBounds( VRRect_t *rect ) OVERRIDE;
	virtual bool GetEyeProjectionMatrix( VMatrix *result, VREye eye, float zNear, float zFar, float fovScale ) OVERRIDE;
	virtual VMatrix GetMidEyeFromEye( VREye eye ) OVERRIDE;
	virtual int GetVRModeAdapter() OVERRIDE;
	virtual bool WillDriftInYaw() OVERRIDE;
	virtual void CreateRenderTargets( IMaterialSystem *materialSystem ) OVERRIDE;
	virtual void ShutdownRenderTargets() OVERRIDE;
	virtual ITexture *GetRenderTarget( VREye eye, EWhichRenderTarget which ) OVERRIDE;
	virtual void GetRenderTargetFrameBufferDimensions( int &width, int &height ) OVERRIDE;
	virtual bool Activate() OVERRIDE;
	virtual void Deactivate() OVERRIDE;
	virtual bool ShouldForceVRMode() OVERRIDE;
	virtual void SetShouldForceVRMode() OVERRIDE;
private:
	bool CreateD3D11Resources();
	bool CreateGPUBridgeResources();
	ID3D11Texture2D *CopyEyeGPU( VREye eye );
	bool SubmitEye( VREye eye );
	void ReleaseD3D11Resources();

	vr::IVRSystem *m_pVRSystem;
	bool m_bActive;
	bool m_bForceVR;
	uint32 m_nRenderWidth;
	uint32 m_nRenderHeight;
	VMatrix m_MideyePose;
	int m_nLastTrackingFrame;
	CTextureReference m_EyeTexture[2];
	CTextureReference m_GlowTexture;
	CUtlVector<uint8> m_Readback;
	ID3D11Device *m_pD3D11Device;
	ID3D11DeviceContext *m_pD3D11Context;
	ID3D11Texture2D *m_pSubmitTexture[2];
	IDirect3DDevice9Ex *m_pD3D9Device;
	ID3D11Texture2D *m_pSharedTexture11[2][kGPUBridgeBufferCount];
	ID3D11Texture2D *m_pLastReadyTexture11[2];
	IDirect3DTexture9 *m_pSharedTexture9[2][kGPUBridgeBufferCount];
	IDirect3DSurface9 *m_pSharedSurface9[2][kGPUBridgeBufferCount];
	IDirect3DQuery9 *m_pCopyQuery9[2][kGPUBridgeBufferCount];
	uint32 m_nCopyGeneration[2][kGPUBridgeBufferCount];
	int m_nGPUWriteSlot[2];
	uint32 m_nNextCopyGeneration;
	bool m_bGPUBridge;
	bool m_bSubmitTextureInitialized[2];
};

CTFOpenVR::CTFOpenVR()
	: m_pVRSystem( NULL ),
	  m_bActive( false ),
	  m_bForceVR( true ),
	  m_nRenderWidth( 0 ),
	  m_nRenderHeight( 0 ),
	  m_pD3D11Device( NULL ),
	  m_pD3D11Context( NULL ),
	  m_pD3D9Device( NULL ),
	  m_bGPUBridge( false )
{
	m_MideyePose.Identity();
	m_nLastTrackingFrame = -1;
	m_pSubmitTexture[0] = NULL;
	m_pSubmitTexture[1] = NULL;
	Q_memset( m_pSharedTexture11, 0, sizeof( m_pSharedTexture11 ) );
	m_pLastReadyTexture11[0] = m_pLastReadyTexture11[1] = NULL;
	Q_memset( m_pSharedTexture9, 0, sizeof( m_pSharedTexture9 ) );
	Q_memset( m_pSharedSurface9, 0, sizeof( m_pSharedSurface9 ) );
	Q_memset( m_pCopyQuery9, 0, sizeof( m_pCopyQuery9 ) );
	Q_memset( m_nCopyGeneration, 0, sizeof( m_nCopyGeneration ) );
	m_nGPUWriteSlot[0] = m_nGPUWriteSlot[1] = 0;
	m_nNextCopyGeneration = 1;
	m_bSubmitTextureInitialized[0] = false;
	m_bSubmitTextureInitialized[1] = false;
}

CTFOpenVR::~CTFOpenVR()
{
	Shutdown();
}

bool CTFOpenVR::Connect( CreateInterfaceFn factory )
{
	if ( !factory )
		return false;
	return true;
}

void CTFOpenVR::Disconnect()
{
}

void *CTFOpenVR::QueryInterface( const char *pInterfaceName )
{
	return pInterfaceName && !Q_strcmp( pInterfaceName, SOURCE_VIRTUAL_REALITY_INTERFACE_VERSION ) ? this : NULL;
}

InitReturnVal_t CTFOpenVR::Init()
{
	return INIT_OK;
}

void CTFOpenVR::Shutdown()
{
	Deactivate();
}

bool CTFOpenVR::ShouldRunInVR()
{
	return m_bActive;
}

bool CTFOpenVR::IsHmdConnected()
{
	return vr::VR_IsHmdPresent();
}

void CTFOpenVR::GetViewportBounds( VREye eye, int *x, int *y, int *width, int *height )
{
	if ( x ) *x = 0;
	if ( y ) *y = 0;
	if ( width ) *width = static_cast<int>( m_nRenderWidth );
	if ( height ) *height = static_cast<int>( m_nRenderHeight );
}

bool CTFOpenVR::DoDistortionProcessing( VREye eye )
{
	// Modern SteamVR performs distortion.  The legacy callback is now the point
	// at which the completed, undistorted eye texture is submitted.
	return SubmitEye( eye );
}

bool CTFOpenVR::CompositeHud( VREye eye, float ndcHudBounds[4], bool doUndistort, bool blackout, bool translucent )
{
	// The viability backend renders the HUD into the world before SubmitEye.
	// A compositor overlay will replace this fallback in the zero-copy stage.
	return false;
}

VMatrix CTFOpenVR::GetMideyePose()
{
	return m_MideyePose;
}

bool CTFOpenVR::SampleTrackingState( float playerGameFov, float predictionSeconds )
{
	if ( !m_bActive || !vr::VRCompositor() )
		return false;
	// View setup and DrawMainMenu can both request tracking in one frame.
	// WaitGetPoses is a frame pacing boundary, not a cheap pose getter.
	if ( m_nLastTrackingFrame == gpGlobals->framecount )
		return true;

	vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount];
	const bool profiling = vr_profile_menu.GetBool();
	const double waitStart = profiling ? Plat_FloatTime() : 0;
	const vr::EVRCompositorError error = vr::VRCompositor()->WaitGetPoses( poses, ARRAYSIZE( poses ), NULL, 0 );
	static int samples = 0, duplicates = 0, previousFrame = -1;
	static double waitTotal = 0;
	if ( profiling )
	{
		waitTotal += Plat_FloatTime() - waitStart;
		if ( previousFrame == gpGlobals->framecount ) ++duplicates;
		previousFrame = gpGlobals->framecount;
		if ( ++samples == 120 )
		{
			char message[192];
			Q_snprintf( message, sizeof( message ), "Pose CPU ms avg/120: wait=%.3f repeated_same_frame=%d in_level=%d gpu=%d",
				waitTotal * 1000 / samples, duplicates, engine->IsInGame(), m_bGPUBridge );
			OpenVRTrace( message );
			samples = duplicates = 0; waitTotal = 0;
		}
	}
	else { samples = duplicates = 0; previousFrame = -1; waitTotal = 0; }
	if ( error != vr::VRCompositorError_None )
		return false;

	const vr::TrackedDevicePose_t &hmd = poses[vr::k_unTrackedDeviceIndex_Hmd];
	if ( !hmd.bDeviceIsConnected || !hmd.bPoseIsValid )
		return false;

	m_MideyePose = OpenVRMatrixToSource( hmd.mDeviceToAbsoluteTracking );
	m_nLastTrackingFrame = gpGlobals->framecount;
	return true;
}

bool CTFOpenVR::GetDisplayBounds( VRRect_t *rect )
{
	// Direct mode owns the HMD display.  Source should keep its companion window.
	return false;
}

bool CTFOpenVR::GetEyeProjectionMatrix( VMatrix *result, VREye eye, float zNear, float zFar, float fovScale )
{
	if ( !result || !m_pVRSystem )
		return false;

	float left, right, top, bottom;
	m_pVRSystem->GetProjectionRaw( static_cast<vr::EVREye>( eye ), &left, &right, &top, &bottom );
	BuildProjectionMatrix( *result, left, right, top, bottom, zNear, zFar, fovScale );
	return true;
}

VMatrix CTFOpenVR::GetMidEyeFromEye( VREye eye )
{
	if ( !m_pVRSystem )
	{
		VMatrix identity;
		identity.Identity();
		return identity;
	}

	return OpenVRMatrixToSource( m_pVRSystem->GetEyeToHeadTransform( static_cast<vr::EVREye>( eye ) ) );
}

int CTFOpenVR::GetVRModeAdapter()
{
	if ( !m_pVRSystem )
		return -1;

	int32 adapter = -1;
	m_pVRSystem->GetDXGIOutputInfo( &adapter );
	return adapter;
}

bool CTFOpenVR::WillDriftInYaw()
{
	if ( !m_pVRSystem )
		return false;

	vr::ETrackedPropertyError error = vr::TrackedProp_Success;
	return m_pVRSystem->GetBoolTrackedDeviceProperty( vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_WillDriftInYaw_Bool, &error );
}

void CTFOpenVR::CreateRenderTargets( IMaterialSystem *materialSystem )
{
	OpenVRTrace( "RenderTargets: callback entered" );
	ShutdownRenderTargets();
	if ( !m_bActive || !materialSystem || !m_nRenderWidth || !m_nRenderHeight )
	{
		OpenVRTrace( "RenderTargets: skipped because backend is not ready" );
		return;
	}

	m_EyeTexture[VREye_Left].Init( materialSystem->CreateNamedRenderTargetTextureEx2(
		"_rt_OpenVRLeft", m_nRenderWidth, m_nRenderHeight, RT_SIZE_NO_CHANGE,
		IMAGE_FORMAT_BGRA8888, MATERIAL_RT_DEPTH_SEPARATE,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP, 0 ) );
	m_EyeTexture[VREye_Right].Init( materialSystem->CreateNamedRenderTargetTextureEx2(
		"_rt_OpenVRRight", m_nRenderWidth, m_nRenderHeight, RT_SIZE_NO_CHANGE,
		IMAGE_FORMAT_BGRA8888, MATERIAL_RT_DEPTH_SEPARATE,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP, 0 ) );

	for ( int eye = 0; eye < 2; ++eye )
	{
		ITexture *texture = m_EyeTexture[eye];
		const int width = texture ? texture->GetActualWidth() : 0;
		const int height = texture ? texture->GetActualHeight() : 0;
		char message[192];
		Q_snprintf( message, sizeof( message ), "Eye allocation: eye=%d requested=%ux%u actual=%dx%d",
			eye, m_nRenderWidth, m_nRenderHeight, width, height );
		OpenVRTrace( message );
		if ( !texture || texture->IsError() || width != (int)m_nRenderWidth || height != (int)m_nRenderHeight )
		{
			Warning( "OpenVR eye target allocation failed or size mismatched. Lower vr_render_scale and reactivate VR.\n" );
			ShutdownRenderTargets();
			return;
		}
	}
	m_Readback.SetCount( m_nRenderWidth * m_nRenderHeight * 4 );
	// Eye-sized scratch color for outlines. The desktop FullFrameFB may be
	// much smaller than an eye (or have an entirely different aspect ratio).
	// Each eye renders and consumes this scratch target before the next eye.
	m_GlowTexture.Init( materialSystem->CreateNamedRenderTargetTextureEx2(
		"_rt_OpenVRGlow", m_nRenderWidth, m_nRenderHeight, RT_SIZE_NO_CHANGE,
		IMAGE_FORMAT_BGRA8888, MATERIAL_RT_DEPTH_NONE,
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP, 0 ) );
	OpenVRTrace( m_EyeTexture[VREye_Left].IsValid() && m_EyeTexture[VREye_Right].IsValid()
		? "RenderTargets: both eye textures created"
		: "RenderTargets: eye texture creation failed" );
}

void CTFOpenVR::ShutdownRenderTargets()
{
	m_GlowTexture.Shutdown();
	m_EyeTexture[VREye_Left].Shutdown();
	m_EyeTexture[VREye_Right].Shutdown();
	m_Readback.Purge();
}

ITexture *CTFOpenVR::GetRenderTarget( VREye eye, EWhichRenderTarget which )
{
	if ( which != RT_Color )
		return NULL;

	return m_EyeTexture[eye];
}

void CTFOpenVR::GetRenderTargetFrameBufferDimensions( int &width, int &height )
{
	width = static_cast<int>( m_nRenderWidth );
	height = static_cast<int>( m_nRenderHeight );
}

bool CTFOpenVR::Activate()
{
	m_nLastTrackingFrame = -1;
	OpenVRTrace( "Activate: entered" );
	if ( m_bActive )
		return true;

	vr::EVRInitError error = vr::VRInitError_None;
	OpenVRTrace( "Activate: calling VR_Init" );
	m_pVRSystem = vr::VR_Init( &error, vr::VRApplication_Scene );
	OpenVRTrace( "Activate: VR_Init returned" );
	if ( error != vr::VRInitError_None || !m_pVRSystem || !vr::VRCompositor() )
	{
		Warning( "OpenVR initialization failed: %s\n", vr::VR_GetVRInitErrorAsEnglishDescription( error ) );
		m_pVRSystem = NULL;
		return false;
	}

	OpenVRTrace( "Activate: compositor available" );
	m_pVRSystem->GetRecommendedRenderTargetSize( &m_nRenderWidth, &m_nRenderHeight );
	const float renderScale = clamp( vr_render_scale.GetFloat(), 0.25f, 2.0f );
	// Bound resource sizes before float-to-integer conversion and allocation.
	if ( !m_nRenderWidth || !m_nRenderHeight ||
		m_nRenderWidth * renderScale > 8192.0f || m_nRenderHeight * renderScale > 8192.0f )
	{
		Warning( "OpenVR requested eye size exceeds the bridge's 8192-pixel dimension limit. Lower vr_render_scale.\n" );
		vr::VR_Shutdown();
		m_pVRSystem = NULL;
		return false;
	}
	m_nRenderWidth = Max( 1u, static_cast<uint32>( m_nRenderWidth * renderScale ) );
	m_nRenderHeight = Max( 1u, static_cast<uint32>( m_nRenderHeight * renderScale ) );
	OpenVRTrace( "Activate: creating D3D11 bridge" );
	if ( !CreateD3D11Resources() )
	{
		Warning( "OpenVR could not create the D3D11 submission bridge.\n" );
		vr::VR_Shutdown();
		m_pVRSystem = NULL;
		return false;
	}

	OpenVRTrace( "Activate: D3D11 bridge ready" );
	m_bActive = true;
	vr::VRCompositor()->SetTrackingSpace( vr::TrackingUniverseSeated );
	OpenVRTrace( "Activate: completed" );

	// CClientVirtualReality follows a successful activation with
	// mat_reset_rendertargets. CTFRenderTargets creates the eye textures from
	// that normal material-system allocation callback; allocating them here is
	// too late in the current frame and can abort inside the engine.
	Msg( "OpenVR initialized at %ux%u per eye; awaiting render-target reset.\n", m_nRenderWidth, m_nRenderHeight );
	return true;
}

void CTFOpenVR::Deactivate()
{
	if ( !m_bActive && !m_pVRSystem )
		return;

	m_bActive = false;
	ShutdownRenderTargets();
	ReleaseD3D11Resources();
	if ( m_pVRSystem )
		vr::VR_Shutdown();
	m_pVRSystem = NULL;
	m_MideyePose.Identity();
}

bool CTFOpenVR::ShouldForceVRMode()
{
	return m_bForceVR;
}

void CTFOpenVR::SetShouldForceVRMode()
{
	m_bForceVR = true;
}

bool CTFOpenVR::CreateD3D11Resources()
{
	ReleaseD3D11Resources();

	IDXGIFactory1 *factory = NULL;
	IDXGIAdapter1 *adapter = NULL;
	int adapterIndex = GetVRModeAdapter();
	if ( SUCCEEDED( CreateDXGIFactory1( __uuidof( IDXGIFactory1 ), reinterpret_cast<void **>( &factory ) ) ) && adapterIndex >= 0 )
		factory->EnumAdapters1( adapterIndex, &adapter );

	const D3D_DRIVER_TYPE driverType = adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;
	const HRESULT createResult = D3D11CreateDevice( adapter, driverType, NULL, 0, NULL, 0,
		D3D11_SDK_VERSION, &m_pD3D11Device, NULL, &m_pD3D11Context );
	SafeRelease( adapter );
	SafeRelease( factory );
	if ( FAILED( createResult ) )
		return false;

	if ( vr_gpu_bridge.GetBool() && CreateGPUBridgeResources() )
	{
		m_bGPUBridge = true;
		Msg( "OpenVR using GPU-shared D3D9Ex submission bridge.\n" );
		OpenVRTrace( "Bridge: GPU sharing enabled" );
		return true;
	}
	if ( vr_gpu_bridge.GetBool() )
	{
		Warning( "OpenVR GPU sharing unavailable; using the slower CPU submission bridge.\n" );
		OpenVRTrace( "Bridge: GPU sharing unavailable, using CPU fallback" );
	}
	else
	{
		Msg( "OpenVR using configured CPU submission bridge.\n" );
		OpenVRTrace( "Bridge: CPU fallback selected by configuration" );
	}

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = m_nRenderWidth;
	desc.Height = m_nRenderHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	for ( int eye = 0; eye < 2; ++eye )
	{
		if ( FAILED( m_pD3D11Device->CreateTexture2D( &desc, NULL, &m_pSubmitTexture[eye] ) ) )
		{
			ReleaseD3D11Resources();
			return false;
		}
	}
	return true;
}

bool CTFOpenVR::CreateGPUBridgeResources()
{
	typedef IDirect3DDevice9 *( *GetD3D9DeviceFn )();
	HMODULE launcher = GetModuleHandle( NULL );
	GetD3D9DeviceFn getDevice = launcher ? reinterpret_cast<GetD3D9DeviceFn>(
		GetProcAddress( launcher, "OpenVR_GetD3D9Device" ) ) : NULL;
	IDirect3DDevice9 *device9 = getDevice ? getDevice() : NULL;
	if ( !device9 )
		return false;

	const HRESULT exResult = device9->QueryInterface( __uuidof( IDirect3DDevice9Ex ),
		reinterpret_cast<void **>( &m_pD3D9Device ) );
	device9->Release();
	if ( FAILED( exResult ) || !m_pD3D9Device )
		return false;

	for ( int eye = 0; eye < 2; ++eye )
	{
		for ( int slot = 0; slot < kGPUBridgeBufferCount; ++slot )
		{
			HANDLE sharedHandle = NULL;
			if ( FAILED( m_pD3D9Device->CreateTexture( m_nRenderWidth, m_nRenderHeight, 1,
				D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
				&m_pSharedTexture9[eye][slot], &sharedHandle ) ) || !sharedHandle ||
				FAILED( m_pSharedTexture9[eye][slot]->GetSurfaceLevel( 0, &m_pSharedSurface9[eye][slot] ) ) ||
				FAILED( m_pD3D11Device->OpenSharedResource( sharedHandle, __uuidof( ID3D11Texture2D ),
					reinterpret_cast<void **>( &m_pSharedTexture11[eye][slot] ) ) ) ||
				FAILED( m_pD3D9Device->CreateQuery( D3DQUERYTYPE_EVENT, &m_pCopyQuery9[eye][slot] ) ) )
			{
				for ( int cleanupEye = 0; cleanupEye < 2; ++cleanupEye )
					for ( int cleanupSlot = 0; cleanupSlot < kGPUBridgeBufferCount; ++cleanupSlot )
					{
						SafeRelease( m_pCopyQuery9[cleanupEye][cleanupSlot] );
						SafeRelease( m_pSharedSurface9[cleanupEye][cleanupSlot] );
						SafeRelease( m_pSharedTexture9[cleanupEye][cleanupSlot] );
						SafeRelease( m_pSharedTexture11[cleanupEye][cleanupSlot] );
					}
				SafeRelease( m_pD3D9Device );
				return false;
			}
		}
	}
	return true;
}

ID3D11Texture2D *CTFOpenVR::CopyEyeGPU( VREye eye )
{
	if ( !m_pD3D9Device )
		return NULL;

	// Retire completed copies before selecting a destination. Reissuing an
	// event query while its previous copy is still pending loses that fence and
	// can leave SteamVR presenting the last menu frame forever.
	int writeSlot = -1;
	uint32 oldestReadyGeneration = UINT_MAX;
	for ( int slot = 0; slot < kGPUBridgeBufferCount; ++slot )
	{
		const bool unused = m_nCopyGeneration[eye][slot] == 0;
		const bool complete = !unused && m_pCopyQuery9[eye][slot]->GetData( NULL, 0, 0 ) == S_OK;
		if ( unused )
		{
			writeSlot = slot;
			oldestReadyGeneration = 0;
		}
		else if ( complete && m_pSharedTexture11[eye][slot] != m_pLastReadyTexture11[eye] && m_nCopyGeneration[eye][slot] < oldestReadyGeneration )
		{
			writeSlot = slot;
			oldestReadyGeneration = m_nCopyGeneration[eye][slot];
		}
	}
	// Do not submit older completed copies with the current frame's pose.

	// All three copies may still be in flight during a device transition or a
	// heavy frame. Preserve the most recent completed image rather than
	// destroying one of the outstanding fences.
	if ( writeSlot < 0 )
		return NULL;

	IDirect3DSurface9 *source = NULL;
	HRESULT result = m_pD3D9Device->GetRenderTarget( 0, &source );
	if ( SUCCEEDED( result ) && source )
	{
		D3DSURFACE_DESC desc = {};
		result = source->GetDesc( &desc );
		static int reports[2] = { 0, 0 };
		if ( reports[eye] < 3 )
		{
			char message[256];
			Q_snprintf( message, sizeof( message ),
				"GPU source: frame=%d eye=%d surface=%p size=%ux%u expected=%ux%u",
				gpGlobals->framecount, eye, source, desc.Width, desc.Height, m_nRenderWidth, m_nRenderHeight );
			OpenVRTrace( message );
			++reports[eye];
		}
		if ( SUCCEEDED( result ) && ( desc.Width != m_nRenderWidth || desc.Height != m_nRenderHeight ) )
		{
			SafeRelease( source );
			return NULL; // Never stretch an unrelated backbuffer into an eye.
		}
	}
	if ( SUCCEEDED( result ) && source )
		result = m_pD3D9Device->StretchRect( source, NULL, m_pSharedSurface9[eye][writeSlot], NULL, D3DTEXF_NONE );
	SafeRelease( source );
	if ( FAILED( result ) )
		return NULL;

	IDirect3DQuery9 *writeQuery = m_pCopyQuery9[eye][writeSlot];
	if ( FAILED( writeQuery->Issue( D3DISSUE_END ) ) )
		return NULL;
	m_nCopyGeneration[eye][writeSlot] = m_nNextCopyGeneration++;
	// Kick the command buffer before waiting for this frame's copy.
	writeQuery->GetData( NULL, 0, D3DGETDATA_FLUSH );

	// Wait for this frame's copy, not an older ring entry. This is a GPU-side
	// pixel transfer with a bounded CPU fence wait, not a pixel readback.
	{
		const uint32 waitStart = Plat_MSTime();
		HRESULT ready = S_FALSE;
		do
			ready = writeQuery->GetData( NULL, 0, D3DGETDATA_FLUSH );
		while ( ready == S_FALSE && Plat_MSTime() - waitStart < 50 );
		if ( ready == S_OK )
			m_pLastReadyTexture11[eye] = m_pSharedTexture11[eye][writeSlot];
		else
		{
			static bool reported = false;
			if ( !reported )
			{
				Warning( "OpenVR GPU copy fence failed/timed out: 0x%08lx; skipping incomplete eye.\n", (unsigned long)ready );
				reported = true;
			}
			return NULL;
		}
	}
	return m_pLastReadyTexture11[eye];
}

bool CTFOpenVR::SubmitEye( VREye eye )
{
	static bool firstAttempt[2] = { true, true };
	static bool firstSuccess[2] = { true, true };
	static bool firstError[2] = { true, true };
	if ( firstAttempt[eye] )
	{
		OpenVRTrace( eye == VREye_Left ? "Submit: left eye entered" : "Submit: right eye entered" );
		firstAttempt[eye] = false;
	}

	if ( !m_bActive || !m_pD3D11Context ||
		( !m_bGPUBridge && !m_pSubmitTexture[eye] ) || !m_EyeTexture[eye].IsValid() )
	{
		OpenVRTrace( eye == VREye_Left ? "Submit: left prerequisites missing" : "Submit: right prerequisites missing" );
		return false;
	}

	const int copyInterval = clamp( vr_copy_interval.GetInt(), 1, 8 );
	const bool copyThisFrame = !m_bSubmitTextureInitialized[eye] || ( gpGlobals->framecount % copyInterval ) == 0;
	ID3D11Texture2D *submitTexture = m_bGPUBridge ? m_pLastReadyTexture11[eye] : m_pSubmitTexture[eye];
	if ( copyThisFrame )
	{
		bool copied = true;
		CMatRenderContextPtr context( materials );
		context->PushRenderTargetAndViewport( m_EyeTexture[eye], 0, 0, m_nRenderWidth, m_nRenderHeight );
		// Queue Source's outstanding rendering; the D3D9 event query below is
		// the single synchronization point for the render plus shared copy.
		// Raw D3D9 calls must follow execution of queued target bindings/draws.
		context->Flush( m_bGPUBridge );
		if ( m_bGPUBridge )
		{
			// Do not drain the material queue recursively from RenderView.
			// GPU submission remains experimental until render-thread ownership
			// is handled without ExecuteQueued at this callback boundary.
			submitTexture = CopyEyeGPU( eye );
			copied = submitTexture != NULL;
		}
		else
		{
			context->ReadPixels( 0, 0, m_nRenderWidth, m_nRenderHeight, m_Readback.Base(), IMAGE_FORMAT_BGRA8888 );
		}
		context->PopRenderTargetAndViewport();
		if ( !copied )
			return false;

		if ( !m_bGPUBridge )
		{
			m_pD3D11Context->UpdateSubresource( m_pSubmitTexture[eye], 0, NULL, m_Readback.Base(), m_nRenderWidth * 4, 0 );
			m_bSubmitTextureInitialized[eye] = true;
		}
	}
	if ( !submitTexture )
		return false;
	vr::Texture_t texture = { submitTexture, vr::TextureType_DirectX, vr::ColorSpace_Gamma };
	const vr::EVRCompositorError error = vr::VRCompositor()->Submit( static_cast<vr::EVREye>( eye ), &texture );
	if ( error != vr::VRCompositorError_None )
	{
		if ( firstError[eye] )
		{
			char trace[128];
			Q_snprintf( trace, sizeof( trace ), "Submit: eye %d failed with compositor error %d", eye, error );
			OpenVRTrace( trace );
			Warning( "OpenVR eye %d submission failed: %d\n", eye, error );
			firstError[eye] = false;
		}
		return false;
	}
	if ( firstSuccess[eye] )
	{
		OpenVRTrace( eye == VREye_Left ? "Submit: left eye accepted" : "Submit: right eye accepted" );
		firstSuccess[eye] = false;
	}
	// Both eye submissions now describe one complete application frame. Give
	// the compositor an explicit scheduling boundary instead of immediately
	// starting more D3D9 work while it is acquiring the submitted textures.
	if ( eye == VREye_Right )
		vr::VRCompositor()->PostPresentHandoff();
	return true;
}

void CTFOpenVR::ReleaseD3D11Resources()
{
	for ( int eye = 0; eye < 2; ++eye )
		for ( int slot = 0; slot < kGPUBridgeBufferCount; ++slot )
		{
			SafeRelease( m_pCopyQuery9[eye][slot] );
			SafeRelease( m_pSharedSurface9[eye][slot] );
			SafeRelease( m_pSharedTexture9[eye][slot] );
			SafeRelease( m_pSharedTexture11[eye][slot] );
			m_nCopyGeneration[eye][slot] = 0;
		}
	m_pLastReadyTexture11[0] = m_pLastReadyTexture11[1] = NULL;
	m_nGPUWriteSlot[0] = m_nGPUWriteSlot[1] = 0;
	m_nNextCopyGeneration = 1;
	SafeRelease( m_pD3D9Device );
	SafeRelease( m_pSubmitTexture[0] );
	SafeRelease( m_pSubmitTexture[1] );
	SafeRelease( m_pD3D11Context );
	SafeRelease( m_pD3D11Device );
	m_bSubmitTextureInitialized[0] = false;
	m_bSubmitTextureInitialized[1] = false;
	m_bGPUBridge = false;
}

CTFOpenVR g_TFOpenVR;

} // namespace

ISourceVirtualReality *TFOpenVR_GetSourceVR()
{
	return &g_TFOpenVR;
}
