#include "LimboOnComplete.h"

#include <BuildingClass.h>
#include <CCINIClass.h>
#include <SessionClass.h>

#include <vector>
#include <Utilities/Debug.h>

std::set<BuildingTypeClass*> LimboOnComplete::Types;
static std::vector<FactoryClass*> PendingFactories;

void LimboOnComplete::ReadConfig(CCINIClass* pINI)
{
	if (!pINI)
		return;

	// Deliberately NOT Types.clear(). RulesClass::Addition runs once per INI --
	// rulesmd, then the map's -- and clearing here meant the map pass wiped
	// every flag set by rules. Instead each type's CURRENT state is the default,
	// so an absent key changes nothing while an explicit `no` in a map INI still
	// turns it off.
	//
	// BuildingTypeClass::Array is a reference, not a pointer — no indirection.
	for (auto const pType : BuildingTypeClass::Array)
	{
		if (!pType)
			continue;

		bool const was = Types.find(pType) != Types.end();
		bool const now = pINI->ReadBool(pType->ID, "LimboOnComplete", was);

		if (now == was)
			continue;

		if (now)
			Types.insert(pType);
		else
			Types.erase(pType);

		Debug::Log("[BQExt] LimboOnComplete %s: %s\n",
			now ? "enabled" : "disabled", pType->ID);
	}
}

bool LimboOnComplete::IsEnabledFor(BuildingTypeClass* pType)
{
	return pType && Types.find(pType) != Types.end();
}

bool LimboOnComplete::Create(BuildingTypeClass* pType, HouseClass* pOwner)
{
	if (!pType || !pOwner)
		return false;

	// BuildLimit is checked before creation, matching Phobos. Without this a
	// limbo delivery could exceed a limit that the normal build path enforces,
	// and because limbo buildings can never be removed the excess is permanent.
	if (pType->BuildLimit > 0)
	{
		int sum = pOwner->CountOwnedNow(pType);

		if (auto const pUndeploy = pType->UndeploysInto)
			sum += pOwner->CountOwnedNow(pUndeploy);

		if (sum >= pType->BuildLimit)
			return false;
	}

	auto const pBuilding = static_cast<BuildingClass*>(pType->CreateObject(pOwner));

	if (!pBuilding)
		return false;

	// These three are what make it "exists but is not on the map". InLimbo is
	// deliberately false -- see the header note.
	pBuilding->InLimbo = false;
	pBuilding->IsAlive = true;
	pBuilding->IsOnMap = true;

	// Power drain/output discovery is only evaluated in campaign for some
	// logics, and a limbo building is never unshrouded, so reveal explicitly
	// rather than relying on the shroud check.
	if (SessionClass::IsCampaign())
		pBuilding->DiscoveredBy(HouseClass::CurrentPlayer);

	pBuilding->DiscoveredBy(pOwner);

	pOwner->RegisterGain(pBuilding, false);
	pOwner->RecheckTechTree = true;
	pOwner->RecheckPower = true;
	pOwner->Buildings.AddItem(pBuilding);

	// Per-role registrations. Without these the building would count for
	// prerequisites but silently fail to act as a ConYard / lab / cost plant.
	if (pType->ConstructionYard)
		pOwner->ConYards.AddItem(pBuilding);

	if (pType->SecretLab)
		pOwner->SecretLabs.AddItem(pBuilding);

	if (pType->FactoryPlant)
	{
		pOwner->FactoryPlants.AddItem(pBuilding);
		pOwner->CalculateCostMultipliers();
	}

	// BuildingClass::Place already ran inside DiscoveredBy, which is what books
	// the Ore Purifier and self-heal house counters.

	return true;
}

void LimboOnComplete::MarkPending(FactoryClass* pFactory)
{
	if (Types.empty() || !pFactory)
		return;

	auto const pObject = pFactory->Object;

	if (!pObject || pObject->WhatAmI() != BuildingClass::AbsID)
		return;

	auto const pType = static_cast<BuildingTypeClass*>(pObject->GetTechnoType());

	if (!IsEnabledFor(pType) || !pFactory->IsDone())
		return;

	// Record only. Touching the factory here is what crashed the game.
	for (auto const pQueued : PendingFactories)
		if (pQueued == pFactory)
			return;

	PendingFactories.push_back(pFactory);
}

void LimboOnComplete::ProcessPending()
{
	if (PendingFactories.empty())
		return;

	auto const pending = PendingFactories;
	PendingFactories.clear();

	for (auto const pFactory : pending)
	{
		// The factory may have been destroyed since it was queued -- the player
		// can sell or lose the producing building between frames. Validate by
		// membership in the live array; a stale pointer here would be a
		// use-after-free in the middle of the logic loop.
		bool alive = false;

		for (auto const pLive : FactoryClass::Array)
		{
			if (pLive == pFactory)
			{
				alive = true;
				break;
			}
		}

		if (!alive)
			continue;

		auto const pObject = pFactory->Object;

		if (!pObject || pObject->WhatAmI() != BuildingClass::AbsID)
			continue;

		auto const pType = static_cast<BuildingTypeClass*>(pObject->GetTechnoType());

		if (!IsEnabledFor(pType) || !pFactory->IsDone())
			continue;

		auto const pOwner = pFactory->Owner;

		if (!pOwner)
			continue;

		// The no-refund assumption: Balance is "credits the house still owes us
		// for building this", so a completed item has Balance 0 and the abandon
		// below refunds nothing. Measured at 0/0 across 44 completions.
		if (pFactory->Balance != 0)
		{
			Debug::Log("[BQExt] LimboOnComplete ABORT %s: balance %d != 0 on a"
				" finished item; delivering would refund credits\n",
				pType->ID, pFactory->Balance);
			continue;
		}

		if (!Create(pType, pOwner))
			continue;

		// Safe HERE, but not in the completion hook: the engine has finished
		// with this factory for the frame.
		pFactory->AbandonProduction();

		Debug::Log("[BQExt] LimboOnComplete delivered %s to house %s\n",
			pType->ID, pOwner->PlainName);
	}
}
