#include "AlwaysAvailable.h"

#include <CCINIClass.h>
#include <BuildingClass.h>
#include <BuildingTypeClass.h>
#include <InfantryTypeClass.h>
#include <UnitTypeClass.h>
#include <AircraftTypeClass.h>
#include <MouseClass.h>
#include <RulesClass.h>
#include <HouseTypeClass.h>
#include <Utilities/Debug.h>

std::set<TechnoTypeClass*> AlwaysAvailable::Types;
std::set<TechnoTypeClass*> AlwaysAvailable::SpectatorTypes;
static int SpectatorSuppressed = 0;
static int DisableLowered = 0;
static int DisableLimitKept = 0;
static int DisableTaggedCalls = 0;
static int CanBuildPromotes = 0;
bool AlwaysAvailable::ProbeEnabled = false;
bool AlwaysAvailable::Enabled = false;
bool AlwaysAvailable::PushCameoEnabled = false;
static int PushFrame = 0;
static int PushAttempts = 0;
static int PushAccepted = 0;
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
	PushCameoEnabled = pINI->ReadBool(
		"BuildQueueExt", "AlwaysAvailable.PushCameo", PushCameoEnabled);
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

		// Read unconditionally rather than only for tagged types: the two keys
		// are independent INI reads, and making one depend on the other's
		// current state would make the result depend on pass order.
		bool const specWas = SpectatorTypes.find(pType) != SpectatorTypes.end();
		bool const specNow =
			pINI->ReadBool(pType->ID, "AlwaysAvailable.Spectators", specWas);

		if (specNow != specWas)
		{
			if (specNow)
				SpectatorTypes.insert(pType);
			else
				SpectatorTypes.erase(pType);

			Debug::Log("[BQExt] AlwaysAvailable.Spectators %s: %s\n",
				specNow ? "allowed" : "denied", pType->ID);
		}

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

bool AlwaysAvailable::AllowsSpectators(TechnoTypeClass* pType)
{
	return pType && SpectatorTypes.find(pType) != SpectatorTypes.end();
}

bool AlwaysAvailable::IsSpectating(HouseClass* pHouse)
{
	if (!pHouse)
		return false;

	// Defeated is included deliberately. The modder saw the cameo survive their
	// own game-over: once defeated you are, in effect, a spectator, and the
	// normal sidebar path would never have offered it.
	//
	// ⚠ IsInitiallyObserver() IS DELIBERATELY NOT USED. It is
	// `IsHumanPlayer && GetSpawnPosition() == -1`, and GetSpawnPosition scans
	// ScenarioClass::HouseIndices -- a MULTIPLAYER LOBBY array. In skirmish the
	// human player is not necessarily in it, so it returns -1 and the ordinary
	// player is misclassified as an observer. Including it silently disabled
	// the whole substitution path for the human on its first armed run: the
	// probe kept logging WOULD SUBSTITUTE while Resolve returned early every
	// time. IsObserver() is `this == Observer`, which is unambiguous.
	return pHouse->IsObserver() || pHouse->Defeated;
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

	// The ask is "can spectators BUILD it", so the gate belongs here as well as
	// on the cameo push. Without this, a defeated player whose cameo is still
	// on screen from before the suppression kicked in could still click it.
	//
	// LOGGED, not silent. The first version of this gate rejected the human
	// player every frame and said nothing, so the log showed WOULD SUBSTITUTE
	// forever with no SUBSTITUTE and no reason -- indistinguishable from the
	// hook not running. Every early return on this path now names itself.
	if (IsSpectating(pHouse) && !AllowsSpectators(pType))
	{
		if (++SpectatorSuppressed == 1 || SpectatorSuppressed % 500 == 0)
		{
			Debug::Log("[BQExt] AlwaysAvailable REFUSE-BUILD #%d %s --"
				" house is spectating (observer=%d defeated=%d)\n",
				SpectatorSuppressed, pType->ID,
				pHouse->IsObserver() ? 1 : 0,
				pHouse->Defeated ? 1 : 0);
		}

		return pVerdict;
	}

	// Owner=, TechLevel and Prerequisite= still apply. Without this a tagged
	// type is buildable by the wrong country, under-teched, with nothing built.
	if (!MeetsNormalRules(pHouse, pType))
		return pVerdict;

	auto const pStandIn = FindStandIn(pHouse);

	if (!pStandIn)
	{
		// A house with no buildings at all. Nothing to stand in, and inventing
		// something is exactly what this option exists to avoid.
		if (++NoStandIn == 1 || NoStandIn % 500 == 0)
		{
			Debug::Log("[BQExt] AlwaysAvailable NO-STAND-IN #%d %s --"
				" house owns no usable building\n", NoStandIn, pType->ID);
		}

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

bool AlwaysAvailable::HouseHasUsableFactory(
	HouseClass* pHouse, AbstractType produces)
{
	if (!pHouse)
		return false;

	for (auto const pBld : pHouse->Buildings)
	{
		if (!pBld || !pBld->IsAlive || pBld->InLimbo)
			continue;

		if (pBld->GetCurrentMission() == Mission::Selling
			|| pBld->QueuedMission == Mission::Selling)
		{
			continue;
		}

		auto const pType = pBld->Type;

		if (!pType || pType->Factory != produces)
			continue;

		// requirePower, deliberately. PrereqValidate calls HasFactory with
		// requirePower=true, so a powered-down factory reports Unpowered, not
		// Available. Skipping this check would make us answer "yes, there is a
		// factory" where Antares answers "no" -- two implementations of one
		// question, disagreeing exactly when the base is low on power.
		if (!pBld->HasPower || pBld->Deactivated)
			continue;

		return true;
	}

	return false;
}

// Does the house own at least one of the BuildingTypes in this list?
static bool OwnsAnyOf(HouseClass* pHouse, TypeList<int> const& list)
{
	for (auto i = 0; i < list.Count; ++i)
	{
		int const idx = list.Items[i];

		if (idx >= 0 && pHouse->ActiveBuildingTypes.GetItemCount(idx) > 0)
			return true;
	}

	return false;
}

bool AlwaysAvailable::MeetsNormalRules(HouseClass* pHouse, TechnoTypeClass* pType)
{
	if (!pHouse || !pType)
		return false;

	// --- Owner= -------------------------------------------------------------
	// InOwners takes the house-type BIT, not the index. Passing the index would
	// silently test the wrong country for every index above 0.
	if (auto const pHouseType = pHouse->Type)
	{
		if (!pType->InOwners(1u << pHouseType->ArrayIndex))
			return false;
	}

	// --- TechLevel ----------------------------------------------------------
	// -1 means "never buildable" in vanilla, so it is a refusal rather than a
	// trivially-satisfied low bar.
	if (pType->TechLevel < 0)
		return false;

	if (pHouse->TechLevel < pType->TechLevel)
		return false;

	// --- PrerequisiteOverride= ---------------------------------------------
	// Owning any of these satisfies prerequisites outright, so it is checked
	// before the main list rather than after.
	if (OwnsAnyOf(pHouse, pType->PrerequisiteOverride))
		return true;

	// --- Prerequisite= ------------------------------------------------------
	// Every entry must be satisfied. Negative entries are the six generic
	// groups, where owning ANY member of the group satisfies that one entry.
	// DEFINE_REFERENCE(RulesClass*, Instance, 0x8871E0) -- a reference TO a
	// pointer, not a function. `RulesClass::Instance()` does not compile.
	auto const pRules = RulesClass::Instance;

	for (auto i = 0; i < pType->Prerequisite.Count; ++i)
	{
		int const entry = pType->Prerequisite.Items[i];

		if (entry >= 0)
		{
			if (pHouse->ActiveBuildingTypes.GetItemCount(entry) <= 0)
				return false;

			continue;
		}

		if (!pRules)
			return false;

		// The negative encoding, straight from vanilla: -1 POWER, -2 FACTORY,
		// -3 BARRACKS, -4 RADAR, -5 TECH, -6 PROC. Anything outside that range
		// is something we do not understand, and refusing is the safe answer.
		TypeList<int> const* pGroup = nullptr;

		switch (entry)
		{
		case -1: pGroup = &pRules->PrerequisitePower;    break;
		case -2: pGroup = &pRules->PrerequisiteFactory;  break;
		case -3: pGroup = &pRules->PrerequisiteBarracks; break;
		case -4: pGroup = &pRules->PrerequisiteRadar;    break;
		case -5: pGroup = &pRules->PrerequisiteTech;     break;
		case -6: pGroup = &pRules->PrerequisiteProc;     break;
		default: return false;
		}

		if (!OwnsAnyOf(pHouse, *pGroup))
			return false;
	}

	return true;
}

int AlwaysAvailable::ResolveCanBuild(
	HouseClass* pHouse, TechnoTypeClass* pType, int incoming)
{
	// Unbuildable(0) ONLY. A -1 means greyed-for-a-reason (build limit, or
	// unpowered) and a 1 is already a yes; touching either would be us
	// overruling a decision we were not asked about.
	if (!Enabled || incoming != 0 || !pHouse || !pType)
		return incoming;

	if (!IsEnabledFor(pType))
		return incoming;

	if (IsSpectating(pHouse) && !AllowsSpectators(pType))
		return incoming;

	// The 0 has to be the FACTORY's fault. With a usable factory present the
	// verdict was refused for some other reason entirely, and promoting it
	// would grant something unrelated to this feature.
	if (HouseHasUsableFactory(pHouse, AbstractType::BuildingType))
		return incoming;

	if (!MeetsNormalRules(pHouse, pType))
		return incoming;

	if (BuildLimitReached(pHouse, pType))
		return incoming;

	if (++CanBuildPromotes == 1 || CanBuildPromotes % 2000 == 0)
	{
		Debug::Log("[BQExt] AlwaysAvailable PROMOTE-CANBUILD #%d %s --"
			" 0 -> 1 (no factory, rules met, limit ok)\n",
			CanBuildPromotes, pType->ID);
	}

	return 1;
}

bool AlwaysAvailable::BuildLimitReached(HouseClass* pHouse, TechnoTypeClass* pType)
{
	if (!pHouse || !pType)
		return false;

	int const limit = pType->BuildLimit;

	// Zero is "unlimited" in vanilla, and treating it as a limit of zero would
	// disable every untagged-limit building on the map.
	if (limit > 0)
		return pHouse->CountOwnedNow(pType) >= limit;

	if (limit < 0)
		return pHouse->CountOwnedEver(pType) >= -limit;

	return false;
}

void AlwaysAvailable::ProbeDisableCameo(
	HouseClass* pHouse, TechnoTypeClass* pType, bool disable)
{
	static bool announced = false;

	if (!announced)
	{
		announced = true;
		Debug::Log("[BQExt] AlwaysAvailable ShouldDisableCameo hook IS LIVE"
			" (first call: disable=%d type=%s)\n",
			disable ? 1 : 0, pType ? pType->ID : "(null)");
	}

	if (!pType || !IsEnabledFor(pType))
		return;

	// Tagged types only, and bounded: enough to see the steady state without
	// flooding a log that already runs to hundreds of thousands of lines.
	if (++DisableTaggedCalls > 20 && DisableTaggedCalls % 2000 != 0)
		return;

	// Adjacent is logged to settle the placement question, not the cameo one.
	// DisplayClass::PassesProximityCheck (0x4A8F20) reads Adjacent at 0x4A8F3E
	// and expands the scan rectangle by Adjacent+1 on every side
	// (`inc eax` at 0x4A8F48, then the lea/sub pairs through 0x4A8F74).
	// So an observed slack of exactly ONE cell means Adjacent read as ZERO at
	// runtime, whatever the INI says. This prints the value the engine holds.
	// Safe to cast: the IsEnabledFor gate above means the type came from
	// BuildingTypeClass::Array.
	auto const pBldType = static_cast<BuildingTypeClass*>(pType);

	Debug::Log("[BQExt] AlwaysAvailable disable-probe #%d %s:"
		" incoming=%d enabled=%d hasFactory=%d limitReached=%d"
		" spectating=%d allowsSpec=%d  (limit=%d ownedNow=%d"
		" ADJACENT=%d baseNormal=%d)\n",
		DisableTaggedCalls, pType->ID,
		disable ? 1 : 0,
		Enabled ? 1 : 0,
		HouseHasUsableFactory(pHouse, AbstractType::BuildingType) ? 1 : 0,
		BuildLimitReached(pHouse, pType) ? 1 : 0,
		IsSpectating(pHouse) ? 1 : 0,
		AllowsSpectators(pType) ? 1 : 0,
		pType->BuildLimit,
		pHouse ? pHouse->CountOwnedNow(pType) : -1,
		pBldType->Adjacent,
		pBldType->BaseNormal ? 1 : 0);
}

bool AlwaysAvailable::ResolveDisableCameo(
	HouseClass* pHouse, TechnoTypeClass* pType, bool disable)
{
	// Only ever lower, and only from an already-disabled state. If nothing
	// disabled the cameo there is nothing here to fix.
	if (!Enabled || !disable || !pHouse || !pType)
		return disable;

	if (!IsEnabledFor(pType))
		return disable;

	if (IsSpectating(pHouse) && !AllowsSpectators(pType))
		return disable;

	// Types holds BuildingTypes only, so the factory abstract is BuildingType.
	// If the house DOES have a usable factory then whatever disabled this cameo
	// was not the no-factory clause, and overriding it would be us silently
	// cancelling a decision that is none of our business.
	if (HouseHasUsableFactory(pHouse, AbstractType::BuildingType))
		return disable;

	// Same three rules as the substitution. A cameo we make clickable must be
	// one the house is genuinely entitled to.
	if (!MeetsNormalRules(pHouse, pType))
		return disable;

	// Never resurrect a type whose BuildLimit is genuinely spent -- that is the
	// other clause of ShouldDisableCameo, and it is a real game rule.
	if (BuildLimitReached(pHouse, pType))
	{
		if (++DisableLimitKept == 1 || DisableLimitKept % 500 == 0)
		{
			Debug::Log("[BQExt] AlwaysAvailable KEEP-DISABLED #%d %s --"
				" BuildLimit reached (limit=%d ownedNow=%d)\n",
				DisableLimitKept, pType->ID, pType->BuildLimit,
				pHouse->CountOwnedNow(pType));
		}

		return disable;
	}

	if (++DisableLowered == 1 || DisableLowered % 500 == 0)
	{
		Debug::Log("[BQExt] AlwaysAvailable ENABLE-CAMEO #%d %s --"
			" lowered ShouldDisableCameo (no usable factory, limit ok)"
			"  [kept-for-limit %d]\n",
			DisableLowered, pType->ID, DisableLimitKept);
	}

	return false;
}

void AlwaysAvailable::PushCameos()
{
	if (!PushCameoEnabled || Types.empty())
		return;

	// Throttled hard. AddCameo is a strip mutation, not a query, and this runs
	// on the post-loop seat -- once every 15 ticks is ample to observe whether
	// a cameo appears.
	if (++PushFrame % 15 != 0)
		return;

	auto const pHouse = HouseClass::CurrentPlayer;

	if (!pHouse)
		return;

	// Only when the normal path genuinely cannot produce the cameo. With a
	// usable ConYard, UpdateConstructionOptions populates the strip itself and
	// pushing would duplicate its work.
	if (HouseHasUsableFactory(pHouse, AbstractType::BuildingType))
		return;

	// Computed once per pass, not per type: it is a property of the house.
	bool const spectating = IsSpectating(pHouse);

	for (auto const pTechnoType : Types)
	{
		// Types is populated only from BuildingTypeClass::Array, so this is safe.
		auto const pType = static_cast<BuildingTypeClass*>(pTechnoType);

		// Checked per type inside the loop because MeetsNormalRules is
		// type-specific, unlike the house-wide spectator test above.
		if (!MeetsNormalRules(pHouse, pType))
			continue;

		if (spectating && !AllowsSpectators(pType))
		{
			// Note this does NOT remove a cameo pushed before the house was
			// defeated -- AddCameo has no inverse we use here. It stops the
			// push from re-asserting it, which is enough for the observer case
			// (never pushed at all) but leaves a defeated player's existing
			// cameo on screen until the strip is next rebuilt.
			if (++SpectatorSuppressed == 1 || SpectatorSuppressed % 500 == 0)
			{
				Debug::Log("[BQExt] AlwaysAvailable SUPPRESS #%d %s --"
					" house is spectating (observer=%d defeated=%d)\n",
					SpectatorSuppressed, pType->ID,
					pHouse->IsObserver() ? 1 : 0,
					pHouse->Defeated ? 1 : 0);
			}

			continue;
		}

		++PushAttempts;

		bool const added = MouseClass::Instance.AddCameo(
			AbstractType::BuildingType, pType->ArrayIndex);

		if (added)
			++PushAccepted;

		// First attempt and every 100th: enough to see whether AddCameo ever
		// accepts, without a per-tick stream.
		if (PushAttempts == 1 || PushAttempts % 100 == 0)
		{
			Debug::Log("[BQExt] AlwaysAvailable PUSH #%d %s idx=%d ->"
				" AddCameo returned %s  [accepted %d]\n",
				PushAttempts, pType->ID, pType->ArrayIndex,
				added ? "TRUE" : "false", PushAccepted);
		}
	}
}
