/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: GameWindow.cpp ///////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Westwood Studios Pacific.
//
//                       Confidential Information
//                Copyright (C) 2001 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   RTS3
//
// File name: GameWindow.cpp
//
// Created:   Dean Iverson, March 1998
//						Colin Day, June 2001
//
// Desc:      Game window implementation
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

// USER INCLUDES //////////////////////////////////////////////////////////////
#include "Common/AudioEventRTS.h"
#include "Common/Language.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/Mouse.h"
#include "GameClient/SelectionXlat.h"
#include "GameClient/GameWindowTransitions.h"

// SandboxRTS-android: missing .wnd windows come back as nullptr; make window calls on them no-ops.
static inline bool gxIsNullWin(const void *p) { return p == nullptr; }

// DEFINES ////////////////////////////////////////////////////////////////////

// PRIVATE TYPES //////////////////////////////////////////////////////////////

// PRIVATE DATA ///////////////////////////////////////////////////////////////

// PUBLIC DATA ////////////////////////////////////////////////////////////////

// PRIVATE PROTOTYPES /////////////////////////////////////////////////////////

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////

// GameWindow::GameWindow =====================================================
//=============================================================================
GameWindow::GameWindow()
{
	m_status = WIN_STATUS_NONE;

	m_size.x = 0;
	m_size.y = 0;

	m_region.lo.x = 0;
	m_region.lo.y = 0;
	m_region.hi.x = 0;
	m_region.hi.y = 0;

	m_cursorX = 0;
	m_cursorY = 0;

	m_userData = nullptr;

	m_inputData = nullptr;

	winSetDrawFunc( TheWindowManager->getDefaultDraw() );
	winSetInputFunc( TheWindowManager->getDefaultInput() );
	winSetSystemFunc( TheWindowManager->getDefaultSystem() );
	// We use to set the default tooltip func to TheWindowManager->getDefaultTooltip()
	// but I removed this so that we can set in GUI edit a text string that will be the
	// default tool tip for a control.
	winSetTooltipFunc( nullptr );

	m_next = nullptr;
	m_prev = nullptr;
	m_parent = nullptr;
	m_child = nullptr;

	m_nextLayout = nullptr;
	m_prevLayout = nullptr;
	m_layout = nullptr;

	m_editData = nullptr;

}

// GameWindow::~GameWindow ====================================================
//=============================================================================
GameWindow::~GameWindow()
{

	delete m_inputData;
	m_inputData = nullptr;

	delete m_editData;
	m_editData = nullptr;

	unlinkFromTransitionWindows();

}

// GameWindow::linkTransitionWindow ============================================
//=============================================================================
void GameWindow::linkTransitionWindow( TransitionWindow* transitionWindow )
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	m_transitionWindows.push_back(transitionWindow);

}

// GameWindow::unlinkTransitionWindow =========================================
//=============================================================================
void GameWindow::unlinkTransitionWindow( TransitionWindow* transitionWindow )
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	std::vector<TransitionWindow*>::iterator it = m_transitionWindows.begin();
	while ( it != m_transitionWindows.end() )
	{
		if ( *it == transitionWindow )
		{
			*it = m_transitionWindows.back();
			m_transitionWindows.pop_back();
			return;
		}
		++it;
	}

}

// GameWindow::unlinkFromTransitionWindows =========================================
//=============================================================================
void GameWindow::unlinkFromTransitionWindows()
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	while ( !m_transitionWindows.empty() )
	{
		m_transitionWindows.back()->unlinkGameWindow(this);
		m_transitionWindows.pop_back();
	}

}

// GameWindow::normalizeWindowRegion ==========================================
/** Puts the upper left corner in the window's region.lo field */
//=============================================================================
void GameWindow::normalizeWindowRegion()
{
	if (gxIsNullWin(this)) return; /*gx-null*/
	Int temp;

	if( m_region.lo.x > m_region.hi.x)
	{

		temp = m_region.lo.x;
		m_region.lo.x = m_region.hi.x;
		m_region.hi.x = temp;

	}

	if( m_region.lo.y > m_region.hi.y )
	{

		temp = m_region.lo.y;
		m_region.lo.y = m_region.hi.y;
		m_region.hi.y = temp;

	}

}

// GameWindow::findFirstLeaf ==================================================
/** Returns the first leaf of the branch */
//=============================================================================
GameWindow *GameWindow::findFirstLeaf()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/
	GameWindow *leaf = this;

	// Find the root of this branch
	while( leaf->m_parent )
		leaf = leaf->m_parent;

	// Find the first leaf
	while( leaf->m_child )
		leaf = leaf->m_child;

	return leaf;

}

// GameWindow::findLastLeaf ===================================================
/** Returns the last leaf of the branch */
//=============================================================================
GameWindow *GameWindow::findLastLeaf()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/
	GameWindow *leaf = this;

	// Find the root of this branch
	while( leaf->m_parent )
		leaf = leaf->m_parent;

	// Find the last leaf
	while( leaf->m_child )
	{

		leaf = leaf->m_child;

		while( leaf->m_next )
			leaf = leaf->m_next;

	}

	return leaf;

}

// GameWindow::findPrevLeaf ===================================================
/** Returns the prev leaf of the tree */
//=============================================================================
GameWindow *GameWindow::findPrevLeaf()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/
	GameWindow *leaf = this;

	if( leaf->m_prev )
	{

		leaf = leaf->m_prev;

		while( leaf->m_child &&
					 BitIsSet( leaf->m_status, WIN_STATUS_TAB_STOP ) == FALSE )
		{

			leaf = leaf->m_child;

			while( leaf->m_next )
				leaf = leaf->m_next;

		}

		return leaf;

	}
	else
	{

		while( leaf->m_parent )
		{

			leaf = leaf->m_parent;

			if( leaf->m_parent && leaf->m_prev )
			{

				leaf = leaf->m_prev;

				while( leaf->m_child &&
							 BitIsSet( leaf->m_status, WIN_STATUS_TAB_STOP ) == FALSE )
				{

					leaf = leaf->m_child;

					while( leaf->m_next )
						leaf = leaf->m_next;

				}

				return leaf;

			}

		}

		if( leaf )
			return leaf->findLastLeaf();
		else
			return nullptr;

	}

	return nullptr;

}

// GameWindow::findNextLeaf ===================================================
/** Returns the next leaf of the tree */
//=============================================================================
GameWindow *GameWindow::findNextLeaf()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/
	GameWindow *leaf = this;

	if( leaf->m_next )
	{

		if( leaf->m_next->m_status & WIN_STATUS_TAB_STOP )
			return leaf->m_next;

		for( leaf = leaf->m_next; leaf; leaf = leaf->m_child )
			if( leaf->m_child == nullptr || BitIsSet( leaf->m_status,
																						WIN_STATUS_TAB_STOP ) )
				return leaf;

	}
	else
	{

		while( leaf->m_parent )
		{

			leaf = leaf->m_parent;

			if( leaf->m_parent && leaf->m_next )
			{

				for( leaf = leaf->m_next; leaf; leaf = leaf->m_child )
					if( leaf->m_child == nullptr ||
							BitIsSet( leaf->m_status, WIN_STATUS_TAB_STOP ) )
						return leaf;

			}

		}

		if( leaf )
			return leaf->findFirstLeaf();
		else
			return nullptr;

	}

	return nullptr;

}

// GameWindow::winNextTab =====================================================
/** Go to next window in tab chain */
//=============================================================================
Int GameWindow::winNextTab()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/
/*
	GameWindow *newTab = this;
	Bool firstTry = TRUE;

	// Un-hilite the current window
	m_instData.m_state &= ~WIN_STATE_HILITED;

	do
	{

		if( m_parent == nullptr && firstTry )
		{

			newTab = findLastLeaf( newTab );
			firstTry = FALSE;

		}
		else
			newTab = findPrevLeaf( newTab );
	} while( ( isEnabled( newTab ) == FALSE ) ||
					 ( isHidden( newTab ) ) );

	newTab->instData.state |= WIN_STATE_HILITED;
	WinSetFocus( newTab );

*/
	return WIN_ERR_OK;

}

// GameWindow::winPrevTab =====================================================
/** Go to previous window in tab chain */
//=============================================================================
Int GameWindow::winPrevTab()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/
/*
	GameWindow *newTab = this;
	Bool firstTry = TRUE;

	// Un-hilite the current window
	m_instData.m_state &= ~WIN_STATE_HILITED;

	do
	{

		if( m_parent == nullptr && firstTry )
		{

			newTab = findFirstLeaf( newTab );
			firstTry = FALSE;

		}
		else
			newTab = findNextLeaf( newTab );

	} while( ( isEnabled( newTab ) == FALSE ) ||
					 ( isHidden( newTab ) ) );

	newTab->instData.state |= WIN_STATE_HILITED;
	WinSetFocus( newTab );

*/

	return WIN_ERR_OK;

}

// GameWindow::winBringToTop ==================================================
/** Bring this window to the top of the window list, if we have a parent
	* we will go to the top of the child list for that parent */
//=============================================================================
Int GameWindow::winBringToTop()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/
	GameWindow *current;
	GameWindow *parent = winGetParent();

	if( parent )
	{

		TheWindowManager->unlinkChildWindow( this );
		TheWindowManager->addWindowToParent( this, parent );
//		TheWindowManager->addWindowToParentAtEnd( this, parent );

	}
	else
	{

		// sanity, make sure this window is in the window list
		for( current = TheWindowManager->winGetWindowList();
				 current != this;
				 current = current->m_next)
			if (current == nullptr)
				return WIN_ERR_INVALID_PARAMETER;

		// move to head of windowList
		TheWindowManager->unlinkWindow( this );
		TheWindowManager->linkWindow( this );

	}

	//
	// if the window is part of a screen layout, move it to the top
	// of the screen layout to reflect the new position of the window
	// in the real window list (it's all about draw order :) )
	//
	if( m_layout )
	{
		WindowLayout *saveLayout = m_layout;

		//
		// note we must use saveScreen because removing the window from the
		// screen will clear the m_screen member (as it should for removing
		// a window from a screen)
		//
		saveLayout->removeWindow( this );
		saveLayout->addWindow( this );

	}

	return WIN_ERR_OK;

}

// GameWindow::winActivate ====================================================
/** Pop window to top of window list AND activate it */
//=============================================================================
Int GameWindow::winActivate()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/
	Int returnCode;

	// bring window to top
	returnCode = winBringToTop();
	if( returnCode != WIN_ERR_OK )
		return returnCode;

	// activate it and unhide
	BitSet( m_status, WIN_STATUS_ACTIVE );
	winHide( FALSE );

	return WIN_ERR_OK;

}

// GameWindow::winSetPosition =================================================
/** Set the window's position */
//=============================================================================
Int GameWindow::winSetPosition( Int x, Int y )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	m_region.lo.x = x;
	m_region.lo.y = y;

	m_region.hi.x = x + m_size.x;
	m_region.hi.y = y + m_size.y;

	normalizeWindowRegion();

	return WIN_ERR_OK;

}

// WinGetPosition =============================================================
/** Get the window's position */
//=============================================================================
Int GameWindow::winGetPosition( Int *x, Int *y )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	// sanity
	if( x == nullptr || y == nullptr )
		return WIN_ERR_INVALID_PARAMETER;

	*x = m_region.lo.x;
	*y = m_region.lo.y;

	return WIN_ERR_OK;

}

// WinSetCursorPosition =============================================================
/** Set the window's cursor position */
//=============================================================================
Int GameWindow::winSetCursorPosition( Int x, Int y )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/
	m_cursorX = x;
	m_cursorY = y;

	return WIN_ERR_OK;

}

// WinGetCursorPosition =============================================================
/** Get the window's cursor position */
//=============================================================================
Int GameWindow::winGetCursorPosition( Int *x, Int *y )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/
	if ( x )
	{
		*x = m_cursorX;
	}

	if ( y )
	{
		*y = m_cursorY;
	}

	return WIN_ERR_OK;

}

// GameWindow::winGetScreenPosition ===========================================
/** Get the window's position in screen coordinates */
//=============================================================================
Int GameWindow::winGetScreenPosition( Int *x, Int *y )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/
	GameWindow *parent = m_parent;

	*x = m_region.lo.x;
	*y = m_region.lo.y;

	while( parent )
	{

		*x += parent->m_region.lo.x;
		*y += parent->m_region.lo.y;
		parent = parent->m_parent;

	}

	return WIN_ERR_OK;

}

// GameWindow::winGetRegion ===================================================
/** Get the window region */
//=============================================================================
Int GameWindow::winGetRegion( IRegion2D *region )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	if( region )
		*region = m_region;

	return WIN_ERR_OK;

}

// GameWindow::winPointInWindow ===============================================
/** Check to see if the given point is inside the window.  Will
	* still return true if the point is actually in a child. */
//=============================================================================
Bool GameWindow::winPointInWindow( Int x, Int y )
{
	if (gxIsNullWin(this)) return FALSE; /*gx-null*/
	Int winX, winY, width, height;

	winGetScreenPosition( &winX, &winY );
	winGetSize( &width, &height );

	if (x >= winX && x <= winX + width &&
			y >= winY && y <= winY + height)
		return TRUE;

	return FALSE;

}

// GameWindow::winSetSize =====================================================
/** Set the window's size */
//=============================================================================
Int GameWindow::winSetSize( Int width, Int height )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	m_size.x = width;
	m_size.y = height;
	m_region.hi.x = m_region.lo.x + width;
	m_region.hi.y = m_region.lo.y + height;

	TheWindowManager->winSendSystemMsg( this,
																			GGM_RESIZED,
																			(WindowMsgData)width,
																			(WindowMsgData)height );

	return WIN_ERR_OK;

}

// GameWindow::winGetSize =====================================================
/** Get the window's size */
//=============================================================================
Int GameWindow::winGetSize( Int *width, Int *height )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	// sanity
	if( width == nullptr || height == nullptr )
		return WIN_ERR_INVALID_PARAMETER;

	*width  = m_size.x;
	*height = m_size.y;

	return WIN_ERR_OK;

}

// GameWindow::winEnable ======================================================
/** Enable or disable a window based on the enable parameter.
	* A disabled window can be seen but accepts no input. */
//=============================================================================
Int GameWindow::winEnable( Bool enable )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/
	GameWindow *child;

	if( enable )
		BitSet( m_status, WIN_STATUS_ENABLED );
	else
		BitClear( m_status, WIN_STATUS_ENABLED );

	if( m_child )
	{

		for( child = m_child; child; child = child->m_next)
			child->winEnable( enable );

	}

	return WIN_ERR_OK;

}

// GameWindow::winGetEnabled ======================================================
/** Enable or disable a window based on the enable parameter.
	* A disabled window can be seen but accepts no input. */
//=============================================================================
Bool GameWindow::winGetEnabled()
{
	if (gxIsNullWin(this)) return FALSE; /*gx-null*/
  return BitIsSet( m_status, WIN_STATUS_ENABLED );

}

// GameWindow::winHide ========================================================
/** Hide or show a window based on the hide parameter.
	* A hidden window can't be seen and accepts no input. */
//=============================================================================
Int GameWindow::winHide( Bool hide )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	if( hide )
	{

		//
		// if we're running in small game window mode and this window becomes
		// invisible then there's a good chance that the black border around
		// the game window needs redrawing
		//
		if( !BitIsSet( m_status, WIN_STATUS_NO_FLUSH ) )
			freeImages();

		BitSet( m_status, WIN_STATUS_HIDDEN );

		// notify the window manger we are hiding
		TheWindowManager->windowHiding( this );

	}
	else
	{

		BitClear( m_status, WIN_STATUS_HIDDEN );

	}

	return WIN_ERR_OK;

}

// GameWindow::winIsHidden ====================================================
/** Am I hidden? */
//=============================================================================
Bool GameWindow::winIsHidden()
{
	if (gxIsNullWin(this)) return FALSE; /*gx-null*/

	return BitIsSet( m_status, WIN_STATUS_HIDDEN );

}

// GameWindow::winSetStatus ===================================================
/** Allows the user to directly set a window's status flags. */
//=============================================================================
UnsignedInt GameWindow::winSetStatus( UnsignedInt status )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/
	UnsignedInt oldStatus;

	oldStatus = m_status;
	BitSet( m_status, status );
//	m_status = status;

	return oldStatus;

}

// GameWindow::winClearStatus =================================================
/** Allows the user to directly clear a window's status flags. */
//=============================================================================
UnsignedInt GameWindow::winClearStatus( UnsignedInt status )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/
	UnsignedInt oldStatus;

	oldStatus = m_status;
	BitClear( m_status, status );

	return oldStatus;

}

// GameWindow::winGetStatus ===================================================
/** Returns a window's status flags. */
//=============================================================================
UnsignedInt GameWindow::winGetStatus()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	return m_status;

}

// GameWindow::winGetStyle ====================================================
/** Returns a window's style flags. */
//=============================================================================
UnsignedInt GameWindow::winGetStyle()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	return m_instData.m_style;

}

// GameWindow::winSetHiliteState ==============================================
/** Set whether window is highlighted or not */
//=============================================================================
void GameWindow::winSetHiliteState( Bool state )
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	if( state )
		BitSet( m_instData.m_state, WIN_STATE_HILITED );
	else
		BitClear( m_instData.m_state, WIN_STATE_HILITED );

}

// GameWindow::winSetDrawOffset ===============================================
/** Set offset for drawing images */
//=============================================================================
void GameWindow::winSetDrawOffset( Int x, Int y )
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	m_instData.m_imageOffset.x = x;
	m_instData.m_imageOffset.y = y;

}

// GameWindow::winGetDrawOffset ===============================================
/** Get offset for drawing images */
//=============================================================================
void GameWindow::winGetDrawOffset( Int *x, Int *y )
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	// sanity
	if( x == nullptr || y == nullptr )
		return;

	*x = m_instData.m_imageOffset.x;
	*y = m_instData.m_imageOffset.y;

}

// GameWindow::winSetText =====================================================
/** Sets the text in a window */
//=============================================================================
Int GameWindow::winSetText( UnicodeString newText )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/
	// copy text over
	m_instData.setText( newText );

	return WIN_ERR_OK;

}

// GameWindow::winGetText =====================================================
/** Get text from a window ... this works for static text windows and
	* edit boxes */
//=============================================================================
UnicodeString GameWindow::winGetText()
{
	if (gxIsNullWin(this)) return UnicodeString::TheEmptyString; /*gx-null*/
	// return the contents of our text field
	return m_instData.getText();

}

// GameWindow::winGetTextLength =====================================================
//=============================================================================
Int GameWindow::winGetTextLength()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/
	// return the contents of our text field
	return m_instData.getTextLength();

}

// GameWindow::winGetFont =====================================================
/** Get the font being used by this window */
//=============================================================================
GameFont *GameWindow::winGetFont()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/

	return m_instData.getFont();

}

// GameWindow::winSetFont =====================================================
/** Set font for text in this window */
//=============================================================================
void GameWindow::winSetFont( GameFont *font )
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	// set font in window member
	m_instData.m_font = font;

	// set font for other display strings in special gadget window controls
	if( BitIsSet( m_instData.getStyle(), GWS_SCROLL_LISTBOX ) )
		GadgetListBoxSetFont( this, font );
	else if( BitIsSet( m_instData.getStyle(), GWS_COMBO_BOX ) )
		GadgetComboBoxSetFont( this, font );
	else if( BitIsSet( m_instData.getStyle(), GWS_ENTRY_FIELD ) )
		GadgetTextEntrySetFont( this, font );
	else if( BitIsSet( m_instData.getStyle(), GWS_STATIC_TEXT ) )
		GadgetStaticTextSetFont( this, font );
	else
	{
		DisplayString *dString;

		// set the font for the display strings all windows have
		dString = m_instData.getTextDisplayString();
		if( dString )
			dString->setFont( font );
		dString = m_instData.getTooltipDisplayString();
		if( dString )
			dString->setFont( font );

	}

}

// GameWindow::winSetEnabledTextColors ========================================
/** Set the text colors for the enabled state */
//=============================================================================
void GameWindow::winSetEnabledTextColors( Color color, Color borderColor )
{
	if (gxIsNullWin(this)) return; /*gx-null*/
	m_instData.m_enabledText.color = color;
	m_instData.m_enabledText.borderColor = borderColor;

	if( BitIsSet( m_instData.getStyle(), GWS_COMBO_BOX ) )
		GadgetComboBoxSetEnabledTextColors(this,  color, borderColor );


}

// GameWindow::winSetDisabledTextColors =======================================
/** Set the text colors for the disabled state */
//=============================================================================
void GameWindow::winSetDisabledTextColors( Color color, Color borderColor )
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	m_instData.m_disabledText.color = color;
	m_instData.m_disabledText.borderColor = borderColor;

	if( BitIsSet( m_instData.getStyle(), GWS_COMBO_BOX ) )
		GadgetComboBoxSetDisabledTextColors( this, color, borderColor );

}

// GameWindow::winSetHiliteTextColors =========================================
/** Set the text colors for the Hilite state */
//=============================================================================
void GameWindow::winSetHiliteTextColors( Color color, Color borderColor )
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	m_instData.m_hiliteText.color = color;
	m_instData.m_hiliteText.borderColor = borderColor;

	if( BitIsSet( m_instData.getStyle(), GWS_COMBO_BOX ) )
		GadgetComboBoxSetHiliteTextColors( this, color, borderColor );

}

// GameWindow::winSetIMECompositeTextColors =========================================
/** Set the text colors for the IME Composite state */
//=============================================================================
void GameWindow::winSetIMECompositeTextColors( Color color, Color borderColor )
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	m_instData.m_imeCompositeText.color = color;
	m_instData.m_imeCompositeText.borderColor = borderColor;

	if( BitIsSet( m_instData.getStyle(), GWS_COMBO_BOX ) )
		GadgetComboBoxSetIMECompositeTextColors( this, color, borderColor );
}

// GameWindow::winGetEnabledTextColor =========================================
/** Get the enabled text color */
//=============================================================================
Color GameWindow::winGetEnabledTextColor()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	return m_instData.m_enabledText.color;

}

// GameWindow::winGetEnabledTextBorderColor ===================================
/** Get the enabled text color */
//=============================================================================
Color GameWindow::winGetEnabledTextBorderColor()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	return m_instData.m_enabledText.borderColor;

}

// GameWindow::winGetDisabledTextColor ========================================
/** Get the disabled text color */
//=============================================================================
Color GameWindow::winGetDisabledTextColor()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	return m_instData.m_disabledText.color;

}

// GameWindow::winGetDisabledTextBorderColor ==================================
/** Get the disabled text color */
//=============================================================================
Color GameWindow::winGetDisabledTextBorderColor()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	return m_instData.m_disabledText.borderColor;

}

// GameWindow::winGetIMECompositeTextColor ==========================================
/** Get the IME composite text color */
//=============================================================================
Color GameWindow::winGetIMECompositeTextColor()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	return m_instData.m_imeCompositeText.color;

}

// GameWindow::winGetIMECompositeBorderColor ==========================================
/** Get the IME composite border color */
//=============================================================================
Color GameWindow::winGetIMECompositeBorderColor()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	return m_instData.m_imeCompositeText.borderColor;

}

// GameWindow::winGetHiliteTextColor ==========================================
/** Get the hilite text color */
//=============================================================================
Color GameWindow::winGetHiliteTextColor()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	return m_instData.m_hiliteText.color;

}

// GameWindow::winGetHiliteTextBorderColor ====================================
/** Get the hilite text color */
//=============================================================================
Color GameWindow::winGetHiliteTextBorderColor()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	return m_instData.m_hiliteText.borderColor;

}

// GameWindow::winSetInstanceData =============================================
/** Sets the window's instance data which includes parameters
	* such as background color. */
//=============================================================================
Int GameWindow::winSetInstanceData( WinInstanceData *data )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/
	DisplayString *text, *tooltipText;

	// save our own instance of text and tooltip text display strings
	text = m_instData.m_text;
	tooltipText = m_instData.m_tooltip;

	// copy over all values from the inst data passed in
	// using memcpy is VERY VERY bad here, since the strings
	// must be copied 'correctly' or bad things will ensue
	m_instData = *data;

	// put our text instance pointers back
	m_instData.m_text = text;
	m_instData.m_tooltip = tooltipText;

	// make sure we didn't try to copy over a video buffer.
	m_instData.m_videoBuffer = nullptr;

	// set our text display instance text if present
	if( data->getTextLength() )
		m_instData.setText( data->getText() );
	if( data->getTooltipTextLength() )
		m_instData.setTooltipText( data->getTooltipText() );

	return WIN_ERR_OK;

}

// GameWindow::winGetInstanceData =============================================
/** Return pointer to the instance data for this window */
//=============================================================================
WinInstanceData *GameWindow::winGetInstanceData()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/

	return &m_instData;

}

// GameWindow::winGetUserData =================================================
/** Return the user data stored */
//=============================================================================
void *GameWindow::winGetUserData()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/

	return m_userData;

}

// GameWindow::winSetUserData =================================================
/** Set the user data stored */
//=============================================================================
void GameWindow::winSetUserData( void *data )
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	m_userData = data;

}

// GameWindow::winSetTooltip ==================================================
/** Sets the window's tooltip text */
//=============================================================================
void GameWindow::winSetTooltip( UnicodeString tip )
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	m_instData.setTooltipText( tip );

}

// GameWindow::winSetWindowId =================================================
/** Sets the window's id */
//=============================================================================
Int GameWindow::winSetWindowId( Int id )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	m_instData.m_id = id;

	return WIN_ERR_OK;

}

// GameWindow::winGetWindowId =================================================
/** Gets the window's id */
//=============================================================================
Int GameWindow::winGetWindowId()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	return m_instData.m_id;

}

// GameWindow::winSetParent ===================================================
/** Sets this window's parent */
//=============================================================================
Int GameWindow::winSetParent( GameWindow *parent )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	if( m_parent == nullptr)
	{
		// Top level window so unlink it
		TheWindowManager->unlinkWindow( this );
	}
	else
	{
		// A child window
		TheWindowManager->unlinkChildWindow( this );
	}

	if( parent == nullptr )
	{

		// Want to make it a top level window so add to window list
		TheWindowManager->linkWindow( this );
		m_parent = nullptr;

	}
	else
	{

		// Set it's new parent
		TheWindowManager->addWindowToParent( this, parent );

	}

	return WIN_ERR_OK;

}

// GameWindow::winGetParent ===================================================
/** Gets the window's parent */
//=============================================================================
GameWindow *GameWindow::winGetParent()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/

	return m_parent;

}

// GameWindow::winIsChild =====================================================
/** Determines if a window is a child/grand-child of a parent */
//=============================================================================
Bool GameWindow::winIsChild( GameWindow *child )
{
	if (gxIsNullWin(this)) return FALSE; /*gx-null*/

	while( child )
	{

		if( this == child->m_parent )
			return TRUE;

		// set up tree
		child = child->m_parent;

	}

	return FALSE;

}

// GameWindow::winGetChild ====================================================
/** Get the child window of this window */
//=============================================================================
GameWindow *GameWindow::winGetChild()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/

	return m_child;

}

// GameWindow::winSetOwner ====================================================
/** Sets the window's owner */
//=============================================================================
Int GameWindow::winSetOwner( GameWindow *owner )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	if( owner == nullptr )
		m_instData.m_owner = this;
	else
		m_instData.m_owner = owner;

	return WIN_ERR_OK;

}

// GameWindow::winGetOwner ====================================================
/** Gets the window's owner */
//=============================================================================
GameWindow *GameWindow::winGetOwner()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/

	return m_instData.getOwner();

}

// GameWindow::winSetNext =====================================================
/** Set next pointer */
//=============================================================================
void GameWindow::winSetNext( GameWindow *next )
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	m_next = next;

}

// GameWindow::winGetNext =====================================================
/** Gets the next window */
//=============================================================================
GameWindow *GameWindow::winGetNext()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/

	return m_next;

}

// GameWindow::winSetPrev =====================================================
/** Set prev pointer */
//=============================================================================
void GameWindow::winSetPrev( GameWindow *prev )
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	m_prev = prev;

}

// GameWindow::winGetPrev =====================================================
/** Get the previous window */
//=============================================================================
GameWindow *GameWindow::winGetPrev()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/

	return m_prev;

}

// GameWindow::winSetNextInLayout =============================================
/** Set next window in layout */
//=============================================================================
void GameWindow::winSetNextInLayout( GameWindow *next )
{
	if (gxIsNullWin(this)) return; /*gx-null*/
	m_nextLayout = next;
}

// GameWindow::winSetPrevInLayout =============================================
/** Set previous window in layout pointer */
//=============================================================================
void GameWindow::winSetPrevInLayout( GameWindow *prev )
{
	if (gxIsNullWin(this)) return; /*gx-null*/
	m_prevLayout = prev;
}

// GameWindow::winSetLayout ===================================================
/** Set this window as belonging to layout 'layout' */
//=============================================================================
void GameWindow::winSetLayout( WindowLayout *layout )
{
	if (gxIsNullWin(this)) return; /*gx-null*/
	m_layout = layout;
}

// GameWindow::winGetLayout ===================================================
/** Get layout this window is a part of, if any */
//=============================================================================
WindowLayout *GameWindow::winGetLayout()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/
	return m_layout;
}

// GameWindow::winGetNextInLayout =============================================
/** Get next window in layout list if any */
//=============================================================================
GameWindow *GameWindow::winGetNextInLayout()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/
	return m_nextLayout;
}

// GameWindow::winGetPrevInLayout =============================================
/** Get prev window in layout list if any */
//=============================================================================
GameWindow *GameWindow::winGetPrevInLayout()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/
	return m_prevLayout;
}

// GameWindow::winSetSystemFunc ===============================================
/** Sets the window's input, system, and redraw callback functions. */
//=============================================================================
Int GameWindow::winSetSystemFunc( GameWinSystemFunc system )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/
	if( system )
		m_system = system;
	else
		m_system = TheWindowManager->getDefaultSystem();

	return WIN_ERR_OK;

}

// GameWindow::winSetInputFunc ================================================
/** Sets the window's input callback functions. */
//=============================================================================
Int GameWindow::winSetInputFunc( GameWinInputFunc input )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	if( input )
		m_input = input;
	else
		m_input = TheWindowManager->getDefaultInput();

	return WIN_ERR_OK;

}

// GameWindow::winSetDrawFunc =================================================
/** Sets the window's redraw callback functions. */
//=============================================================================
Int GameWindow::winSetDrawFunc( GameWinDrawFunc draw )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	if( draw )
		m_draw = draw;
	else
		m_draw = TheWindowManager->getDefaultDraw();

	return WIN_ERR_OK;

}

// GameWindow::winSetTooltipFunc ==============================================
/** Sets a window's tooltip callback */
//=============================================================================
Int GameWindow::winSetTooltipFunc( GameWinTooltipFunc tooltip )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	m_tooltip = tooltip;

	return WIN_ERR_OK;

}

// GameWindow::winSetCallbacks ================================================
/** Sets the window's input, tooltip, and redraw callback functions. */
//=============================================================================
Int GameWindow::winSetCallbacks( GameWinInputFunc input,
																 GameWinDrawFunc draw,
																 GameWinTooltipFunc tooltip )
{

	winSetInputFunc( input );
	winSetDrawFunc( draw );
	winSetTooltipFunc( tooltip );

	return WIN_ERR_OK;

}

// GameWindow::winDrawWindow ==================================================
/** Draws the default background for the specified window. */
//=============================================================================
Int GameWindow::winDrawWindow()
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	if( BitIsSet( m_status, WIN_STATUS_HIDDEN ) == FALSE && m_draw )
		m_draw( this, &m_instData );

	return WIN_ERR_OK;

}

// GameWindow::winPointInChild ================================================
/** Given a window and the mouse coordinates, return the child
	* window which contains the mouse pointer.  Child windows are
	* relative to their parents */
//=============================================================================
GameWindow *GameWindow::winPointInChild( Int x, Int y, Bool ignoreEnableCheck, Bool playDisabledSound )
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/
	GameWindow *parent;
	GameWindow *child;
	ICoord2D origin;

	for( child = m_child; child; child = child->m_next )
	{

		origin = child->m_region.lo;
		parent = child->winGetParent();

		while( parent )
		{

			origin.x += parent->m_region.lo.x;
			origin.y += parent->m_region.lo.y;
			parent = parent->m_parent;

		}

		if( x >= origin.x && x <= origin.x + child->m_size.x &&
				y >= origin.y && y <= origin.y + child->m_size.y )
		{
			Bool enabled = ignoreEnableCheck || BitIsSet( child->m_status, WIN_STATUS_ENABLED );
			Bool hidden = BitIsSet( child->m_status, WIN_STATUS_HIDDEN );
			if( !hidden )
			{
				if( enabled )
				{
					return child->winPointInChild( x, y, ignoreEnableCheck, playDisabledSound );
				}
				else if( playDisabledSound )
				{
					AudioEventRTS disabledClick( "GUIClickDisabled" );
					if( TheAudio )
					{
						TheAudio->addAudioEvent( &disabledClick );
					}
				}
			}
		}

	}

	// not in any children, must be in parent
	return this;

}

// GameWindow::winPointInAnyChild =============================================
/** Find the child in which the cursor resides; regardless of
	* whether or not the window is actually enabled */
//=============================================================================
GameWindow *GameWindow::winPointInAnyChild( Int x, Int y, Bool ignoreHidden, Bool ignoreEnableCheck )
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/
	GameWindow *parent;
	GameWindow *child;
	ICoord2D origin;

	for( child = m_child; child; child = child->m_next )
	{

		origin = child->m_region.lo;
		parent = child->m_parent;

		while( parent )
		{

			origin.x += parent->m_region.lo.x;
			origin.y += parent->m_region.lo.y;
			parent = parent->m_parent;

		}

		if( x >= origin.x && x <= origin.x + child->m_size.x &&
				y >= origin.y && y <= origin.y + child->m_size.y )
		{

			if( !(ignoreHidden == TRUE &&	BitIsSet( child->m_status, WIN_STATUS_HIDDEN )) )
				return child->winPointInChild( x, y, ignoreEnableCheck );

		}

	}

	// not in any children, must be in parent
	return this;

}

//
// In release builds the default input and system functions are optimized
// to the same address since they take the same input and have the same
// body.  Rather than fill them with bogus code we just want to make
// sure that different functions are actually created.  If you change the
// body of one but not the other so they are different, please remove
// the dummy code
//

// GameWinDefaultInput ========================================================
/** The default input callback.  Currently does nothing. */
//=============================================================================
WindowMsgHandledType GameWinDefaultInput( GameWindow *window, UnsignedInt msg,
													WindowMsgData mData1, WindowMsgData mData2 )
{

	return MSG_IGNORED;

}

///< Input that blocks all (mouse) input like a wall, instead of passing like it wasn't there
WindowMsgHandledType GameWinBlockInput( GameWindow *window, UnsignedInt msg,
													WindowMsgData mData1, WindowMsgData mData2 )
{
	if (msg == GWM_CHAR || msg == GWM_MOUSE_POS)
		return MSG_IGNORED;

	if (msg == GWM_LEFT_UP )//|| msg == GWM_LEFT_DRAG)
	{
		//stop drag selecting

		TheSelectionTranslator->setLeftMouseButton(FALSE);
		TheSelectionTranslator->setDragSelecting(FALSE);

		TheTacticalView->setMouseLock( FALSE );
		TheInGameUI->setSelecting( FALSE );
		TheInGameUI->endAreaSelectHint(nullptr);

	}

	return MSG_HANDLED;

}

// GameWinDefaultSystem =======================================================
/** The default system callback.  Currently does nothing. */
//=============================================================================
WindowMsgHandledType GameWinDefaultSystem( GameWindow *window, UnsignedInt msg,
													 WindowMsgData mData1, WindowMsgData mData2 )
{

	return MSG_IGNORED;

}

// GameWinDefaultTooltip ======================================================
/** Default tooltip callback */
//=============================================================================
void GameWinDefaultTooltip( GameWindow *window,
														WinInstanceData *instData,
														UnsignedInt mouse )
{

}

// GameWinDefaultDraw =========================================================
/** Default draw, does nothing */
//=============================================================================
void GameWinDefaultDraw( GameWindow *window, WinInstanceData *instData )
{

}

// GameWindow::winSetEnabledImage =============================================
/** Set an enabled image into the draw data for the enabled state */
//=============================================================================
Int GameWindow::winSetEnabledImage( Int index, const Image *image )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	// sanity
	if( index < 0 || index >= MAX_DRAW_DATA )
	{

		DEBUG_LOG(( "set enabled image, index out of range '%d'", index ));
		assert( 0 );
		return WIN_ERR_INVALID_PARAMETER;

	}

	m_instData.m_enabledDrawData[ index ].image = image;
	return WIN_ERR_OK;

}

// GameWindow::winSetEnabledColor =============================================
/** set color for enabled state at index */
//=============================================================================
Int GameWindow::winSetEnabledColor( Int index, Color color )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	// sanity
	if( index < 0 || index >= MAX_DRAW_DATA )
	{

		DEBUG_LOG(( "set enabled color, index out of range '%d'", index ));
		assert( 0 );
		return WIN_ERR_INVALID_PARAMETER;

	}

	m_instData.m_enabledDrawData[ index ].color = color;
	return WIN_ERR_OK;

}

// GameWindow::winSetEnabledBorderColor =======================================
/** set border color for state at this index */
//=============================================================================
Int GameWindow::winSetEnabledBorderColor( Int index, Color color )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	// sanity
	if( index < 0 || index >= MAX_DRAW_DATA )
	{

		DEBUG_LOG(( "set enabled border color, index out of range '%d'", index ));
		assert( 0 );
		return WIN_ERR_INVALID_PARAMETER;

	}

	m_instData.m_enabledDrawData[ index ].borderColor = color;
	return WIN_ERR_OK;

}

// GameWindow::winSetDisabledImage ============================================
/** Set an disabled image into the draw data for the disabled state */
//=============================================================================
Int GameWindow::winSetDisabledImage( Int index, const Image *image )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	// sanity
	if( index < 0 || index >= MAX_DRAW_DATA )
	{

		DEBUG_LOG(( "set disabled image, index out of range '%d'", index ));
		assert( 0 );
		return WIN_ERR_INVALID_PARAMETER;

	}

	m_instData.m_disabledDrawData[ index ].image = image;
	return WIN_ERR_OK;

}

// GameWindow::winSetDisabledColor ============================================
/** set color for disabled state at index */
//=============================================================================
Int GameWindow::winSetDisabledColor( Int index, Color color )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	// sanity
	if( index < 0 || index >= MAX_DRAW_DATA )
	{

		DEBUG_LOG(( "set disabled color, index out of range '%d'", index ));
		assert( 0 );
		return WIN_ERR_INVALID_PARAMETER;

	}

	m_instData.m_disabledDrawData[ index ].color = color;
	return WIN_ERR_OK;

}

// GameWindow::winSetDisabledBorderColor ======================================
/** set border color for state at this index */
//=============================================================================
Int GameWindow::winSetDisabledBorderColor( Int index, Color color )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	// sanity
	if( index < 0 || index >= MAX_DRAW_DATA )
	{

		DEBUG_LOG(( "set disabled border color, index out of range '%d'", index ));
		assert( 0 );
		return WIN_ERR_INVALID_PARAMETER;

	}

	m_instData.m_disabledDrawData[ index ].borderColor = color;
	return WIN_ERR_OK;

}

// GameWindow::winSetHiliteImage ==============================================
/** Set an hilite image into the draw data for the hilite state */
//=============================================================================
Int GameWindow::winSetHiliteImage( Int index, const Image *image )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	// sanity
	if( index < 0 || index >= MAX_DRAW_DATA )
	{

		DEBUG_LOG(( "set hilite image, index out of range '%d'", index ));
		assert( 0 );
		return WIN_ERR_INVALID_PARAMETER;

	}

	m_instData.m_hiliteDrawData[ index ].image = image;
	return WIN_ERR_OK;

}

// GameWindow::winSetHiliteColor ==============================================
/** set color for hilite state at index */
//=============================================================================
Int GameWindow::winSetHiliteColor( Int index, Color color )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	// sanity
	if( index < 0 || index >= MAX_DRAW_DATA )
	{

		DEBUG_LOG(( "set hilite color, index out of range '%d'", index ));
		assert( 0 );
		return WIN_ERR_INVALID_PARAMETER;

	}

	m_instData.m_hiliteDrawData[ index ].color = color;
	return WIN_ERR_OK;

}

// GameWindow::winSetHiliteBorderColor ========================================
/** set border color for state at this index */
//=============================================================================
Int GameWindow::winSetHiliteBorderColor( Int index, Color color )
{
	if (gxIsNullWin(this)) return 0; /*gx-null*/

	// sanity
	if( index < 0 || index >= MAX_DRAW_DATA )
	{

		DEBUG_LOG(( "set hilite border color, index out of range '%d'", index ));
		assert( 0 );
		return WIN_ERR_INVALID_PARAMETER;

	}

	m_instData.m_hiliteDrawData[ index ].borderColor = color;
	return WIN_ERR_OK;

}

// GameWindow::winGetInputFunc ================================================
//=============================================================================
GameWinInputFunc GameWindow::winGetInputFunc()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/

	return m_input;

}

// GameWindow::winGetSystemFunc ===============================================
//=============================================================================
GameWinSystemFunc GameWindow::winGetSystemFunc()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/

	return m_system;

}

// GameWindow::winGetTooltipFunc ==============================================
//=============================================================================
GameWinTooltipFunc GameWindow::winGetTooltipFunc()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/

	return m_tooltip;

}

// GameWindow::winGetDrawFunc =================================================
//=============================================================================
GameWinDrawFunc GameWindow::winGetDrawFunc()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/

	return m_draw;

}

// GameWindow::winSetEditData =================================================
//=============================================================================
void GameWindow::winSetEditData( GameWindowEditData *editData )
{
	if (gxIsNullWin(this)) return; /*gx-null*/

	m_editData = editData;

}

// GameWindow::winGetEditData =================================================
//=============================================================================
GameWindowEditData *GameWindow::winGetEditData()
{
	if (gxIsNullWin(this)) return nullptr; /*gx-null*/

	return m_editData;

}
