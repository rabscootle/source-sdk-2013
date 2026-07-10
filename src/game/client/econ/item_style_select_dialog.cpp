//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:
//
//=============================================================================//


#include "cbase.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/ComboBox.h"
#include "econ_item_system.h"
#include "econ_item_constants.h"
#include "item_style_select_dialog.h"
#include "econ_gcmessages.h"
#include "backpack_panel.h"
#include "econ_ui.h"
#include "gc_clientsystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>


//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
CComboBoxBackpackOverlayDialogBase::CComboBoxBackpackOverlayDialogBase( vgui::Panel *parent, CEconItemView *pItem )
	: vgui::EditablePanel( parent, "ComboBoxBackpackOverlayDialogBase" )
	, m_pPreviewModelPanel( NULL )
	, m_pItem( pItem )
{
	if ( m_pItem )
	{
		m_pPreviewModelPanel = new CItemModelPanel( this, "preview_model" );
		m_pPreviewModelPanel->SetItem( m_pItem );
	}

	m_pComboBox = new vgui::ComboBox( this, "ComboBox", 5, false );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CComboBoxBackpackOverlayDialogBase::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	LoadControlSettings( "Resource/UI/econ/ComboBoxBackpackOverlayDialog.res" );

	BaseClass::ApplySchemeSettings( pScheme );

	if ( m_pPreviewModelPanel )
	{
		if ( m_pPreviewModelPanel && m_pItem )
		{
			m_pPreviewModelPanel->SetItem( m_pItem );
		}

		m_pPreviewModelPanel->SetActAsButton( true, false );
	}

	if ( m_pComboBox )
	{
		m_pComboBox->RemoveAll();

		vgui::HFont hFont = pScheme->GetFont( "HudFontSmallestBold", true );
		m_pComboBox->SetFont( hFont );

		PopulateComboBoxOptions();

		m_pComboBox->AddActionSignalTarget( this );
	}

	CExLabel *pTitleLabel = dynamic_cast<CExLabel *>( FindChildByName( "TitleLabel" ) );
	if ( pTitleLabel )
	{
		pTitleLabel->SetText( GetTitleLabelLocalizationToken() );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CComboBoxBackpackOverlayDialogBase::OnCommand( const char *command )
{
	if ( !Q_strnicmp( command, "cancel", 6 ) )
	{
		TFModalStack()->PopModal( this );

		SetVisible( false );
		MarkForDeletion();
	}
	else if ( !Q_strnicmp( command, "apply", 5 ) )
	{
		OnComboBoxApplication();
		
		TFModalStack()->PopModal( this );

		SetVisible( false );
		MarkForDeletion();
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CComboBoxBackpackOverlayDialogBase::Show()
{
	SetVisible( true );
	MakePopup();
	MoveToFront();
	SetKeyBoardInputEnabled( true );
	SetMouseInputEnabled( true );
	TFModalStack()->PushModal( this );

	// Special-case the path where we only wind up with one option in the combo box. If
	// this happens we just pretend the user selected it and move on to the next step. For
	// restoration, this will bring up a confirmation dialog, and for setting styles... well,
	// we'd expect it to never happen because the option to bring up the UI wouldn't be enabled
	// if there was only a single style.
	if ( m_pComboBox && m_pComboBox->GetItemCount() == 1 )
	{
		OnCommand( "apply" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called when text changes in combo box
//-----------------------------------------------------------------------------
void CComboBoxBackpackOverlayDialogBase::OnTextChanged( KeyValues *data )
{
	if ( !m_pComboBox )
		return;

	Panel *pPanel = reinterpret_cast<vgui::Panel *>( data->GetPtr("panel") );
	vgui::ComboBox *pComboBox = dynamic_cast<vgui::ComboBox *>( pPanel );

	if ( pComboBox == m_pComboBox )
	{
		OnComboBoxChanged( m_pComboBox->GetActiveItem() );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CStyleSelectDialog::PopulateComboBoxOptions()
{
	CEconItemView* pItem = GetPreviewModelPanel()->GetItem();
	Assert( pItem );

	if ( pItem->GetStaticData()->GetNumStyles() )
	{
		KeyValues *pKeyValues = new KeyValues( "data" );
		for ( style_index_t i=0; i<pItem->GetStaticData()->GetNumStyles(); i++ )
		{
			const CEconStyleInfo *pStyle = pItem->GetStaticData()->GetStyleInfo( i );
			if ( pStyle && pStyle->IsSelectable() )
			{
				pKeyValues->SetInt( "style_index", i );
				GetComboBox()->AddItem( pItem->GetStaticData()->GetStyleInfo( i )->GetName(), pKeyValues );
			}
		}
		pKeyValues->deleteThis();

		GetComboBox()->ActivateItemByRow( pItem->GetItemStyle() );
	}
	else
	{
		GetComboBox()->ActivateItemByRow( 0 );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CStyleSelectDialog::OnComboBoxApplication()
{
	KeyValues *pKV = GetComboBox()->GetActiveItemUserData();
	int iNewStyle = pKV->GetInt( "style_index", 0 );
	CEconItemView* pItem = GetPreviewModelPanel()->GetItem();

	if ( pItem )
	{
		const char* pszStyleName = pItem->GetStaticData()->GetStyleInfo( iNewStyle )->GetName();

		// Tell the GC to update the style.
		GCSDK::CGCMsg< MsgGCSetItemStyle_t > msg( k_EMsgGCSetItemStyle );
		msg.Body().m_unItemID = pItem->GetItemID();
		msg.Body().m_iStyle = iNewStyle;
		GCClientSystem()->BSendMessage( msg );

		EconUI()->Gamestats_ItemTransaction( IE_ITEM_CHANGED_STYLE, pItem, pszStyleName /* stored unlocalized here intentionally */, iNewStyle );

		// Tell our parent about the change
		if ( pItem && pItem->IsValid() )
		{
			KeyValues *pKey = new KeyValues( "SelectionReturned" );
			pKey->SetUint64( "itemindex", pItem->GetItemID() );
			PostMessage( GetParent(), pKey );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CStyleSelectDialog::OnComboBoxChanged( int iNewSelection )
{
	GetPreviewModelPanel()->SetItemStyle( GetComboBox()->GetActiveItem() );
	GetPreviewModelPanel()->UpdatePanels();
}

// ===== BEGIN STAT CLOCK ENHANCEMENT: COUNTER SELECT DIALOG =====
static bool SetStatTrakDisplayOverrideOnItemView( CEconItemView *pItem, int iSlot )
{
	if ( !pItem || !BIsValidKillEaterSlotForItem( pItem, iSlot ) )
		return false;

	static CSchemaAttributeDefHandle pAttrDef_StatTrakDisplayOverride( "stattrak display override" );
	if ( !pAttrDef_StatTrakDisplayOverride )
		return false;

	pItem->GetAttributeList()->RemoveAttribute( pAttrDef_StatTrakDisplayOverride );
	CEconItemAttribute displayOverride( pAttrDef_StatTrakDisplayOverride->GetDefinitionIndex(), (uint32)iSlot );
	pItem->GetAttributeList()->AddAttribute( &displayOverride );
	return true;
}

void CStatTrakSelectDialog::PopulateComboBoxOptions()
{
	CEconItemView *pItem = GetPreviewModelPanel()->GetItem();
	if ( !pItem )
		return;

	uint32 unCurrentSlot = 0;
	static CSchemaAttributeDefHandle pAttrDef_StatTrakDisplayOverride( "stattrak display override" );
	if ( pAttrDef_StatTrakDisplayOverride )
		pItem->FindAttribute( pAttrDef_StatTrakDisplayOverride, &unCurrentSlot );

	int iSelectedRow = 0;
	int iRow = 0;
	for ( int i = 0; i < GetKillEaterAttrCount(); i++ )
	{
		if ( !BIsValidKillEaterSlotForItem( pItem, i ) )
			continue;

		uint32 unEventType = kKillEaterEvent_PlayerKill;
		float flEventType;
		if ( FindAttribute_UnsafeBitwiseCast<attrib_value_t>( pItem, GetKillEaterAttr_Type( i ), &flEventType ) )
			unEventType = (uint32)flEventType;

		const char *pszTypeLocKey = GetItemSchema()->GetKillEaterScoreTypeLocString( unEventType );
		const locchar_t *pCounterName = pszTypeLocKey ? GLocalizationProvider()->Find( pszTypeLocKey ) : NULL;
		if ( !pCounterName )
			continue;

		KeyValues *pData = new KeyValues( "data" );
		pData->SetInt( "counter_slot", i );
		GetComboBox()->AddItem( pCounterName, pData );
		pData->deleteThis();

		if ( i == (int)unCurrentSlot )
			iSelectedRow = iRow;
		iRow++;
	}

	if ( GetComboBox()->GetItemCount() <= 0 )
		return;

	GetComboBox()->ActivateItemByRow( iSelectedRow );
	KeyValues *pActiveData = GetComboBox()->GetActiveItemUserData();
	if ( pActiveData )
		UpdatePreviewForSlot( pActiveData->GetInt( "counter_slot", 0 ) );
}

void CStatTrakSelectDialog::UpdatePreviewForSlot( int iSlot )
{
	CEconItemView *pPreviewItem = GetPreviewModelPanel()->GetItem();
	if ( !SetStatTrakDisplayOverrideOnItemView( pPreviewItem, iSlot ) )
		return;

	CAttribute_String attrModule;
	if ( GetStattrak( pPreviewItem, &attrModule ) )
		GetPreviewModelPanel()->SetPreviewModel( attrModule.value().c_str() );
}

void CStatTrakSelectDialog::OnComboBoxChanged( int iNewSelection )
{
	KeyValues *pData = GetComboBox()->GetActiveItemUserData();
	if ( pData )
		UpdatePreviewForSlot( pData->GetInt( "counter_slot", 0 ) );
}

void CStatTrakSelectDialog::OnComboBoxApplication()
{
	KeyValues *pData = GetComboBox()->GetActiveItemUserData();
	int iSlot = pData ? pData->GetInt( "counter_slot", 0 ) : 0;
	if ( !SetStatTrakDisplayOverrideOnItemView( m_pItem, iSlot ) )
		return;

	Msg( "[StatClock] Selected Kill Eater display slot %d\n", iSlot );

	if ( m_pItem->IsValid() )
	{
		KeyValues *pKey = new KeyValues( "SelectionReturned" );
		pKey->SetUint64( "itemindex", m_pItem->GetItemID() );
		PostMessage( GetParent(), pKey );
	}
}
// ===== END STAT CLOCK ENHANCEMENT: COUNTER SELECT DIALOG =====
