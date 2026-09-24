#include "ProductionProbe.h"

#include <CCINIClass.h>
#include <HouseClass.h>
#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <Utilities/Debug.h>

bool ProductionProbe::Enabled = false;
bool ProductionProbe::Verbose = false;
int ProductionProbe::ReadConfigPasses = 0;

void ProductionProbe::ReadConfig(CCINIClass* pINI)
{
	if (!pINI)
		return;

	// Pass the CURRENT value as the default, never a literal false.
	//
	// RulesClass::Addition is the "add another INI on top" pass -- it runs for
	// rulesmd AND again for the map's INI. A literal `false` default meant the
	// map pass, which has no [BuildQueueExt] section, silently switched the
	// probe back off: exactly one "enabled" line in the log and then nothing,
	// for six games. This is the same trap Phobos avoids by using
	// Valueable::Read, which leaves the value untouched when the key is absent.
	Enabled = pINI->ReadBool("BuildQueueExt", "Probe", Enabled);
	Verbose = pINI->ReadBool("BuildQueueExt", "Probe.Verbose", Verbose);

	++ReadConfigPasses;
}

void ProductionProbe::LogConfigPass(bool channelTable)
{
	// Log every pass, not just the enabling one, so the number of INI passes is
	// visible in the log rather than inferred -- and report EVERY switch, since
	// a missing one makes "is the feature even on?" unanswerable after the fact.
	Debug::Log("[BQExt] ReadConfig pass %d: Probe=%s Verbose=%s ChannelTable=%s\n",
		ReadConfigPasses,
		Enabled ? "yes" : "no",
		Verbose ? "yes" : "no",
		channelTable ? "yes" : "no");
}

bool ProductionProbe::ShouldLog(FactoryClass* pFactory)
{
	// Cheapest possible bail-out: this runs on hot production paths.
	if (!Enabled || !pFactory)
		return false;

	// Only the local player's factories. AI houses in a skirmish produce
	// constantly and would bury the interesting lines; Q3 is covered by the
	// per-house channel dump instead, which prints every house.
	return pFactory->Owner && pFactory->Owner->IsInPlayerControl;
}

void ProductionProbe::Report(const char* site, FactoryClass* pFactory)
{
	if (!ShouldLog(pFactory))
		return;

	auto const pObject = pFactory->Object;
	auto const pType = pObject ? pObject->GetTechnoType() : nullptr;

	// Q1/Q2: the three suspension-ish flags side by side, with the progress
	// value and the outstanding balance. If Suspend(manual) is already
	// feature #1, "hold" should show IsSuspended=1 IsManual=1 with Production
	// frozen and Balance retained.
	Debug::Log(
		"[BQExt] %-22s obj=%-12s prog=%4d/%d queued=%d "
		"OnHold=%d Susp=%d Manual=%d Bal=%d/%d\n",
		site,
		pType ? pType->ID : "(none)",
		pFactory->Production.Value,
		pFactory->Production.Step,
		pFactory->QueuedObjects.Count,
		pFactory->OnHold ? 1 : 0,
		pFactory->IsSuspended ? 1 : 0,
		pFactory->IsManual ? 1 : 0,
		pFactory->Balance,
		pFactory->OriginalBalance);
}

void ProductionProbe::ReportHouseChannels(HouseClass* pHouse, const char* site)
{
	if (!Enabled || !pHouse)
		return;

	// Q4: how many live factories this house owns, and what each is building.
	// Q3: Production is documented in YRpp as "AI production has begun" — print
	// it next to IsHumanPlayer so the correlation is observable rather than
	// assumed (DESIGN.md §3 gates P2 on this).
	int owned = 0;

	Debug::Log("[BQExt] === %s | house=%s idx=%d Human=%d InPlayerControl=%d "
		"Production=%d ===\n",
		site,
		pHouse->PlainName,
		pHouse->ArrayIndex,
		pHouse->IsHumanPlayer ? 1 : 0,
		pHouse->IsInPlayerControl ? 1 : 0,
		pHouse->Production ? 1 : 0);

	for (auto const pFactory : FactoryClass::Array)
	{
		if (!pFactory || pFactory->Owner != pHouse)
			continue;

		++owned;

		auto const pObject = pFactory->Object;
		auto const pType = pObject ? pObject->GetTechnoType() : nullptr;

		// WhatAmI() of the produced object is what the Ares-lineage channel
		// bookkeeping switches on (see the encyclopedia page for 0x4CA07A) —
		// so print it, not the factory building's Factory= type.
		// Print Naval explicitly. WhatAmI() reports Unit(1) for both vehicles
		// and ships, so without this the two channels are indistinguishable in
		// the dump -- and vehicle-vs-naval is one of the central distinctions
		// of this whole subsystem (they share a tab but not a queue). The first
		// run built a Dolphin and the log could not show it.
		Debug::Log("[BQExt]     factory obj=%-12s abs=%d naval=%d prog=%4d queued=%d "
			"Susp=%d Manual=%d OnHold=%d\n",
			pType ? pType->ID : "(none)",
			pObject ? static_cast<int>(pObject->WhatAmI()) : -1,
			pType && pType->Naval ? 1 : 0,
			pFactory->Production.Value,
			pFactory->QueuedObjects.Count,
			pFactory->IsSuspended ? 1 : 0,
			pFactory->IsManual ? 1 : 0,
			pFactory->OnHold ? 1 : 0);
	}

	Debug::Log("[BQExt]     total live factories for this house: %d\n", owned);
}
