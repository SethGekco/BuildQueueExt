#include "ExtraQueues.h"

#include <BuildingClass.h>
#include <CCINIClass.h>
#include <Utilities/Debug.h>

bool ExtraQueues::ReportEnabled = false;
std::map<BuildingTypeClass*, int> ExtraQueues::Grants;
std::map<HouseClass*, std::map<ExtraQueues::Channel, int>> ExtraQueues::LastReported;
int ExtraQueues::FrameCounter = 0;

void ExtraQueues::ReadGlobalConfig(CCINIClass* pINI)
{
	if (!pINI)
		return;

	// Current value as the default -- Read_File runs once per INI and a literal
	// would switch this off on the map pass.
	ReportEnabled = pINI->ReadBool(
		"BuildQueueExt", "ExtraQueues.Report", ReportEnabled);
}

void ExtraQueues::ReadTypeConfig(CCINIClass* pINI)
{
	if (!pINI)
		return;

	// MUST be called from the Read_File TAIL. At the entry BuildingTypeClass::
	// Array is still empty on the rulesmd pass, so this loop would read nothing
	// and the tag would appear to do nothing at all -- the exact failure that
	// cost a playthrough with AlwaysAvailable and LimboOnComplete.
	for (auto const pType : BuildingTypeClass::Array)
	{
		if (!pType)
			continue;

		auto const it = Grants.find(pType);
		int const was = it == Grants.end() ? 0 : it->second;
		int const now = pINI->ReadInteger(pType->ID, "Factory.ExtraQueues", was);

		if (now == was)
			continue;

		if (now > 0)
			Grants[pType] = now;
		else
			Grants.erase(pType);

		Debug::Log("[BQExt] ExtraQueues: %s grants %d (was %d), produces abs=%d"
			" naval=%d\n",
			pType->ID, now, was,
			static_cast<int>(pType->Factory), pType->Naval ? 1 : 0);
	}
}

int ExtraQueues::GrantOf(BuildingTypeClass* pType)
{
	if (!pType)
		return 0;

	auto const it = Grants.find(pType);

	return it == Grants.end() ? 0 : it->second;
}

int ExtraQueues::SlotsFor(HouseClass* pHouse, Channel const& channel)
{
	// Slot 0 always exists: it is the engine's own primary.
	int slots = 1;

	if (!pHouse || Grants.empty())
		return slots;

	for (auto const pBld : pHouse->Buildings)
	{
		// Mirror HasFactory's exclusions -- a building being sold or in limbo is
		// not a factory the engine would use, so it should not grant capacity
		// either. Counting one would promise a queue that cannot produce.
		if (!pBld || !pBld->IsAlive || pBld->InLimbo)
			continue;

		if (pBld->GetCurrentMission() == Mission::Selling
			|| pBld->QueuedMission == Mission::Selling)
		{
			continue;
		}

		auto const pType = pBld->Type;

		if (!pType || pType->Factory != channel.Produces)
			continue;

		// Naval only distinguishes UnitType channels; for every other
		// AbstractType the engine ignores the flag entirely (§1).
		if (channel.Produces == AbstractType::UnitType
			&& pType->Naval != channel.IsNaval)
		{
			continue;
		}

		slots += GrantOf(pType);
	}

	return slots;
}

void ExtraQueues::ReportChanges()
{
	if (!ReportEnabled || Grants.empty())
		return;

	// Throttled: this walks every house's building list, and P-a only needs to
	// observe transitions, not sample continuously.
	if (++FrameCounter % 30 != 0)
		return;

	static Channel const channels[] = {
		{ AbstractType::BuildingType,  false },
		{ AbstractType::InfantryType,  false },
		{ AbstractType::UnitType,      false },
		{ AbstractType::UnitType,      true  },
		{ AbstractType::AircraftType,  false },
	};

	for (auto const pHouse : HouseClass::Array)
	{
		if (!pHouse || pHouse->Defeated)
			continue;

		for (auto const& channel : channels)
		{
			int const slots = SlotsFor(pHouse, channel);

			auto& last = LastReported[pHouse][channel];

			// Report transitions only. A per-frame dump of an unchanging number
			// is exactly the kind of log that hides the one line that matters.
			if (slots == last)
				continue;

			// Skip the initial 1 -> 1 no-op so an untagged game stays silent.
			if (!(last == 0 && slots == 1))
			{
				Debug::Log("[BQExt] ExtraQueues %s: abs=%d naval=%d slots %d -> %d\n",
					pHouse->PlainName,
					static_cast<int>(channel.Produces),
					channel.IsNaval ? 1 : 0,
					last == 0 ? 1 : last, slots);
			}

			last = slots;
		}
	}
}
