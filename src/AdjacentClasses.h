#pragma once

// Include order copied from AlwaysAvailable.h, which compiles: BuildingClass.h
// pulls in Helpers/Cast.h, whose generic_cast needs FootClass COMPLETE.
#include <TechnoTypeClass.h>
#include <HouseClass.h>
#include <FootClass.h>
#include <BuildingTypeClass.h>
#include <BuildingClass.h>

#include <map>
#include <string>
#include <vector>

// ===========================================================================
// ADJACENCY CLASSES — asks #16/#17. Replaces the boolean tag families.
//
// The modder's design, and it is a better one than what it replaces. The
// per-scope boolean families had reached 5 scopes x 2 sides x 2 families = 20
// keys, all expressing one idea badly. This is four key patterns plus a class
// registry:
//
//   [AdjacentTypes]          ; the registry, an indexed list like other YR lists
//   0=Base
//   1=Factory
//   2=ConstructionYard
//
//   [GACNST] AdjacentType=ConstructionYard,Factory   ; membership, IN PRIORITY ORDER
//   [NACNST] AdjacentType=Factory,ConstructionYard
//
//   [ATESLA] Repel.Enemy.Type=ConstructionYard,Factory
//            Repel.Enemy.Range=12,5
//
//   [GAPOWR] Adjacent.Owner.Type=Base
//            Adjacent.Owner.Range=9
//
// ---------------------------------------------------------------------------
// ⚠ THE PRIORITY LIVES ON THE OTHER BUILDING, NOT ON THE RULE
//
// Given one rule, two buildings in the same classes can resolve to different
// ranges. For each candidate we walk ITS OWN `AdjacentType=` list in order and
// take the first entry the rule also names. From the identical ATESLA rule
// above: GACNST resolves as ConstructionYard -> 12, NACNST as Factory -> 5,
// because that is the order each one lists for itself. So every building
// declares its own primary identity.
//
// Entries in a candidate's list that the rule does not mention are SKIPPED, not
// treated as a miss -- a building listing `Base,Factory` against a rule naming
// only `Factory` resolves as Factory. (The modder's example does not cover
// this; it is the only reading that makes partial rules usable.)
//
// ---------------------------------------------------------------------------
// WHY THIS SUBSUMES THE TWO-SIDED BOOLEANS
//
// `AdjacentType=` **is** the other building's half of the permission: it
// declares what it is, and the placed type declares which classes it accepts.
// That is exactly what `Anchor.For<scope>` / `Repel.For<scope>` were doing by
// hand, so those are deleted rather than kept alongside. `BaseNormal` and
// `EligibileForAllyBuilding` remain the vanilla default for any scope with no
// rule.
//
// ---------------------------------------------------------------------------
// ⚠ RANGE IS CLAMPED FOR ANCHORS, FREE FOR REPEL — and the asymmetry is real
//
// The engine scans a fixed rectangle of `Adjacent + 1` cells
// (PassesProximityCheck 0x4A8F20) and we cannot widen it: the radius read at
// 0x4A8F3E is Phobos', and it returns non-zero so the address cannot be
// chained. So an `Adjacent.<scope>.Range` LARGER than the type's own
// `Adjacent` describes cells the engine never visits. Such ranges are clamped,
// with a log line, and widening means raising `Adjacent=` itself.
//
// Repel has no such limit, because it runs as our own scan at the function
// entry rather than inside the engine's loop. `Repel.<scope>.Range=12` works
// regardless of `Adjacent`.

class AdjacentClasses
{
public:
	// Shared with the rest of the project; same five names and the same
	// resolution as PrerequisiteExt's HouseScope, so one word means one thing
	// across the modder's projects.
	enum class Scope { Owner, Team, Ally, Enemy, Neutral, Count };

	static void ReadGlobalConfig(CCINIClass* pINI);
	static void ReadTypeConfig(CCINIClass* pINI);

	static Scope Classify(HouseClass* pAsking, HouseClass* pOther);
	static HouseClass* HouseByIndex(int idx);
	static char const* ScopeName(Scope scope);

	// Does the placed type define an anchor rule for this scope at all? When it
	// does, the rule REPLACES vanilla's BaseNormal / EligibileForAllyBuilding
	// decision for that scope; when it does not, vanilla stands untouched.
	static bool HasAnchorRule(BuildingTypeClass* pPlacing, Scope scope);

	// Range in cells if this candidate qualifies as an anchor under the placed
	// type's rule for this scope, else -1. Caller still enforces the distance,
	// because only the caller knows where the candidate is.
	static int AnchorRange(BuildingTypeClass* pPlacing, Scope scope,
		BuildingTypeClass* pCandidate);

	// Range in cells within which this candidate repels the placed type, else
	// -1. Both halves are in one lookup: the placed type must name a class the
	// candidate belongs to.
	static int RepelRange(BuildingTypeClass* pPlacing, Scope scope,
		BuildingTypeClass* pCandidate);

	// Largest repel range configured for this type across all scopes, or 0.
	// Lets the entry scan size its window once instead of per candidate, and
	// skip entirely for the overwhelmingly common untagged case.
	static int MaxRepelRange(BuildingTypeClass* pPlacing);

	static bool AnyRepelConfigured();

private:
	// classIndex -> range, in the order the rule listed them. A vector rather
	// than a map because these are tiny and the resolution walks the
	// CANDIDATE's order, not this one.
	using RuleList = std::vector<std::pair<int, int>>;

	struct TypeRules
	{
		std::vector<int> Membership;              // class indices, priority order
		RuleList Anchor[int(Scope::Count)];
		RuleList Repel[int(Scope::Count)];
		int MaxRepel = 0;
		bool HasAnchor[int(Scope::Count)] = {};
	};

	static std::map<std::string, int> ClassIds;
	static std::vector<std::string> ClassNames;
	static std::map<BuildingTypeClass*, TypeRules> Rules;
	static bool RepelConfigured;

	static int Resolve(TypeRules const* pPlacingRules, RuleList const& rule,
		BuildingTypeClass* pCandidate);
};
