/*
 * SupportTask.cpp
 *
 *  Created on: Jul 3, 2016
 *      Author: rlcevg
 */

#include "task/fighter/SupportTask.h"
#include "task/fighter/SquadTask.h"
#include "module/MilitaryManager.h"
#include "setup/SetupManager.h"
#include "terrain/TerrainManager.h"
#include "terrain/PathFinder.h"
#include "CircuitAI.h"
#include "util/utils.h"

#include "AISCommands.h"

namespace circuit {

using namespace springai;

CSupportTask::CSupportTask(ITaskManager* mgr)
		: IFighterTask(mgr, FightType::SUPPORT)
		, updCount(0)
		, isWait(false)
{
	const AIFloat3& pos = manager->GetCircuit()->GetSetupManager()->GetBasePos();
	position = utils::get_radial_pos(pos, SQUARE_SIZE * 32);
}

CSupportTask::~CSupportTask()
{
	PRINT_DEBUG("Execute: %s\n", __PRETTY_FUNCTION__);
}

void CSupportTask::RemoveAssignee(CCircuitUnit* unit)
{
	IFighterTask::RemoveAssignee(unit);
	if (units.empty()) {
		manager->AbortTask(this);
	}
}

void CSupportTask::Execute(CCircuitUnit* unit)
{
	if (isWait) {
		return;
	}

	CCircuitAI* circuit = manager->GetCircuit();
	CTerrainManager* terrainManager = circuit->GetTerrainManager();
	AIFloat3 pos = position;
	terrainManager->CorrectPosition(pos);
	pos = terrainManager->FindBuildSite(unit->GetCircuitDef(), pos, 300.0f, UNIT_COMMAND_BUILD_NO_FACING);

	TRY_UNIT(circuit, unit,
		unit->GetUnit()->Fight(pos, UNIT_COMMAND_OPTION_INTERNAL_ORDER, circuit->GetLastFrame() + FRAMES_PER_SEC * 60);
		unit->GetUnit()->SetWantedMaxSpeed(MAX_UNIT_SPEED);
	)
	isWait = true;
}

void CSupportTask::Update()
{
	if (updCount++ % 8 != 0) {
		return;
	}

	CCircuitUnit* unit = *units.begin();
	const std::set<IFighterTask*>& tasks = static_cast<CMilitaryManager*>(manager)->GetTasks(IFighterTask::FightType::ATTACK);
	if (tasks.empty()) {
		Execute(unit);
		return;
	}

	CCircuitAI* circuit = manager->GetCircuit();
	int frame = circuit->GetLastFrame();
	CTerrainManager* terrainManager = circuit->GetTerrainManager();
	static F3Vec ourPositions;  // NOTE: micro-opt
//	ourPositions.reserve(tasks.size());
	for (IFighterTask* candy : tasks) {
		const AIFloat3& pos = static_cast<ISquadTask*>(candy)->GetLeaderPos(frame);
		if (terrainManager->CanMoveToPos(unit->GetArea(), pos)) {
			ourPositions.push_back(pos);
		}
	}
	if (ourPositions.empty()) {
		Execute(unit);
		return;
	}

	F3Vec path;
	AIFloat3 startPos = unit->GetPos(frame);
	CPathFinder* pathfinder = circuit->GetPathfinder();
	pathfinder->SetMapData(unit, circuit->GetThreatMap(), frame);
	pathfinder->FindBestPath(path, startPos, pathfinder->GetSquareSize(), ourPositions);
	ourPositions.clear();
	if (path.empty()) {
		Execute(unit);
		return;
	}

	const AIFloat3& endPos = path.back();
	if (startPos.SqDistance2D(endPos) < SQUARE(1000.f)) {
		IFighterTask* task = *tasks.begin();
		float minSqDist = std::numeric_limits<float>::max();
		for (IFighterTask* candy : tasks) {
			float sqDist = endPos.SqDistance2D(static_cast<ISquadTask*>(candy)->GetLeaderPos(frame));
			if (minSqDist > sqDist) {
				minSqDist = sqDist;
				task = candy;
			}
		}
		manager->AssignTask(unit, task);
//		manager->DoneTask(this);  // NOTE: RemoveAssignee will abort task
	} else {
		TRY_UNIT(circuit, unit,
			unit->GetUnit()->Fight(endPos, UNIT_COMMAND_OPTION_INTERNAL_ORDER, frame + FRAMES_PER_SEC * 60);
		)
		isWait = false;
	}
}

} // namespace circuit