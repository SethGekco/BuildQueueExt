#include "BuildOffAnyBuilding.h"

#include <CCINIClass.h>

#include <Utilities/Debug.h>

std::set<BuildingTypeClass*> BuildOffAnyBuilding::Types;
std::set<BuildingTypeClass*> BuildOffAnyBuilding::NoProximityTypes;
bool BuildOffAnyBuilding::Enabled = false;
static int ProximitySkips = 0;

void BuildOffAnyBuilding::ReadGlobalConfig(CCINIClass* pINI)
{
	if (!pINI)
		return;

	// Current value as the default, never a literal: RulesClass::Read_File runs
	// once per INI and a literal would switch this off on the map pass.
	Enabled = pINI->ReadBool(
		"BuildQueueExt", "BuildOffAnyBuilding.Enabled", Enabled);
}

void BuildOffAnyBuilding::ReadTypeConfig(CCINIClass* pINI)
{
	if (!pINI)
		return;

	// Read at the Read_File TAIL, where the type array actually exists. At the
	// entry it is still empty on the rulesmd pass, which silently swallowed
	// three earlier per-type tags in this project.
	for (auto const pType : BuildingTypeClass::Array)
	{
		if (!pType)
			continue;

		bool const npWas = NoProximityTypes.find(pType) != NoProximityTypes.end();
		bool const npNow =
			pINI->ReadBool(pType->ID, "Adjacent.NotRequired", npWas);

		if (npNow != npWas)
		{
			if (npNow)
				NoProximityTypes.insert(pType);
			else
				NoProximityTypes.erase(pType);

			Debug::Log("[BQExt] Adjacent.NotRequired %s: %s (Adjacent=%d)\n",
				npNow ? "enabled" : "disabled", pType->ID, pType->Adjacent);
		}

		bool const was = Types.find(pType) != Types.end();
		bool const now = pINI->ReadBool(pType->ID, "BuildOffAnyBuilding", was);

		if (now == was)
			continue;

		if (now)
			Types.insert(pType);
		else
			Types.erase(pType);

		Debug::Log("[BQExt] BuildOffAnyBuilding %s: %s (BaseNormal=%d)\n",
			now ? "enabled" : "disabled", pType->ID,
			pType->BaseNormal ? 1 : 0);
	}
}

void BuildOffAnyBuilding::CaptureType(BuildingTypeClass* pType)
{
	// Write-only. See the reentrancy note in the header: Phobos re-enters this
	// function with the same pType, so overwriting is harmless, but clearing on
	// exit would blank the outer call's value.
	PlacingType = pType;

	if (++EntryCalls == 1)
	{
		Debug::Log("[BQExt] BuildOffAnyBuilding proximity entry hook IS LIVE"
			" (first call: type=%s, tagged=%d, enabled=%d)\n",
			pType ? pType->ID : "(null)",
			(pType && Types.find(pType) != Types.end()) ? 1 : 0,
			Enabled ? 1 : 0);
	}
}

bool BuildOffAnyBuilding::ShouldIgnoreBaseNormal(BuildingClass* pCellBuilding)
{
	if (!Enabled || !PlacingType)
		return false;

	if (Types.find(PlacingType) == Types.end())
		return false;

	// Mirror the exclusions the engine would apply to any anchor. A building
	// mid-sale or in limbo is not something you should be able to build off,
	// and accepting one would hand back an anchor that may vanish this frame.
	if (!pCellBuilding || !pCellBuilding->IsAlive || pCellBuilding->InLimbo)
		return false;

	if (pCellBuilding->GetCurrentMission() == Mission::Selling
		|| pCellBuilding->QueuedMission == Mission::Selling)
	{
		return false;
	}

	// Deliberately NOT gated on power. BaseNormal is a layout property, not an
	// operational one, and the engine's own BaseNormal test ignores power too --
	// adding a power condition here would invent a rule the modder did not ask
	// for. (Confirmed in game that an unpowered building is a usable anchor.)

	if (++Accepts == 1 || Accepts % 500 == 0)
	{
		Debug::Log("[BQExt] BuildOffAnyBuilding ANCHOR #%d placing=%s ->"
			" accepted %s (its BaseNormal=%d)\n",
			Accepts, PlacingType->ID,
			pCellBuilding->Type ? pCellBuilding->Type->ID : "(?)",
			pCellBuilding->Type && pCellBuilding->Type->BaseNormal ? 1 : 0);
	}

	return true;
}

bool BuildOffAnyBuilding::SkipsProximityCheck(void* pType)
{
	if (!Enabled || !pType || NoProximityTypes.empty())
		return false;

	// Pointer identity only. NoProximityTypes is populated solely from
	// BuildingTypeClass::Array, so a UnitType or garbage value cannot match and
	// is never dereferenced.
	auto const candidate = reinterpret_cast<BuildingTypeClass*>(pType);

	if (NoProximityTypes.find(candidate) == NoProximityTypes.end())
		return false;

	if (++ProximitySkips == 1 || ProximitySkips % 2000 == 0)
	{
		Debug::Log("[BQExt] Adjacent.NotRequired SKIP #%d %s --"
			" proximity check forced to pass\n",
			ProximitySkips, candidate->ID);
	}

	return true;
}




BuildingTypeClass* BuildOffAnyBuilding::PlacingTypeNow()
{
	return PlacingType;
}







