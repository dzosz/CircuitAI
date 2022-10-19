/*
 * CaptureAction.cpp
 *
 *  Created on: Oct 31, 2021
 *      Author: rlcevg
 */

#include "unit/action/CaptureAction.h"
#include "unit/CircuitUnit.h"
#include "map/ThreatMap.h"
#include "module/EconomyManager.h"
#include "terrain/TerrainManager.h"
#include "CircuitAI.h"
#include "util/Utils.h"

#include "spring/SpringCallback.h"

#include "Unit.h"

namespace circuit {

CCaptureAction::CCaptureAction(CCircuitUnit* owner, float range)
		: IUnitAction(owner, Type::CAPTURE)
		, range(range)
		, updCount(0)
{
}

CCaptureAction::~CCaptureAction()
{
}

void CCaptureAction::Update(CCircuitAI* circuit)
{
	if (updCount++ % 2 != 0) {
		return;
	}
	isBlocking = false;
	CCircuitUnit* unit = static_cast<CCircuitUnit*>(ownerList);

//	if (CCircuitUnit::ETaskState::EXECUTE == unit->GetTaskState()) {
//		StateWait();
//		return;
//	}

	COOAICallback* clb = circuit->GetCallback();
	CThreatMap* threatMap = circuit->GetThreatMap();
	threatMap->SetThreatType(unit);
	const float power = threatMap->GetUnitPower(unit) * 0.5f;
	const AIFloat3& pos = unit->GetPos(circuit->GetLastFrame());
	const float metalIncome = circuit->GetEconomyManager()->GetAvgMetalIncome() * 0.5f;
	const float energyIncome = circuit->GetEconomyManager()->GetAvgEnergyIncome() * 0.5f;

	const std::vector<Unit*>& neutrals = clb->GetNeutralUnitsIn(pos, range);
	if (!neutrals.empty()) {
		float minSqDist = SQUARE(range);
		Unit* neutral = nullptr;
		for (Unit* n : neutrals) {
			// NOTE: check allyTeam as BAR creates units as neutral (nano-frame spam fix)
			if (n->GetAllyTeam() == circuit->GetAllyTeamId()) {
				continue;
			}
			CCircuitDef::Id defId = clb->Unit_GetDefId(n->GetUnitId());
			if (defId != -1) {
				CCircuitDef* ndef = circuit->GetCircuitDef(defId);
				if (ndef->IsIgnore() || !ndef->IsCapturable()
					|| (ndef->GetUpkeepM() > metalIncome) || (ndef->GetUpkeepE() > energyIncome))
				{
					continue;
				}
				CWeaponDef* wdef = ndef->GetWeaponDef();
				if ((wdef != nullptr) && ((wdef->GetCostM() > metalIncome) || (wdef->GetCostE() > energyIncome))) {
					continue;
				}
			}
			AIFloat3 npos = n->GetPos();
			CTerrainManager::CorrectPosition(npos);
			if (threatMap->GetThreatAt(npos) > power) {
				continue;
			}
			float sqDist = npos.SqDistance2D(pos);
			if (minSqDist > sqDist) {
				minSqDist = sqDist;
				neutral = n;
			}
		}
		if (neutral != nullptr) {
			TRY_UNIT(circuit, unit,
				unit->GetUnit()->Capture(neutral);
			)
			isBlocking = true;
		}
		utils::free(neutrals);
		if (neutral != nullptr) {
			return;
		}
	}

	const std::vector<CCircuitUnit::Id>& enemies = clb->GetEnemyUnitIdsIn(pos, range);
	if (enemies.empty()) {
		return;
	}

	const float maxSpeed = unit->GetCircuitDef()->GetSpeed() * 0.5f;
	float minSqDist = SQUARE(range);
	CEnemyInfo* enemy = nullptr;
	for (ICoreUnit::Id eId : enemies) {
		CEnemyInfo* e = circuit->GetEnemyInfo(eId);
		if ((e == nullptr) || (threatMap->GetThreatAt(e->GetPos()) > power)) {
			continue;
		}
		CCircuitDef* edef = e->GetCircuitDef();
		if ((edef != nullptr)
			&& (edef->IsIgnore() || !edef->IsCapturable() || edef->GetSpeed() > maxSpeed))
		{
			continue;
		}
		float sqDist = e->GetPos().SqDistance2D(pos);
		if (minSqDist > sqDist) {
			minSqDist = sqDist;
			enemy = e;
		}
	}
	if (enemy != nullptr) {
		TRY_UNIT(circuit, unit,
			unit->GetUnit()->Capture(enemy->GetUnit());
		)
		isBlocking = true;
	}
}

} // namespace circuit
