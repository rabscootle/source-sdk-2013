//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Local OpenVR implementation of the legacy SourceVR interface.
//
//=============================================================================

#ifndef TF_OPENVR_H
#define TF_OPENVR_H
#ifdef _WIN32
#pragma once
#endif

class ISourceVirtualReality;

// The original SourceVR implementation lived in a private engine DLL which is
// no longer distributed.  TF builds use this client-owned replacement when the
// engine cannot provide SourceVirtualReality001.
ISourceVirtualReality *TFOpenVR_GetSourceVR();

#endif // TF_OPENVR_H
