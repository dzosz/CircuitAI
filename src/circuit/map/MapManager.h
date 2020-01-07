/*
 * MapManager.h
 *
 *  Created on: Dec 21, 2019
 *      Author: rlcevg
 */

#ifndef SRC_CIRCUIT_MAP_MAPMANAGER_H_
#define SRC_CIRCUIT_MAP_MAPMANAGER_H_

#include "unit/EnemyManager.h"
#include "unit/EnemyUnit.h"

namespace circuit {

class CCircuitAI;
class CThreatMap;
class CInfluenceMap;

class CMapManager {
public:
	CMapManager(CCircuitAI* circuit, float decloakRadius);
	virtual ~CMapManager();

	void SetAuthority(CCircuitAI* authority) { circuit = authority; }
	CCircuitAI* GetCircuit() const { return circuit; }

	CThreatMap* GetThreatMap() const { return threatMap; }
	CInfluenceMap* GetInflMap() const { return inflMap; }

	void EnqueueUpdate();

	const std::vector<SEnemyData>& GetHostileDatas() const { return hostileDatas; }
	const std::vector<SEnemyData>& GetPeaceDatas() const { return peaceDatas; }

	bool IsSuddenThreat(CEnemyUnit* enemy) const;

	bool EnemyEnterLOS(CEnemyUnit* enemy, CCircuitAI* ai);
	void EnemyLeaveLOS(CEnemyUnit* enemy, CCircuitAI* ai);
	void EnemyEnterRadar(CEnemyUnit* enemy, CCircuitAI* ai);
	void EnemyLeaveRadar(CEnemyUnit* enemy, CCircuitAI* ai);
	bool EnemyDestroyed(CEnemyUnit* enemy, CCircuitAI* ai);

private:
	bool IsInLOS(const springai::AIFloat3& pos) const;
//	bool IsInRadar(const springai::AIFloat3& pos) const;

	CCircuitAI* circuit;

	CThreatMap* threatMap;
	CInfluenceMap* inflMap;

	CEnemyManager::EnemyUnits hostileUnits;
	CEnemyManager::EnemyUnits peaceUnits;

	std::vector<SEnemyData> hostileDatas;
	std::vector<SEnemyData> peaceDatas;

//	IntVec radarMap;
	IntVec sonarMap;
	IntVec losMap;

	int radarWidth;
	int radarResConv;
	int losWidth;
	int losResConv;
};

} // namespace circuit

#endif // SRC_CIRCUIT_MAP_MAPMANAGER_H_