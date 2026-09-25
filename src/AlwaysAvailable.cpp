#include "AlwaysAvailable.h"

#include <CCINIClass.h>
#include <BuildingClass.h>
#include <BuildingTypeClass.h>
#include <InfantryTypeClass.h>
#include <UnitTypeClass.h>
#include <AircraftTypeClass.h>
#include <Utilities/Debug.h>

std::set<TechnoTypeClass*> AlwaysAvailable::Types;
bool AlwaysAvailable::ProbeEnabled = false;
bool AlwaysAvailable::Enabled = false;
int AlwaysAvailable::Substitutions = 0;
int AlwaysAvailable::NoStandIn = 0;
int AlwaysAvailable::Calls = 0;
int AlwaysAvailable::TypeRecovered = 0;
int AlwaysAvailable::TypeLost = 0;
int AlwaysAvailable::NullVerdicts = 0;

void AlwaysAvailable::ReadGlobalConfig(CCINIClass* pINI)
{
	if (!pINI)
		return;

	// Current value as the default, never a literal — RulesClass::Read_File runs
	// once per INI and a literal would switch this off on the map pass.
	ProbeEnabled = pINI->ReadBool(
		"BuildQueueExt", "AlwaysAvailable.Probe", ProbeEnabled);
	Enabled = pINI->ReadBool(
		"BuildQueueExt", "AlwaysAvailable.Enabled", Enabled);
}

void AlwaysAvailable::ReadTypeConfig(CCINIClass* pINI)
{
	if (!pINI)
		return;

	// Buildings only: the feature is "needs no Construction Yard", and the
	// ConYard is the BuildingType factory. Units have their own factory
	// requirement, which is a separate question.
	//
	// Called from the Read_File TAIL, because at the entry this array is still
	// empty on the rulesmd pass -- the very bug that made the first armed test
	// silently do nothing.
	for (auto const pType : BuildingTypeClass::Array)
	{
		if (!pType)
			continue;

		bool const was = Types.find(pType) != Types.end();
		bool const now = pINI->ReadBool(pType->ID, "AlwaysAvailable", was);

		if (now == was)
			continue;

		if (now)
			Types.insert(pType);
		else
			Types.erase(pType);

		Debug::Log("[BQExt] AlwaysAvailable %s: %s\n",
			now ? "enabled" : "disabled", pType->ID);
	}
}

bool AlwaysAvailable::IsEnabledFor(TechnoTypeClass* pType)
{
	return pType && Types.find(pType) != Types.end();
}

TechnoTypeClass* AlwaysAvailable::IdentifyType(void* candidate)
{
	if (!candidate)
		return nullptr;

	// Pointer comparison ONLY. The candidate is a raw register value that may
	// be anything at all, so it must never be dereferenced -- a garbage value
	// simply matches nothing here and falls through as null.
	for (auto const pType : BuildingTypeClass::Array)
		if (pType == candidate)
			return pType;

	for (auto const pType : InfantryTypeClass::Array)
		if (pType == candidate)
			return pType;

	for (auto const pType : UnitTypeClass::Array)
		if (pType == candidate)
			return pType;

	for (auto const pType : AircraftTypeClass::Array)
		if (pType == candidate)
			return pType;

	return nullptr;
}

void AlwaysAvailable::ProbeEpilogue(
	void* ecx, BuildingClass* pVerdict, HouseClass* pHouse)
{
	if (!ProbeEnabled)
		return;

	++Calls;

	auto const pType = IdentifyType(ecx);

	if (pType)
		++TypeRecovered;
	else
		++TypeLost;

	if (!pVerdict)
		++NullVerdicts;

	// Sparse, because FindFactory is on the sidebar's path and runs constantly.
	// The first call and every 5000th is enough to answer the question; the
	// running totals carry the actual verdict.
	if (Calls == 1 || Calls % 5000 == 0)
	{
		Debug::Log("[BQExt] AlwaysAvailable epilogue #%d: ecx=%p type=%s"
			" verdict=%p house=%p  [recovered %d, lost %d, nullVerdict %d]\n",
			Calls, ecx,
			pType ? pType->ID : "(UNRECOVERABLE)",
			pVerdict, pHouse,
			TypeRecovered, TypeLost, NullVerdicts);
	}

	// The case the feature exists to serve: a tagged type that the engine says
	// has no factory. Log every one of these -- they should be rare, and if
	// they never appear the tag is not reaching this seat.
	if (!pVerdict && pType && IsEnabledFor(pType))
	{
		Debug::Log("[BQExt] AlwaysAvailable WOULD SUBSTITUTE for %s"
			" (house=%p) -- no factory, type is tagged\n",
			pType->ID, pHouse);
	}
}

BuildingClass* AlwaysAvailable::FindStandIn(HouseClass* pHouse)
{
	if (!pHouse)
		return nullptr;

	BuildingClass* pFallback = nullptr;

	for (auto const pBld : pHouse->Buildings)
	{
		// Mirror HasFactory's own exclusions. Handing back something the engine
		// would itself have skipped is how a substitution turns into a crash
		// later, in a frame with no obvious connection to this one.
		if (!pBld || !pBld->IsAlive || pBld->InLimbo)
			continue;

		if (pBld->GetCurrentMission() == Mission::Selling
			|| pBld->QueuedMission == Mission::Selling)
		{
			continue;
		}

		// Prefer a powered, active building: callers ask the returned factory
		// whether it has power, and an offline stand-in would report the type
		// as unbuildable for a reason the modder never configured.
		if (pBld->HasPower && !pBld->Deactivated)
			return pBld;

		if (!pFallback)
			pFallback = pBld;
	}

	return pFallback;
}

BuildingClass* AlwaysAvailable::Resolve(
	void* ecx, BuildingClass* pVerdict, HouseClass* pHouse)
{
	// Never override a real answer -- only fill in a null one.
	if (!Enabled || pVerdict || !pHouse)
		return pVerdict;

	auto const pType = IdentifyType(ecx);

	if (!pType || !IsEnabledFor(pType))
		return pVerdict;

	auto const pStandIn = FindStandIn(pHouse);

	if (!pStandIn)
	{
		// A house with no buildings at all. Nothing to stand in, and inventing
		// something is exactly what this option exists to avoid.
		++NoStandIn;
		return pVerdict;
	}

	if (++Substitutions == 1 || Substitutions % 500 == 0)
	{
		Debug::Log("[BQExt] AlwaysAvailable SUBSTITUTE #%d %s -> stand-in %s"
			" (house=%p)  [no-stand-in %d]\n",
			Substitutions, pType->ID,
			pStandIn->Type ? pStandIn->Type->ID : "(?)",
			pHouse, NoStandIn);
	}

	return pStandIn;
}
