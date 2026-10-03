/*
**	SandboxRTS: manual siege mode for tanks (GPL v3, part of the SandboxRTS engine patch).
*/
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ThingTemplate.h"
#include "Common/GameAudio.h"
#include "Common/Xfer.h"
#include "GameClient/Drawable.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object.h"
#include "GameLogic/WeaponSetType.h"
#include "GameLogic/Module/SandboxSiegeAIUpdate.h"

//-------------------------------------------------------------------------------------------------
SandboxSiegeAIUpdate::SandboxSiegeAIUpdate( Thing *thing, const ModuleData* moduleData ) : AIUpdateInterface( thing, moduleData )
{
	m_state = SIEGE_TRAVEL;
	m_startFrame = 0;
	m_doneFrame = 0;
}

//-------------------------------------------------------------------------------------------------
SandboxSiegeAIUpdate::~SandboxSiegeAIUpdate()
{
}

//-------------------------------------------------------------------------------------------------
void SandboxSiegeAIUpdate::playUnitSound( const char *name )
{
	const AudioEventRTS* snd = getObject()->getTemplate()->getPerUnitSound( name );
	if( snd && snd->getEventName().isNotEmpty() )
	{
		AudioEventRTS s = *snd;
		s.setObjectID( getObject()->getID() );
		TheAudio->addAudioEvent( &s );
	}
}

//-------------------------------------------------------------------------------------------------
void SandboxSiegeAIUpdate::showAnimFrame()
{
	const SandboxSiegeAIUpdateModuleData *d = getSandboxSiegeAIUpdateModuleData();
	Drawable *draw = getObject()->getDrawable();
	if( !draw || d->m_animFrames <= 1 )
		return;
	Real p = 1.0f;	// 0 = travel pose, 1 = siege pose
	UnsignedInt now = TheGameLogic->getFrame();
	if( (m_state == SIEGE_DEPLOYING || m_state == SIEGE_UNDEPLOYING) && m_doneFrame > m_startFrame )
	{
		p = (Real)(now - m_startFrame) / (Real)(m_doneFrame - m_startFrame);
		if( p > 1.0f ) p = 1.0f;
		if( m_state == SIEGE_UNDEPLOYING ) p = 1.0f - p;
	}
	draw->setAnimationFrame( (Int)(p * (d->m_animFrames - 1) + 0.5f) );
}

//-------------------------------------------------------------------------------------------------
void SandboxSiegeAIUpdate::setSiegeState( SandboxSiegeState s )
{
	Object *self = getObject();
	UnsignedInt now = TheGameLogic->getFrame();
	UnsignedInt total = getSandboxSiegeAIUpdateModuleData()->m_deployTime;
	if( total < 1 ) total = 1;

	// reversing a transition half way keeps the current pose
	UnsignedInt done = 0;
	if( (m_state == SIEGE_DEPLOYING || m_state == SIEGE_UNDEPLOYING) && m_doneFrame > now )
		done = (now - m_startFrame);

	switch( s )
	{
		case SIEGE_DEPLOYING:
			aiIdle( CMD_FROM_AI );	// stop where we are (also drops the current target)
			holdFire( TRUE );
			self->clearWeaponSetFlag( WEAPONSET_PLAYER_UPGRADE );
			setLocomotorGoalNone();
			if( !done ) m_startFrame = now;
			else m_startFrame = now - (total - done);
			m_doneFrame = m_startFrame + total;
			self->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK2( MODELCONDITION_PACKING, MODELCONDITION_DEPLOYED ),
																						MAKE_MODELCONDITION_MASK( MODELCONDITION_UNPACKING ) );
			playUnitSound( "Deploy" );
			break;
		case SIEGE_DEPLOYED:
			holdFire( FALSE );
			self->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_DEPLOYED ) );
			self->setWeaponSetFlag( WEAPONSET_PLAYER_UPGRADE );
			self->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK( MODELCONDITION_UNPACKING ),
																						MAKE_MODELCONDITION_MASK( MODELCONDITION_DEPLOYED ) );
			break;
		case SIEGE_UNDEPLOYING:
			aiIdle( CMD_FROM_AI );	// drop the current target
			holdFire( TRUE );
			self->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_DEPLOYED ) );
			self->clearWeaponSetFlag( WEAPONSET_PLAYER_UPGRADE );
			if( !done ) m_startFrame = now;
			else m_startFrame = now - (total - done);
			m_doneFrame = m_startFrame + total;
			self->clearAndSetModelConditionFlags( MAKE_MODELCONDITION_MASK2( MODELCONDITION_UNPACKING, MODELCONDITION_DEPLOYED ),
																						MAKE_MODELCONDITION_MASK( MODELCONDITION_PACKING ) );
			playUnitSound( "Undeploy" );
			break;
		case SIEGE_TRAVEL:
			holdFire( FALSE );
			self->clearModelConditionFlags( MAKE_MODELCONDITION_MASK( MODELCONDITION_PACKING ) );
			break;
	}
	m_state = s;
	showAnimFrame();
}

//-------------------------------------------------------------------------------------------------
// no shooting while the legs move: NO_ATTACK status + an empty weapon set (flag WEAPON_RIDER8)
void SandboxSiegeAIUpdate::holdFire( Bool hold )
{
	Object *self = getObject();
	if( hold )
	{
		self->setStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_NO_ATTACK ) );
		self->setWeaponSetFlag( WEAPONSET_RIDER8 );
	}
	else
	{
		self->clearStatus( MAKE_OBJECT_STATUS_MASK( OBJECT_STATUS_NO_ATTACK ) );
		self->clearWeaponSetFlag( WEAPONSET_RIDER8 );
	}
}

void SandboxSiegeAIUpdate::toggle()
{
	if( getObject()->isEffectivelyDead() )
		return;
	switch( m_state )
	{
		case SIEGE_TRAVEL:
		case SIEGE_UNDEPLOYING:	setSiegeState( SIEGE_DEPLOYING ); break;
		case SIEGE_DEPLOYED:
		case SIEGE_DEPLOYING:		setSiegeState( SIEGE_UNDEPLOYING ); break;
	}
}

//-------------------------------------------------------------------------------------------------
void SandboxSiegeAIUpdate::enable( Bool enable )
{
	if( enable != isOverchargeActive() )
		toggle();
}

//-------------------------------------------------------------------------------------------------
UpdateSleepTime SandboxSiegeAIUpdate::update()
{
	UnsignedInt now = TheGameLogic->getFrame();
	Bool isTryingToMove = isWaitingForPath() || getPath();

	switch( m_state )
	{
		case SIEGE_TRAVEL:
			break;
		case SIEGE_DEPLOYING:
			if( isTryingToMove )
				aiIdle( CMD_FROM_AI );	// no driving while deploying / in siege
			getStateMachine()->setTemporaryState( AI_BUSY, UPDATE_SLEEP_NONE );
			setLocomotorGoalNone();
			if( now >= m_doneFrame )
				setSiegeState( SIEGE_DEPLOYED );
			else
				showAnimFrame();
			break;
		case SIEGE_DEPLOYED:
			if( isTryingToMove )
				aiIdle( CMD_FROM_AI );
			setLocomotorGoalNone();
			showAnimFrame();
			break;
		case SIEGE_UNDEPLOYING:
			// keep any new move order and carry it out once packed up
			getStateMachine()->setTemporaryState( AI_BUSY, UPDATE_SLEEP_NONE );
			setLocomotorGoalNone();
			if( now >= m_doneFrame )
				setSiegeState( SIEGE_TRAVEL );
			else
				showAnimFrame();
			break;
	}

	AIUpdateInterface::update();
	return UPDATE_SLEEP_NONE;
}

// ------------------------------------------------------------------------------------------------
void SandboxSiegeAIUpdate::crc( Xfer *xfer )
{
	AIUpdateInterface::crc(xfer);
}

// ------------------------------------------------------------------------------------------------
void SandboxSiegeAIUpdate::xfer( Xfer *xfer )
{
	XferVersion currentVersion = 1;
	XferVersion version = currentVersion;
	xfer->xferVersion( &version, currentVersion );
	AIUpdateInterface::xfer(xfer);
	xfer->xferUser( &m_state, sizeof(m_state) );
	xfer->xferUnsignedInt( &m_startFrame );
	xfer->xferUnsignedInt( &m_doneFrame );
}

// ------------------------------------------------------------------------------------------------
void SandboxSiegeAIUpdate::loadPostProcess()
{
	AIUpdateInterface::loadPostProcess();
}
