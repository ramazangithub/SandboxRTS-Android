/*
**	SandboxRTS: manual siege mode for tanks (GPL v3, part of the SandboxRTS engine patch).
**
**	Travel: drives normally, the turret aims and fires on the move.
**	Toggle (key D -> MSG_META_DEPLOY -> MSG_TOGGLE_OVERCHARGE): the tank stops and deploys
**	(model condition UNPACKING, manual animation), then sits in SIEGE: cannot move, weapon set
**	flag PLAYER_UPGRADE selects the siege weapons (longer range / more damage).
**	Toggle again: PACKING (animation in reverse) and back to travel.
*/
#pragma once
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/OverchargeBehavior.h"

enum SandboxSiegeState CPP_11(: Int)
{
	SIEGE_TRAVEL = 0,
	SIEGE_DEPLOYING,
	SIEGE_DEPLOYED,
	SIEGE_UNDEPLOYING,
};

class SandboxSiegeAIUpdateModuleData : public AIUpdateModuleData
{
public:
	UnsignedInt m_deployTime;			///< logic frames to deploy (and to pack up)
	Int m_animFrames;							///< number of frames of the deploy animation (manual animation)
	SandboxSiegeAIUpdateModuleData() : m_deployTime(75), m_animFrames(0) {}
	static void buildFieldParse(MultiIniFieldParse& p)
	{
		AIUpdateModuleData::buildFieldParse(p);
		static const FieldParse dataFieldParse[] =
		{
			{ "DeployTime",					INI::parseDurationUnsignedInt,	nullptr, offsetof( SandboxSiegeAIUpdateModuleData, m_deployTime ) },
			{ "DeployAnimFrames",		INI::parseInt,									nullptr, offsetof( SandboxSiegeAIUpdateModuleData, m_animFrames ) },
			{ nullptr, nullptr, nullptr, 0 }
		};
		p.add(dataFieldParse);
	}
};

class SandboxSiegeAIUpdate : public AIUpdateInterface, public OverchargeBehaviorInterface
{
	MEMORY_POOL_GLUE_WITH_USERLOOKUP_CREATE( SandboxSiegeAIUpdate, "SandboxSiegeAIUpdate" )
	MAKE_STANDARD_MODULE_MACRO_WITH_MODULE_DATA( SandboxSiegeAIUpdate, SandboxSiegeAIUpdateModuleData )
public:
	SandboxSiegeAIUpdate( Thing *thing, const ModuleData* moduleData );
	// virtual destructor prototype provided by memory pool declaration

	virtual UpdateSleepTime update() override;

	// toggled through the existing "toggle overcharge" group command
	virtual OverchargeBehaviorInterface* getOverchargeBehaviorInterface() override { return this; }
	virtual void toggle() override;
	virtual void enable( Bool enable ) override;
	virtual Bool isOverchargeActive() override { return m_state == SIEGE_DEPLOYED || m_state == SIEGE_DEPLOYING; }

	SandboxSiegeState getSiegeState() const { return m_state; }

protected:
	void setSiegeState( SandboxSiegeState s );
	void playUnitSound( const char *name );
	void showAnimFrame();
	void holdFire( Bool hold );
	Object *gxScanEnemy();					///< r022: closest living enemy inside vision range

	SandboxSiegeState	m_state;
	UnsignedInt				m_startFrame;	///< frame the current transition started
	UnsignedInt				m_doneFrame;	///< frame the current transition ends
	UnsignedInt				m_noAutoUntil;	///< SandboxRTS autosiege: no auto deploy before this frame (after manual Q pack-up)
	Bool						m_keepOrder;		///< SandboxRTS autosiege: pack up without dropping the move order
	Bool						m_hasResume;		///< r022: deployed out of an attack-move, continue it when clear
	Bool						m_resumePending;	///< r022: packing up to continue the attack-move
	Coord3D					m_resume;			///< r022: attack-move destination
	UnsignedInt				m_clearSince;		///< r022: last frame an enemy was seen while deployed
};
