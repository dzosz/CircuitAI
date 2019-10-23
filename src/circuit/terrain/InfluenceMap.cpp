/*
 * InfluenceMap.cpp
 *
 *  Created on: Oct 20, 2019
 *      Author: rlcevg
 */

#include "terrain/InfluenceMap.h"
#include "terrain/TerrainManager.h"
#include "terrain/ThreatMap.h"
#include "module/EconomyManager.h"
#include "unit/EnemyUnit.h"
#include "CircuitAI.h"
#include "util/utils.h"
// FIXME: DEBUG
#include "terrain/PathFinder.h"
// FIXME: DEBUG

#include "OOAICallback.h"
#include "Cheats.h"
#include "Feature.h"
#include "FeatureDef.h"

namespace circuit {

using namespace springai;

CInfluenceMap::CInfluenceMap(CCircuitAI* circuit)
		: circuit(circuit)
		, vulnMax(0.f)
{
	squareSize = circuit->GetTerrainManager()->GetConvertStoP() * 4;
	width = circuit->GetTerrainManager()->GetSectorXSize() / 4;
	height = circuit->GetTerrainManager()->GetSectorZSize() / 4;
	mapSize = width * height;

	enemyInfl.resize(mapSize, INFL_BASE);
	allyInfl.resize(mapSize, INFL_BASE);
	influence.resize(mapSize, INFL_BASE);
	tension.resize(mapSize, INFL_BASE);
	vulnerability.resize(mapSize, INFL_BASE);
	featureInfl.resize(mapSize, INFL_BASE);
}

CInfluenceMap::~CInfluenceMap()
{
#ifdef DEBUG_VIS
	for (const std::pair<Uint32, float*>& win : sdlWindows) {
		circuit->GetDebugDrawer()->DelSDLWindow(win.first);
		delete[] win.second;
	}
#endif
}

void CInfluenceMap::Update()
{
	Clear();
	const CCircuitAI::EnemyUnits& hostileUnits = circuit->GetThreatMap()->GetHostileUnits();
	for (auto& kv : hostileUnits) {
		CEnemyUnit* e = kv.second;
		AddUnit(e);
	}
	const CAllyTeam::Units& units = circuit->GetFriendlyUnits();
	for (auto& kv : units) {
		CAllyUnit* u = kv.second;
		if (u->GetCircuitDef()->IsAttacker()) {
			AddUnit(u);
		}
	}
	for (size_t i = 0; i < influence.size(); ++i) {
		influence[i] = allyInfl[i] - enemyInfl[i];
	}
	for (size_t i = 0; i < tension.size(); ++i) {
		tension[i] = allyInfl[i] + enemyInfl[i];
	}
	vulnMax = 0.f;
	for (size_t i = 0; i < vulnerability.size(); ++i) {
		vulnerability[i] = tension[i] - abs(influence[i]);
		if (vulnMax < vulnerability[i]) {
			vulnMax = vulnerability[i];
		}
	}
	Cheats* cheats = circuit->GetCallback()->GetCheats();
	cheats->SetEnabled(true);
	auto features = std::move(circuit->GetCallback()->GetFeatures());
	for (Feature* f : features) {
		if (f == nullptr) {
			continue;
		}
		AddFeature(f);
		delete f;
	}
	cheats->SetEnabled(false);
	delete cheats;

#ifdef DEBUG_VIS
	UpdateVis();
#endif
}

float CInfluenceMap::GetEnemyInflAt(const AIFloat3& position) const
{
	int x, z;
	PosToXZ(position, x, z);
	return enemyInfl[z * width + x] - INFL_BASE;
}

float CInfluenceMap::GetInfluenceAt(const AIFloat3& position) const
{
	int x, z;
	PosToXZ(position, x, z);
	return influence[z * width + x] - INFL_BASE;
}

void CInfluenceMap::Clear()
{
	std::fill(enemyInfl.begin(), enemyInfl.end(), INFL_BASE);
	std::fill(allyInfl.begin(), allyInfl.end(), INFL_BASE);
	std::fill(influence.begin(), influence.end(), INFL_BASE);
	std::fill(tension.begin(), tension.end(), INFL_BASE);
	std::fill(vulnerability.begin(), vulnerability.end(), INFL_BASE);
	std::fill(featureInfl.begin(), featureInfl.end(), INFL_BASE);
}

void CInfluenceMap::AddUnit(CAllyUnit* u)
{
	int posx, posz;
	PosToXZ(u->GetPos(circuit->GetLastFrame()), posx, posz);

	const float val = u->GetCircuitDef()->GetPower();
	// FIXME: GetInfluenceRange: for statics it's just range; mobile should account for speed
	const int range = u->GetCircuitDef()->IsMobile()
			? u->GetCircuitDef()->GetThreatRange(CCircuitDef::ThreatType::LAND) / 2
			: u->GetCircuitDef()->GetThreatRange(CCircuitDef::ThreatType::LAND) / 4;
	const int rangeSq = SQUARE(range);

	const int beginX = std::max(int(posx - range + 1),       0);
	const int endX   = std::min(int(posx + range    ),  width);
	const int beginZ = std::max(int(posz - range + 1),       0);
	const int endZ   = std::min(int(posz + range    ), height);

	for (int x = beginX; x < endX; ++x) {
		const int dxSq = SQUARE(posx - x);
		for (int z = beginZ; z < endZ; ++z) {
			const int dzSq = SQUARE(posz - z);
			const int lenSq = dxSq + dzSq;
			if (lenSq > rangeSq) {
				continue;
			}

			const int index = z * width + x;
			const float infl = val * (1.0f - 1.0f * sqrtf(lenSq) / range);
			allyInfl[index] += infl;
		}
	}
}

void CInfluenceMap::AddUnit(CEnemyUnit* e)
{
	int posx, posz;
	PosToXZ(e->GetPos(), posx, posz);

	const float val = e->GetThreat();
	// FIXME: GetInfluenceRange: for statics it's just range; mobile should account for speed
	const int range = (e->GetCircuitDef() == nullptr)
			? e->GetRange(CCircuitDef::ThreatType::LAND)
			: e->GetCircuitDef()->IsMobile()
					? e->GetRange(CCircuitDef::ThreatType::LAND) / 2
					: e->GetRange(CCircuitDef::ThreatType::LAND) / 4;
	const int rangeSq = SQUARE(range);

	const int beginX = std::max(int(posx - range + 1),       0);
	const int endX   = std::min(int(posx + range    ),  width);
	const int beginZ = std::max(int(posz - range + 1),       0);
	const int endZ   = std::min(int(posz + range    ), height);

	for (int x = beginX; x < endX; ++x) {
		const int dxSq = SQUARE(posx - x);
		for (int z = beginZ; z < endZ; ++z) {
			const int dzSq = SQUARE(posz - z);
			const int lenSq = dxSq + dzSq;
			if (lenSq > rangeSq) {
				continue;
			}

			const int index = z * width + x;
			const float infl = val * (1.0f - 1.0f * sqrtf(lenSq) / range);
			enemyInfl[index] += infl;
		}
	}
}

void CInfluenceMap::AddFeature(Feature* f)
{
	int posx, posz;
	PosToXZ(f->GetPosition(), posx, posz);

	FeatureDef* featDef = f->GetDef();
	if (!featDef->IsReclaimable()) {
		delete featDef;
		return;
	}
	const float val = featDef->GetContainedResource(circuit->GetEconomyManager()->GetMetalRes()) * f->GetReclaimLeft();
	delete featDef;
	const int range = 2;
	const int rangeSq = SQUARE(range);

	const int beginX = std::max(int(posx - range + 1),       0);
	const int endX   = std::min(int(posx + range    ),  width);
	const int beginZ = std::max(int(posz - range + 1),       0);
	const int endZ   = std::min(int(posz + range    ), height);

	for (int x = beginX; x < endX; ++x) {
		const int dxSq = SQUARE(posx - x);
		for (int z = beginZ; z < endZ; ++z) {
			const int dzSq = SQUARE(posz - z);
			const int lenSq = dxSq + dzSq;
			if (lenSq > rangeSq) {
				continue;
			}

			const int index = z * width + x;
			const float infl = val * (1.0f - 1.0f * sqrtf(lenSq) / range);
			featureInfl[index] += infl;
		}
	}
}

inline void CInfluenceMap::PosToXZ(const AIFloat3& pos, int& x, int& z) const
{
	x = (int)pos.x / squareSize;
	z = (int)pos.z / squareSize;
}

#ifdef DEBUG_VIS
#define ENEMY(x, i, v) {	\
	x[i * 3 + 0] = .95f * v;  /*R*/	\
	x[i * 3 + 1] = .40f * v;  /*G*/	\
	x[i * 3 + 2] = .10f * v;  /*B*/	\
}
#define ALLY(x, i, v) {	\
	x[i * 3 + 0] = .10f * v;  /*R*/	\
	x[i * 3 + 1] = .40f * v;  /*G*/	\
	x[i * 3 + 2] = .95f * v;  /*B*/	\
}

void CInfluenceMap::UpdateVis()
{
	if (sdlWindows.empty()) {
		return;
	}

	Uint32 sdlWindowId;
	float* dbgMap;
	std::tie(sdlWindowId, dbgMap) = sdlWindows[0];
	for (unsigned i = 0; i < enemyInfl.size(); ++i) {
		dbgMap[i] = std::min<float>((enemyInfl[i] - INFL_BASE) / 200.0f, 1.0f);
	}
	circuit->GetDebugDrawer()->DrawMap(sdlWindowId, dbgMap, {255, 50, 10, 0});

	std::tie(sdlWindowId, dbgMap) = sdlWindows[1];
	for (unsigned i = 0; i < allyInfl.size(); ++i) {
		dbgMap[i] = std::min<float>((allyInfl[i] - INFL_BASE) / 200.0f, 1.0f);
	}
	circuit->GetDebugDrawer()->DrawMap(sdlWindowId, dbgMap, {10, 50, 255, 0});

	std::tie(sdlWindowId, dbgMap) = sdlWindows[2];
	for (unsigned i = 0; i < influence.size(); ++i) {
		float value = utils::clamp(influence[i] / 200.0f, -1.f, 1.f);
		if (value < 0) ENEMY(dbgMap, i, -value)
		else ALLY(dbgMap, i, value)
	}
	circuit->GetDebugDrawer()->DrawTex(sdlWindowId, dbgMap);

	std::tie(sdlWindowId, dbgMap) = sdlWindows[3];
	for (unsigned i = 0; i < tension.size(); ++i) {
		dbgMap[i] = std::min<float>(tension[i] / 200.0f, 1.0f);
	}
	circuit->GetDebugDrawer()->DrawMap(sdlWindowId, dbgMap, {255, 50, 10, 0});

	std::tie(sdlWindowId, dbgMap) = sdlWindows[4];
	for (unsigned i = 0; i < vulnerability.size(); ++i) {
		float value = utils::clamp((vulnerability[i] - vulnMax / 2) / (vulnMax / 2), -1.f, 1.f);
		if (value < 0) ALLY(dbgMap, i, -value)
		else ENEMY(dbgMap, i, value)
	}
	circuit->GetDebugDrawer()->DrawTex(sdlWindowId, dbgMap);

	std::tie(sdlWindowId, dbgMap) = sdlWindows[5];
	for (unsigned i = 0; i < featureInfl.size(); ++i) {
		dbgMap[i] = std::min<float>((featureInfl[i] - INFL_BASE) / 500.f, 1.0f);
	}
	circuit->GetDebugDrawer()->DrawMap(sdlWindowId, dbgMap, {10, 50, 255, 0});

	std::tie(sdlWindowId, dbgMap) = sdlWindows[6];
	for (unsigned i = 0; i < circuit->GetPathfinder()->costs.size(); ++i) {
		float value = circuit->GetPathfinder()->costs[i];
		dbgMap[i] = (value < 0) ? 0 : std::min<float>(value / 200.0f, 1.0f);
	}
	circuit->GetDebugDrawer()->DrawMap(sdlWindowId, dbgMap);
}

void CInfluenceMap::ToggleVis()
{
	if (sdlWindows.empty()) {
		// ~infl
		std::pair<Uint32, float*> win;
		std::string label;

		win.second = new float [enemyInfl.size()];
		label = utils::int_to_string(circuit->GetSkirmishAIId(), "Circuit AI [%i] :: Enemy Influence Map");
		win.first = circuit->GetDebugDrawer()->AddSDLWindow(width, height, label.c_str());
		sdlWindows.push_back(win);

		win.second = new float [allyInfl.size()];
		label = utils::int_to_string(circuit->GetSkirmishAIId(), "Circuit AI [%i] :: Ally Influence Map");
		win.first = circuit->GetDebugDrawer()->AddSDLWindow(width, height, label.c_str());
		sdlWindows.push_back(win);

		win.second = new float [influence.size() * 3];
		label = utils::int_to_string(circuit->GetSkirmishAIId(), "Circuit AI [%i] :: Influence Map");
		win.first = circuit->GetDebugDrawer()->AddSDLWindow(width, height, label.c_str());
		sdlWindows.push_back(win);

		win.second = new float [tension.size() * 3];
		label = utils::int_to_string(circuit->GetSkirmishAIId(), "Circuit AI [%i] :: Tension Map");
		win.first = circuit->GetDebugDrawer()->AddSDLWindow(width, height, label.c_str());
		sdlWindows.push_back(win);

		win.second = new float [vulnerability.size() * 3];
		label = utils::int_to_string(circuit->GetSkirmishAIId(), "Circuit AI [%i] :: Vulnerability Map");
		win.first = circuit->GetDebugDrawer()->AddSDLWindow(width, height, label.c_str());
		sdlWindows.push_back(win);

		win.second = new float [featureInfl.size()];
		label = utils::int_to_string(circuit->GetSkirmishAIId(), "Circuit AI [%i] :: Feature Influence Map");
		win.first = circuit->GetDebugDrawer()->AddSDLWindow(width, height, label.c_str());
		sdlWindows.push_back(win);

		win.second = new float [circuit->GetPathfinder()->costs.size()];
		label = utils::int_to_string(circuit->GetSkirmishAIId(), "Circuit AI [%i] :: PathFinder");
		win.first = circuit->GetDebugDrawer()->AddSDLWindow(circuit->GetPathfinder()->pathMapXSize, circuit->GetPathfinder()->pathMapYSize, label.c_str());
		sdlWindows.push_back(win);

		UpdateVis();
	} else {
		for (const std::pair<Uint32, float*>& win : sdlWindows) {
			circuit->GetDebugDrawer()->DelSDLWindow(win.first);
			delete[] win.second;
		}
		sdlWindows.clear();
	}
}
#endif

} // namespace circuit
