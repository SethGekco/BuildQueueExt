#include "KickOutAlt.h"
#include <string>

#include <CCINIClass.h>
#include <Utilities/Debug.h>

bool KickOutAlt::Enabled = false;
std::map<BuildingTypeClass*, std::vector<BuildingTypeClass*>> KickOutAlt::AltFrom;
std::set<BuildingTypeClass*> KickOutAlt::AnyFactory;

static int Accepted = 0;
static int Considered = 0;
static int Rejected = 0;

// ⚠ LOG THE NEAR MISSES, NOT JUST THE HITS. Four times in this project a
// decision seat logged only its terminal outcome, and a run that produced zero
// lines was undiagnosable. A candidate our tag named but the engine's own
// conditions refused is exactly the case a future bug report will be about.
static void LogDecision(char const* why,
	BuildingClass* pBlocked, BuildingClass* pCandidate)
{
	if (++Rejected == 1 || Rejected % 50 == 0)
	{
		Debug::Log("[BQExt] KickOut.AltFrom reject #%d (%s) blocked=%s"
			" candidate=%s\n", Rejected, why,
			pBlocked->Type ? pBlocked->Type->ID : "(?)",
			pCandidate->Type ? pCandidate->Type->ID : "(?)");
	}
}

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

bool KickOutAlt::AcceptsAsExit(
	BuildingClass* pBlocked, BuildingClass* pCandidate)
{
	++Considered;

	if (!Enabled || !pBlocked || !pBlocked->Type
		|| !pCandidate || !pCandidate->Type)
	{
		return false;
	}

	if (!Accepts(pBlocked, pCandidate))
		return false;

	// =====================================================================
	// Re-apply the engine's own conditions. We are about to jump over both
	// Antares' predicate AND the engine's remaining checks at
	// 0x4444EA-0x444502, so everything they would have tested has to be
	// tested here or we hand the exit code a candidate it cannot use.
	//
	//   0x4444EA  call [ebp]->vt[0x184] ; cmp eax,5  -> Mission::Guard
	//   0x4444FA  mov eax,[ebp+0x524]   ; test; je   -> Factory must be null
	if (pCandidate->GetCurrentMission() != Mission::Guard)
	{
		LogDecision("mission", pBlocked, pCandidate);
		return false;
	}

	// Already building something itself. The accept path at 0x44451F MOVES
	// the blocked building's FactoryClass* into this one's slot, so a
	// candidate with its own factory would have it silently overwritten.
	if (pCandidate->Factory)
	{
		LogDecision("busy", pBlocked, pCandidate);
		return false;
	}

	if (++Accepted == 1 || Accepted % 50 == 0)
	{
		Debug::Log("[BQExt] KickOut.AltFrom ACCEPT #%d blocked=%s ->"
			" exiting from %s instead  [considered %d]\n",
			Accepted, pBlocked->Type->ID, pCandidate->Type->ID, Considered);
	}

	return true;
}
