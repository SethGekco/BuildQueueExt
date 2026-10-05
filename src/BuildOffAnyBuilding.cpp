#include "BuildOffAnyBuilding.h"

#include <CCINIClass.h>

#include <map>
#include <Utilities/Debug.h>

std::set<BuildingTypeClass*> BuildOffAnyBuilding::Types;
std::set<BuildingTypeClass*> BuildOffAnyBuilding::NoProximityTypes;
static int ProximitySkips = 0;
static int AnchorAccepts = 0;
static int AnchorRefusals = 0;

// Per-type anchor rules, tri-state: -1 unset (vanilla), 0 forbidden, 1 allowed.
struct AnchorRules
{
	int Owner   = -1;
	int Team    = -1;
	int Ally    = -1;
	int Enemy   = -1;
	int Neutral = -1;

	bool AnySet() const
	{
		return Owner >= 0 || Team >= 0 || Ally >= 0 || Enemy >= 0 || Neutral >= 0;
	}
};

static std::map<BuildingTypeClass*, AnchorRules> AnchorMap;

// `Anchor.For<scope>` on the ANCHOR type -- the other half of the permission.
// Same tri-state convention; -1 means "use the vanilla default for this scope",
// which differs per scope (BaseNormal vs EligibileForAllyBuilding).
static std::map<BuildingTypeClass*, AnchorRules> AnchorForMap;

// Tri-state INI read. CCINIClass has no "is the key present" query, so read the
// same key twice with opposite defaults: if both agree the key is really there,
// and if they disagree it is absent and the current value is preserved. That
// preservation matters because Read_File runs once per INI -- the map pass would
// otherwise reset every rule to unset.
static int ReadTriState(
	CCINIClass* pINI, const char* section, const char* key, int current)
{
	bool const asFalse = pINI->ReadBool(section, key, false);
	bool const asTrue  = pINI->ReadBool(section, key, true);

	if (asFalse == asTrue)
		return asFalse ? 1 : 0;

	return current;
}
bool BuildOffAnyBuilding::Enabled = false;

static BuildingTypeClass* PlacingType = nullptr;
static int Accepts = 0;
static int EntryCalls = 0;

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

		// Anchor scopes. Parsed for every type, but only stored when at least
		// one key is actually present, so AnchorMap stays small and
		// AnchorVerdict can exit on a single lookup for untagged types.
		{
			auto& rules = AnchorMap[pType];
			AnchorRules const before = rules;

			rules.Owner   = ReadTriState(pINI, pType->ID, "Adjacent.Anchor.Owner",   rules.Owner);
			rules.Team    = ReadTriState(pINI, pType->ID, "Adjacent.Anchor.Team",    rules.Team);
			rules.Ally    = ReadTriState(pINI, pType->ID, "Adjacent.Anchor.Ally",    rules.Ally);
			rules.Enemy   = ReadTriState(pINI, pType->ID, "Adjacent.Anchor.Enemy",   rules.Enemy);
			rules.Neutral = ReadTriState(pINI, pType->ID, "Adjacent.Anchor.Neutral", rules.Neutral);

			if (!rules.AnySet())
			{
				AnchorMap.erase(pType);
			}
			else if (before.Owner != rules.Owner || before.Team != rules.Team
				|| before.Ally != rules.Ally || before.Enemy != rules.Enemy
				|| before.Neutral != rules.Neutral)
			{
				Debug::Log("[BQExt] Adjacent.Anchor %s: owner=%d team=%d"
					" ally=%d enemy=%d neutral=%d  (-1 = vanilla)\n",
					pType->ID, rules.Owner, rules.Team, rules.Ally,
					rules.Enemy, rules.Neutral);
			}
		}

		// The ANCHOR side. Same tri-state and erase-if-unset discipline.
		{
			auto& f = AnchorForMap[pType];
			AnchorRules const before = f;

			f.Owner   = ReadTriState(pINI, pType->ID, "Anchor.ForOwner",   f.Owner);
			f.Team    = ReadTriState(pINI, pType->ID, "Anchor.ForTeam",    f.Team);
			f.Ally    = ReadTriState(pINI, pType->ID, "Anchor.ForAlly",    f.Ally);
			f.Enemy   = ReadTriState(pINI, pType->ID, "Anchor.ForEnemy",   f.Enemy);
			f.Neutral = ReadTriState(pINI, pType->ID, "Anchor.ForNeutral", f.Neutral);

			if (!f.AnySet())
			{
				AnchorForMap.erase(pType);
			}
			else if (before.Owner != f.Owner || before.Team != f.Team
				|| before.Ally != f.Ally || before.Enemy != f.Enemy
				|| before.Neutral != f.Neutral)
			{
				Debug::Log("[BQExt] Anchor.For %s: owner=%d team=%d ally=%d"
					" enemy=%d neutral=%d  (-1 = vanilla;"
					" BaseNormal=%d EligibileForAlly=%d)\n",
					pType->ID, f.Owner, f.Team, f.Ally, f.Enemy, f.Neutral,
					pType->BaseNormal ? 1 : 0,
					pType->EligibileForAllyBuilding ? 1 : 0);
			}
		}

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

HouseClass* BuildOffAnyBuilding::HouseByIndex(int idx)
{
	// DEFINE_REFERENCE gives a reference to the vector ITSELF, not a pointer --
	// `.` not `->`. Same trap BuildingTypeClass::Array sprang earlier in this
	// project (C2232/C2819 here, C2100 there).
	if (idx < 0 || idx >= HouseClass::Array.Count)
		return nullptr;

	return HouseClass::Array.Items[idx];
}

int BuildOffAnyBuilding::AnchorRuleFor(
	BuildingTypeClass* pType, AnchorScope scope)
{
	if (!pType)
		return -1;

	auto const it = AnchorMap.find(pType);

	if (it == AnchorMap.end())
		return -1;

	switch (scope)
	{
	case AnchorScope::Owner:   return it->second.Owner;
	case AnchorScope::Team:    return it->second.Team;
	case AnchorScope::Ally:    return it->second.Ally;
	case AnchorScope::Enemy:   return it->second.Enemy;
	case AnchorScope::Neutral: return it->second.Neutral;
	}

	return -1;
}

int BuildOffAnyBuilding::AnchorVerdict(
	BuildingTypeClass* pPlacing, HouseClass* pAsking, HouseClass* pCellOwner)
{
	if (!Enabled || !pPlacing || !pAsking || !pCellOwner)
		return -1;

	if (AnchorMap.find(pPlacing) == AnchorMap.end())
		return -1;

	auto const scope = ClassifyScope(pAsking, pCellOwner);

	int rule = AnchorRuleFor(pPlacing, scope);

	// A mutual ally is also an ally. If Team is unset, fall back to the broader
	// Ally rule rather than treating the narrower scope's silence as vanilla --
	// otherwise `Ally=yes` would mysteriously fail for your closest allies.
	if (rule < 0 && scope == AnchorScope::Team)
		rule = AnchorRuleFor(pPlacing, AnchorScope::Ally);

	// Logged on the ACCEPT side only; refusals of our own buildings are logged
	// by ForbidsOwnAnchor, and an unset rule is a no-op not worth a line.
	if (rule > 0 && (++AnchorAccepts == 1 || AnchorAccepts % 500 == 0))
	{
		static char const* const names[] =
			{ "Owner", "Team", "Ally", "Enemy", "Neutral" };

		Debug::Log("[BQExt] Adjacent.Anchor ACCEPT #%d placing=%s ->"
			" anchored on a %s house's building\n",
			AnchorAccepts, pPlacing->ID, names[static_cast<int>(scope)]);
	}

	return rule;
}

BuildingTypeClass* BuildOffAnyBuilding::PlacingTypeNow()
{
	return PlacingType;
}

bool BuildOffAnyBuilding::ForbidsOwnAnchor(BuildingClass* pCellBuilding)
{
	if (!Enabled || !PlacingType || !pCellBuilding)
		return false;

	// Reaching 0x4A8FE6 already proves this building belongs to the asking
	// house, so no ownership test is needed here -- the owner comparison is the
	// branch immediately above that address.
	if (AnchorRuleFor(PlacingType, AnchorScope::Owner) != 0)
		return false;

	if (++AnchorRefusals == 1 || AnchorRefusals % 500 == 0)
	{
		Debug::Log("[BQExt] Adjacent.Anchor REFUSE-OWN #%d placing=%s"
			" -> rejected own %s as an anchor\n",
			AnchorRefusals, PlacingType->ID,
			pCellBuilding->Type ? pCellBuilding->Type->ID : "(?)");
	}

	return true;
}

BuildOffAnyBuilding::AnchorScope BuildOffAnyBuilding::ClassifyScope(
	HouseClass* pAsking, HouseClass* pCellOwner)
{
	// Order matters: the scopes overlap, so the most specific wins. Neutral is
	// tested before Ally/Enemy because a passive civilian house reads as
	// "not allied" and would otherwise be judged an enemy.
	if (pCellOwner == pAsking)
		return AnchorScope::Owner;

	if (pCellOwner->IsNeutral())
		return AnchorScope::Neutral;

	if (pAsking->IsMutualAlly(pCellOwner))
		return AnchorScope::Team;

	if (pAsking->IsAlliedWith(pCellOwner))
		return AnchorScope::Ally;

	return AnchorScope::Enemy;
}

static int AnchorForRule(
	BuildingTypeClass* pType, BuildOffAnyBuilding::AnchorScope scope)
{
	using Scope = BuildOffAnyBuilding::AnchorScope;

	auto const it = AnchorForMap.find(pType);

	if (it == AnchorForMap.end())
		return -1;

	switch (scope)
	{
	case Scope::Owner:   return it->second.Owner;
	case Scope::Team:    return it->second.Team;
	case Scope::Ally:    return it->second.Ally;
	case Scope::Enemy:   return it->second.Enemy;
	case Scope::Neutral: return it->second.Neutral;
	}

	return -1;
}

bool BuildOffAnyBuilding::AnchorAllowsScope(
	BuildingTypeClass* pAnchorType, AnchorScope scope)
{
	if (!pAnchorType)
		return false;

	int const explicitRule = AnchorForRule(pAnchorType, scope);

	if (explicitRule >= 0)
		return explicitRule > 0;

	// Unset -> the VANILLA opinion for this scope, which is not the same key in
	// every case. Owner is BaseNormal. Allies have their own dedicated field.
	// Enemy and Neutral have no vanilla key at all, so they fall back to
	// BaseNormal as the nearest expression of "is this base-extending".
	switch (scope)
	{
	case AnchorScope::Team:
	case AnchorScope::Ally:
		return pAnchorType->EligibileForAllyBuilding;

	case AnchorScope::Owner:
	case AnchorScope::Enemy:
	case AnchorScope::Neutral:
	default:
		return pAnchorType->BaseNormal;
	}
}

int BuildOffAnyBuilding::AnchorForOwnerRule(BuildingClass* pCellBuilding)
{
	if (!Enabled || !pCellBuilding || !pCellBuilding->Type)
		return -1;

	return AnchorForRule(pCellBuilding->Type, AnchorScope::Owner);
}
