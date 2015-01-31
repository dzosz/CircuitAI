/*
 * EnergyTask.cpp
 *
 *  Created on: Jan 30, 2015
 *      Author: rlcevg
 */

#include "task/builder/EnergyTask.h"
#include "util/utils.h"

namespace circuit {

using namespace springai;

CBEnergyTask::CBEnergyTask(CCircuitAI* circuit, Priority priority,
						   UnitDef* buildDef, const AIFloat3& position,
						   BuildType type, float cost, int timeout) :
		IBuilderTask(circuit, priority, buildDef, position, BuildType::ENERGY, cost, timeout)
{
}

CBEnergyTask::~CBEnergyTask()
{
	PRINT_DEBUG("Execute: %s\n", __PRETTY_FUNCTION__);
}

} // namespace circuit