#include "LimboOnComplete.h"

#include <BuildingClass.h>
#include <CCINIClass.h>
#include <SessionClass.h>
#include <Utilities/Debug.h>

std::set<BuildingTypeClass*> LimboOnComplete::Types;

void LimboOnComplete::ReadConfig(CCINIClass* pINI)
{
	if (!pINI)
		return;

	Types.clear();

	// BuildingTypeClass::Array is a reference, not a pointer — no indirection.
	for (auto const pType : BuildingTypeClass::Array)
	{
		if (!pType)
			continue;

		if (pINI->ReadBool(pType->ID, "LimboOnComplete", false))
		{
			Types.insert(pType);
			Debug::Log("[BQExt] LimboOnComplete: %s\n", pType->ID);
		}
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

bool LimboOnComplete::TryDeliver(FactoryClass* pFactory)
{
	if (Types.empty() || !pFactory)
		return false;

	auto const pObject = pFactory->Object;

	if (!pObject || pObject->WhatAmI() != BuildingClass::AbsID)
		return false;

	auto const pType = static_cast<BuildingTypeClass*>(pObject->GetTechnoType());

	if (!IsEnabledFor(pType))
		return false;

	// Only act once the item is genuinely finished, otherwise this would
	// deliver a building the player has not paid off yet.
	if (!pFactory->IsDone())
		return false;

	auto const pOwner = pFactory->Owner;

	if (!pOwner)
		return false;

	// The no-refund assumption: Balance is "credits the house still owes us for
	// building this", so a completed item has Balance 0 and AbandonProduction
	// below refunds nothing. The P0 probe logs Bal= at CompletedProduction
	// precisely so this can be confirmed rather than assumed -- if the log ever
	// shows a non-zero balance on a finished item, this path grants free money.
	if (pFactory->Balance != 0)
	{
		Debug::Log("[BQExt] LimboOnComplete ABORT %s: balance %d != 0 on a"
			" finished item; delivering would refund credits\n",
			pType->ID, pFactory->Balance);
		return false;
	}

	if (!Create(pType, pOwner))
		return false;

	// Clear the factory so the queue advances and the cameo stops showing a
	// ready item. AbandonProduction is the engine's own teardown: it also
	// releases the house's channel slot via the Antares/Phobos hooks at
	// 0x4CA07A, which a manual pointer clear would not.
	pFactory->AbandonProduction();

	Debug::Log("[BQExt] LimboOnComplete delivered %s to house %s\n",
		pType->ID, pOwner->PlainName);

	return true;
}
