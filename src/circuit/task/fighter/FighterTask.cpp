/*
 * FighterTask.cpp
 *
 *  Created on: Aug 31, 2015
 *      Author: rlcevg
 */

#include "task/fighter/FighterTask.h"
#include "task/TaskManager.h"
#include "task/RetreatTask.h"
#include "terrain/ThreatMap.h"
#include "unit/action/DGunAction.h"
#include "unit/EnemyUnit.h"
#include "CircuitAI.h"

//#include "Weapon.h"

namespace circuit {

using namespace springai;

IFighterTask::IFighterTask(ITaskManager* mgr, FightType type)
		: IUnitTask(mgr, Priority::NORMAL, Type::FIGHTER)
		, fightType(type)
		, position(-RgtVector)
		, attackPower(.0f)
		, target(nullptr)
{
}

IFighterTask::~IFighterTask()
{
}

void IFighterTask::AssignTo(CCircuitUnit* unit)
{
	IUnitTask::AssignTo(unit);

	attackPower += unit->GetCircuitDef()->GetPower();

	CCircuitDef* cdef = unit->GetCircuitDef();
	if (cdef->GetDGunMount() != nullptr) {
		CDGunAction* act = new CDGunAction(unit, cdef->GetDGunRange() * 0.9f);
		unit->PushBack(act);
	}
}

void IFighterTask::RemoveAssignee(CCircuitUnit* unit)
{
	IUnitTask::RemoveAssignee(unit);

	attackPower -= unit->GetCircuitDef()->GetPower();
}

void IFighterTask::Update()
{
	CCircuitAI* circuit = manager->GetCircuit();
	for (CCircuitUnit* unit : units) {
		unit->Update(circuit);
	}
}

void IFighterTask::OnUnitIdle(CCircuitUnit* unit)
{
	// TODO: Wait for others if goal reached? Or we stuck far away?

	if (unit->IsRetreat()) {
		manager->AssignTask(unit, manager->GetRetreatTask());
		return;
	}
}

void IFighterTask::OnUnitDamaged(CCircuitUnit* unit, CEnemyUnit* attacker)
{
	Unit* u = unit->GetUnit();
	const float healthPerc = u->GetHealth() / u->GetMaxHealth();
	// FIXME: Wait until 101.0 engine
//	if (unit->GetShield() != nullptr) {
//		if ((healthPerc > 0.6f) && (unit->GetShield()->GetShieldPower() > unit->GetCircuitDef()->GetMaxShield() * 0.1f)) {
//			return;
//		}
//	} else
	if (healthPerc > unit->GetCircuitDef()->GetRetreat()) {
		return;
	} else if (healthPerc < 0.2f) {  // stuck units workaround: they don't shoot and don't see distant threat
		manager->AssignTask(unit, manager->GetRetreatTask());
		return;
	}

	CCircuitAI* circuit = manager->GetCircuit();
	CThreatMap* threatMap = circuit->GetThreatMap();
	const float range = unit->GetCircuitDef()->GetMaxRange();
	if ((target == nullptr) || !target->IsInLOS()) {
		manager->AssignTask(unit, manager->GetRetreatTask());
		return;
	}
	const AIFloat3& pos = unit->GetPos(circuit->GetLastFrame());
	if ((target->GetPos().SqDistance2D(pos) > range * range) ||	(threatMap->GetThreatAt(unit, pos) > threatMap->GetUnitThreat(unit))) {
		manager->AssignTask(unit, manager->GetRetreatTask());
		return;
	}
	unit->SetRetreat(true);
}

void IFighterTask::OnUnitDestroyed(CCircuitUnit* unit, CEnemyUnit* attacker)
{
	RemoveAssignee(unit);
}

} // namespace circuit
