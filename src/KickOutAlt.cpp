#include "KickOutAlt.h"
#include <string>

#include <CCINIClass.h>
#include <Utilities/Debug.h>

bool KickOutAlt::Enabled = false;
std::map<BuildingTypeClass*, std::vector<BuildingTypeClass*>> KickOutAlt::AltFrom;
std::set<BuildingTypeClass*> KickOutAlt::AnyFactory;

static int Accepted = 0;
static int Considered = 0;

void KickOutAlt::ReadGlobalConfig(CCINIClass* pINI)
{
	if (!pINI)
		return;

	// Current value as the default, never a literal: RulesClass::Read_File runs
	// once per INI and a literal would switch this off on the map pass.
	Enabled = pINI->ReadBool("BuildQueueExt", "KickOut.Alt.Enabled", Enabled);
}

void KickOutAlt::ReadTypeConfig(CCINIClass* pINI)
{
	if (!pINI)
		return;

	// Read at the Read_File TAIL, where the type array exists. At the entry it
	// is still empty on the rulesmd pass -- the bug that silently swallowed
	// three earlier per-type tags in this project.
	for (auto const pType : BuildingTypeClass::Array)
	{
		if (!pType)
			continue;

		bool const anyWas = AnyFactory.find(pType) != AnyFactory.end();
		bool const anyNow =
			pINI->ReadBool(pType->ID, "KickOut.AltAnyFactory", anyWas);

		if (anyNow != anyWas)
		{
			if (anyNow)
				AnyFactory.insert(pType);
			else
				AnyFactory.erase(pType);

			Debug::Log("[BQExt] KickOut.AltAnyFactory %s: %s\n",
				anyNow ? "enabled" : "disabled", pType->ID);
		}

		char buffer[1024] = {};

		if (pINI->ReadString(pType->ID, "KickOut.AltFrom", "",
			buffer, sizeof(buffer)) <= 0)
		{
			continue;
		}

		std::vector<BuildingTypeClass*> list;
		std::string cur;

		// Split on commas, dropping whitespace. Resolved to pointers once here
		// rather than per candidate, because this runs in the kick-out path.
		for (char const* p = buffer; ; ++p)
		{
			if (*p == ',' || *p == '\0')
			{
				if (!cur.empty())
				{
					if (auto const pFound = BuildingTypeClass::Find(cur.c_str()))
						list.push_back(pFound);
					else
						Debug::Log("[BQExt] [%s]KickOut.AltFrom: unknown"
							" BuildingType '%s'\n", pType->ID, cur.c_str());

					cur.clear();
				}

				if (*p == '\0')
					break;
			}
			else if (*p != ' ' && *p != '\t')
			{
				cur += *p;
			}
		}

		if (list.empty())
		{
			AltFrom.erase(pType);
			continue;
		}

		AltFrom[pType] = list;

		Debug::Log("[BQExt] KickOut.AltFrom %s: %d alternate factor(ies)\n",
			pType->ID, int(list.size()));
	}
}

bool KickOutAlt::Accepts(BuildingClass* pBlocked, BuildingClass* pCandidate)
{
	auto const pBlockedType = pBlocked->Type;
	auto const pCandType = pCandidate->Type;

	// The engine's own identity case. Left to vanilla so an untagged factory
	// behaves bit-for-bit as before.
	if (pBlockedType == pCandType)
		return false;

	// Mirror the exclusions any sane exit candidate needs. The engine checks
	// the candidate's mission and busy-ness itself just after the comparison we
	// are feeding, so this only covers what it cannot: a building that is dead,
	// in limbo or being sold.
	if (!pCandidate->IsAlive || pCandidate->InLimbo)
		return false;

	if (pCandidate->GetCurrentMission() == Mission::Selling
		|| pCandidate->QueuedMission == Mission::Selling)
	{
		return false;
	}

	auto const it = AltFrom.find(pBlockedType);

	if (it != AltFrom.end())
	{
		for (auto const pAllowed : it->second)
			if (pAllowed == pCandType)
				return true;
	}

	// KickOut.AltAnyFactory: any building producing the same CATEGORY, with
	// Naval deliberately not consulted -- that is the whole point, and it is
	// also what the engine's own search never looks at.
	if (AnyFactory.find(pBlockedType) != AnyFactory.end())
	{
		if (pCandType && pBlockedType
			&& pCandType->Factory == pBlockedType->Factory
			&& pCandType->Factory != AbstractType::None)
		{
			return true;
		}
	}

	return false;
}

BuildingTypeClass* KickOutAlt::TypeForCompare(
	BuildingClass* pBlocked, BuildingClass* pCandidate)
{
	// Any doubt at all -> hand back the value the stolen instruction would have
	// produced, so the comparison behaves exactly as vanilla.
	if (!pBlocked || !pBlocked->Type)
		return nullptr;

	if (!Enabled || !pCandidate || !pCandidate->Type)
		return pBlocked->Type;

	if (!Accepts(pBlocked, pCandidate))
		return pBlocked->Type;

	// Returning the CANDIDATE's own type makes `cmp ecx,edx` compare equal, so
	// the engine treats this building as a valid alternate exit.
	if (++Accepted == 1 || Accepted % 100 == 0)
	{
		Debug::Log("[BQExt] KickOut.AltFrom ACCEPT #%d blocked=%s ->"
			" alternate %s  [considered %d]\n",
			Accepted, pBlocked->Type->ID, pCandidate->Type->ID, Considered);
	}

	return pCandidate->Type;
}
