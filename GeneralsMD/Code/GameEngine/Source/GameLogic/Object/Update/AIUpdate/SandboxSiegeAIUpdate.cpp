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
#include "GameLogic/AIStateMachine.h"
#include "GameLogic/PartitionManager.h"
#include "GameLogic/Weapon.h"

Bool GX_IsBlind( ObjectID id );		// AIUpdate.cpp (r022)
Bool GX_TakeCancel( ObjectID id );

//-------------------------------------------------------------------------------------------------
SandboxSiegeAIUpdate::SandboxSiegeAIUpdate( Thing *thing, const ModuleData* moduleData ) : AIUpdateInterface( thing, moduleData )
{
	m_state = SIEGE_TRAVEL;
	m_startFrame = 0;
	m_doneFrame = 0;
	m_noAutoUntil = 0;	// SandboxRTS autosiege
	m_keepOrder = FALSE;
	m_hasResume = FALSE;
	m_resumePending = FALSE;
	m_resume.zero();
	m_clearSince = 0;
}

// r022: closest living enemy the tank can see
Object *SandboxSiegeAIUpdate::gxScanEnemy()
{
	Object *me = getObject();
	if( me == nullptr || ThePartitionManager == nullptr || me->isEffectivelyDead() )
		return nullptr;
	PartitionFilterRelationship rel( me, PartitionFilterRelationship::ALLOW_ENEMIES );
	PartitionFilterAlive alive;
	PartitionFilterSameMapStatus sameMap( me );
	PartitionFilter *filters[] = { &rel, &alive, &sameMap, nullptr };
	return ThePartitionManager->getClosestObject( me, me->getVisionRange(), FROM_BOUNDINGSPHERE_2D, filters );
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
void GX_SetSieged( ObjectID id, Bool on );
void SandboxSiegeAIUpdate::setSiegeState( SandboxSiegeState s )
{
	Object *self = getObject();
	if( self ) GX_SetSieged( self->getID(), s != SIEGE_TRAVEL );
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
			if( !m_keepOrder )
				aiIdle( CMD_FROM_AI );	// drop the current target (SandboxRTS autosiege: keep a move order)
			m_keepOrder = FALSE;
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
			setSiegeState( SIEGE_DEPLOYING );
			break;
		case SIEGE_DEPLOYING:
			m_noAutoUntil = TheGameLogic->getFrame() + LOGICFRAMES_PER_SECOND * 6;
			setSiegeState( SIEGE_UNDEPLOYING );
			break;
		case SIEGE_DEPLOYED:
			m_noAutoUntil = TheGameLogic->getFrame() + LOGICFRAMES_PER_SECOND * 6;
			setSiegeState( SIEGE_UNDEPLOYING );
			break;
		case SIEGE_UNDEPLOYING:
			setSiegeState( SIEGE_DEPLOYING );
			break;
	}
}

void SandboxSiegeAIUpdate::enable( Bool enable )
{
	if( enable )
	{
		if( m_state == SIEGE_TRAVEL || m_state == SIEGE_UNDEPLOYING )
			setSiegeState( SIEGE_DEPLOYING );
	}
	else
	{
		if( m_state == SIEGE_DEPLOYED || m_state == SIEGE_DEPLOYING )
		{
			m_noAutoUntil = TheGameLogic->getFrame() + LOGICFRAMES_PER_SECOND * 6;
			setSiegeState( SIEGE_UNDEPLOYING );
		}
	}
}

UpdateSleepTime SandboxSiegeAIUpdate::update()
{
	if( getObject()->isEffectivelyDead() )
		return UPDATE_SLEEP_FOREVER;

	UnsignedInt now = TheGameLogic->getFrame();
	Bool isTryingToMove = isWaitingForPath() || getPath();
	// SandboxRTS autosiege: a move order from the player (not an attack / chase)
	Bool playerMove = isTryingToMove && getLastCommandSource() == CMD_FROM_PLAYER && !isAttacking();
	// r022: an attack-move (double tap) does not stop the autosiege while driving
	const Bool attackMove = isTryingToMove && getCurrentStateID() == AI_ATTACK_MOVE_TO;
	const Bool travelOrder = playerMove && !attackMove;
	const ObjectID myId = getObject()->getID();
	const Bool blind = GX_IsBlind( myId );
	const Bool scanFrame = ((now + (UnsignedInt)myId) % 10) == 0;
	if( GX_TakeCancel( myId ) )
	{
		m_hasResume = FALSE;
		m_resumePending = FALSE;
	}

	switch( m_state )
	{
		case SIEGE_TRAVEL:
		{
			// enemy in sight -> stop and deploy by itself
			Object *victim = blind ? nullptr : getCurrentVictim();
			// r022: player tanks deploy as soon as they see an enemy too (not only AI ones)
			if( victim == nullptr && !blind && !travelOrder && now >= m_noAutoUntil && scanFrame )
				victim = gxScanEnemy();
			if( !travelOrder && now >= m_noAutoUntil && victim && !victim->isEffectivelyDead()
				&& !getObject()->isEffectivelyDead() )
			{
				m_hasResume = FALSE;
				m_resumePending = FALSE;
				if( attackMove )
				{
					const Coord3D *g = getGoalPosition();
					if( g ) { m_resume = *g; m_hasResume = TRUE; }
				}
				m_clearSince = now;
				setSiegeState( SIEGE_DEPLOYING );
			}
			break;
		}
		case SIEGE_DEPLOYING:
			if( playerMove )
			{
				m_keepOrder = TRUE;	// right click while unfolding: fold back and go
				setSiegeState( SIEGE_UNDEPLOYING );
				break;
			}
			if( isTryingToMove )
				aiIdle( CMD_FROM_AI );	// no driving while deploying
			getStateMachine()->setTemporaryState( AI_BUSY, UPDATE_SLEEP_NONE );
			setLocomotorGoalNone();
			if( now >= m_doneFrame )
				setSiegeState( SIEGE_DEPLOYED );
			else
				showAnimFrame();
			break;
		case SIEGE_DEPLOYED:
			if( playerMove )
			{
				m_keepOrder = TRUE;	// right click on the ground: pack up, then drive there
				setSiegeState( SIEGE_UNDEPLOYING );
				break;
			}
			if( isTryingToMove )
				aiIdle( CMD_FROM_AI );	// AI chase while in siege: ignore
			setLocomotorGoalNone();
			showAnimFrame();
			// r022: deployed out of an attack-move -> enemy gone for 2 s: pack up and go on
			if( m_hasResume )
			{
				if( !blind && getCurrentVictim() != nullptr )
					m_clearSince = now;
				else if( !blind && scanFrame && gxScanEnemy() != nullptr )
					m_clearSince = now;
				if( now > m_clearSince + LOGICFRAMES_PER_SECOND * 2 )
				{
					m_resumePending = TRUE;
					m_hasResume = FALSE;
					setSiegeState( SIEGE_UNDEPLOYING );
				}
			}
			break;
		case SIEGE_UNDEPLOYING:
			// keep any new move order and carry it out once packed up
			getStateMachine()->setTemporaryState( AI_BUSY, UPDATE_SLEEP_NONE );
			setLocomotorGoalNone();
			if( now >= m_doneFrame )
			{
				setSiegeState( SIEGE_TRAVEL );
				if( m_resumePending )
				{
					m_resumePending = FALSE;
					aiAttackMoveToPosition( &m_resume, NO_MAX_SHOTS_LIMIT, CMD_FROM_AI );
				}
			}
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
