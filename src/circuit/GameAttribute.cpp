/*
 * GameAttribute.cpp
 *
 *  Created on: Aug 12, 2014
 *      Author: rlcevg
 */

#include "GameAttribute.h"
#include "SetupManager.h"
#include "MetalManager.h"
#include "Scheduler.h"
#include "utils.h"
#include "json/json.h"

#include "GameRulesParam.h"
#include "Game.h"
#include "Map.h"
#include "UnitDef.h"
#include "Pathing.h"

#include <map>
#include <regex>

namespace circuit {

using namespace springai;

CGameAttribute::CGameAttribute() :
		setupManager(nullptr),
		metalManager(nullptr)
{
	srand(time(nullptr));
}

CGameAttribute::~CGameAttribute()
{
	for (auto& kv : definitions) {
		delete kv.second;
	}
}

void CGameAttribute::ParseSetupScript(const char* setupScript, int width, int height)
{
	std::map<int, Box> boxesMap;
	std::regex patternAlly("\\[allyteam(\\d+)\\]\\s*\\{([^\\}]*)\\}");
	std::regex patternRect("startrect\\w+=(\\d+(\\.\\d+)?);");
	std::string script(setupScript);

	std::smatch allyteam;
	std::string::const_iterator start = script.begin();
	std::string::const_iterator end = script.end();
	while (std::regex_search(start, end, allyteam, patternAlly)) {
		int allyTeamId = utils::string_to_int(allyteam[1]);

		std::string teamDefBody = allyteam[2];
		std::sregex_token_iterator iter(teamDefBody.begin(), teamDefBody.end(), patternRect, 1);
		std::sregex_token_iterator end;
		Box startbox;
		for (int i = 0; iter != end && i < 4; ++iter, i++) {
			startbox.edge[i] = utils::string_to_float(*iter);
		}

		float mapWidth = SQUARE_SIZE * width;
		float mapHeight = SQUARE_SIZE * height;
		startbox.bottom *= mapHeight;
		startbox.left   *= mapWidth;
		startbox.right  *= mapWidth;
		startbox.top    *= mapHeight;
		boxesMap[allyTeamId] = startbox;

		start = allyteam[0].second;
	}

	CGameSetup::StartPosType startPosType;
	std::cmatch matchPosType;
	std::regex patternPosType("startpostype=(\\d+)");
	if (std::regex_search(setupScript, matchPosType, patternPosType)) {
		startPosType = static_cast<CGameSetup::StartPosType>(std::atoi(matchPosType[1].first));
	} else {
		startPosType = CGameSetup::StartPosType::StartPos_ChooseInGame;
	}

	std::vector<Box> startBoxes;
	// Remap start boxes
	// @see rts/Game/GameSetup.cpp CGameSetup::Init
//	for (const std::map<int, Box>::value_type& kv : boxesMap) {
//	for (const std::pair<const int, std::array<float, 4>>& kv : boxesMap) {
	for (const auto& kv : boxesMap) {
		startBoxes.push_back(kv.second);
	}

	setupManager = std::make_shared<CSetupManager>(startBoxes, startPosType);
}

bool CGameAttribute::HasStartBoxes(bool checkEmpty)
{
	if (checkEmpty) {
		return (setupManager != nullptr && !setupManager->IsEmpty());
	}
	return setupManager != nullptr;
}

bool CGameAttribute::CanChooseStartPos()
{
	return setupManager->CanChooseStartPos();
}

void CGameAttribute::PickStartPos(Game* game, Map* map, StartPosType type)
{
	float x, z;
	const Box& box = GetSetupManager()[game->GetMyAllyTeam()];

	auto random = [](const Box& box, float& x, float& z) {
		int min, max;
		min = box.left;
		max = box.right;
		x = min + (rand() % (int)(max - min + 1));
		min = box.top;
		max = box.bottom;
		z = min + (rand() % (int)(max - min + 1));
	};

	switch (type) {
		case StartPosType::METAL_SPOT: {
			// TODO: Optimize (with kd-tree?, convex hull?)
			std::vector<Metals>& clusters = metalManager->GetClusters();
			std::vector<Metals> inBoxClusters;
			for (auto& cluster : clusters) {
				for (auto& metal : cluster) {
					if (box.ContainsPoint(metal.position)) {
						inBoxClusters.push_back(cluster);
						break;
					}
				}
			}
			if (!inBoxClusters.empty()) {
				Metals& spots = inBoxClusters[rand() % inBoxClusters.size()];
				AIFloat3& pos = spots[rand() % spots.size()].position;
				x = pos.x;
				z = pos.z;
			} else {
				random(box, x, z);
			}
			break;
		}
		case StartPosType::MIDDLE: {
			x = (box.left + box.right) / 2;
			z = (box.top + box.bottom) / 2;
			break;
		}
		case StartPosType::RANDOM:
		default: {
			random(box, x, z);
			break;
		}
	}

	game->SendStartPosition(false, AIFloat3(x, map->GetElevationAt(x, z), z));
}

CSetupManager& CGameAttribute::GetSetupManager()
{
	return *setupManager;
}

void CGameAttribute::ParseMetalSpots(const char* metalJson)
{
	Json::Value root;
	Json::Reader json;

	if (!json.parse(metalJson, root, false)) {
		return;
	}

	std::vector<Metal> spots;
	for (const Json::Value& object : root) {
		Metal spot;
		spot.income = object["metal"].asFloat();
		spot.position = AIFloat3(object["x"].asFloat(),
								 object["y"].asFloat(),
								 object["z"].asFloat());
		spots.push_back(spot);
	}

	metalManager = std::make_shared<CMetalManager>(spots);
}

void CGameAttribute::ParseMetalSpots(const std::vector<GameRulesParam*>& gameParams)
{
	int mexCount = 0;
	for (auto param : gameParams) {
		if (strcmp(param->GetName(), "mex_count") == 0) {
			mexCount = param->GetValueFloat();
			break;
		}
	}

	if (mexCount <= 0) {
		return;
	}

	std::vector<Metal> spots(mexCount);
	int i = 0;
	for (auto param : gameParams) {
		const char* name = param->GetName();
		if (strncmp(name, "mex_", 4) == 0) {
			if (strncmp(name + 4, "x", 1) == 0) {
				int idx = std::atoi(name + 5);
				spots[idx - 1].position.x = param->GetValueFloat();
				i++;
			} else if (strncmp(name + 4, "y", 1) == 0) {
				int idx = std::atoi(name + 5);
				spots[idx - 1].position.y = param->GetValueFloat();
				i++;
			} else if (strncmp(name + 4, "z", 1) == 0) {
				int idx = std::atoi(name + 5);
				spots[idx - 1].position.z = param->GetValueFloat();
				i++;
			} else if (strncmp(name + 4, "metal", 5) == 0) {
				int idx = std::atoi(name + 9);
				spots[idx - 1].income = param->GetValueFloat();
				i++;
			}

			if (i >= mexCount * 4) {
				break;
			}
		}
	}

	metalManager = std::make_shared<CMetalManager>(spots);
}

bool CGameAttribute::HasMetalSpots(bool checkEmpty)
{
	if (checkEmpty) {
		return (metalManager != nullptr && !metalManager->IsEmpty());
	}
	return metalManager != nullptr;
}

bool CGameAttribute::HasMetalClusters()
{
	return metalManager->IsClusterizing() || !metalManager->GetClusters().empty();
}

void CGameAttribute::ClusterizeMetal(std::shared_ptr<CScheduler> scheduler, float maxDistance, int pathType, Pathing* pathing)
{
	metalManager->SetClusterizing(true);

	Metals& spots = metalManager->GetSpots();
	int nrows = spots.size();

	// TODO: 1) Save distmatrix, could be useful
	//       2) Break initialization into several index steps
	// Create distance matrix
	float** distmatrix = new float* [nrows];  // CMetalManager::Clusterize will delete distmatrix
	distmatrix[0] = nullptr;
	for (int i = 1; i < nrows; i++) {
		distmatrix[i] = new float [i];
		for (int j = 0; j < i; j++) {
			float lenStartEnd = pathing->GetApproximateLength(spots[i].position, spots[j].position, pathType, 0.0f);
			float lenEndStart = pathing->GetApproximateLength(spots[j].position, spots[i].position, pathType, 0.0f);
			distmatrix[i][j] = (lenStartEnd + lenEndStart) / 2.0f;
		}
	}

	scheduler->RunParallelTask(std::make_shared<CGameTask>(&CMetalManager::Clusterize, metalManager, maxDistance, distmatrix));
}

CMetalManager& CGameAttribute::GetMetalManager()
{
	return *metalManager;
}

void CGameAttribute::InitUnitDefs(std::vector<UnitDef*>& unitDefs)
{
	if (!definitions.empty()) {
		for (auto& kv : definitions) {
			delete kv.second;
		}
		definitions.clear();
	}
	for (auto def : unitDefs) {
		definitions[def->GetName()] = def;
	}
}

bool CGameAttribute::HasUnitDefs()
{
	return !definitions.empty();
}

UnitDef* CGameAttribute::GetUnitDefByName(const char* name)
{
	decltype(definitions)::iterator i = definitions.find(name);
	if (i != definitions.end()) {
		return i->second;
	}

	return nullptr;
}

} // namespace circuit
