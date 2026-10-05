#pragma once

// Include order matters here and is copied from AlwaysAvailable.h, which
// compiles. BuildingClass.h pulls in Helpers/Cast.h, whose generic_cast needs
// FootClass COMPLETE -- including BuildingClass.h first yields a wall of
// C2139/C2338 inside Cast.h that names FootClass and never mentions this file.
#include <TechnoTypeClass.h>
#include <HouseClass.h>
#include <FootClass.h>
#include <BuildingTypeClass.h>
#include <BuildingClass.h>

#include <set>

// `BuildOffAnyBuilding=yes` — let a BuildingType anchor its placement on ANY
// building its owner has, instead of only on `BaseNormal=yes` ones.
//
// ---------------------------------------------------------------------------
// WHY THIS EXISTS, AND WHY IT IS NOT TIED TO AlwaysAvailable
//
// The modder asked for it after an AlwaysAvailable test, but deliberately
// scoped it wider: "perhaps that would be a good tag even for buildings that
// require factories". So it is its own tag with no dependency on
// AlwaysAvailable -- a mod with a perfectly normal ConYard can use it to let a
// wall, a turret or an outpost extend from things that are not base nodes.
//
// It pairs naturally with AlwaysAvailable, where it closes a real gap: with no
// ConYard your only surviving structure may well be `BaseNormal=no` (a pillbox,
// a wall), and then NOTHING can be placed at all. Confirmed in game
// 2026-10-02 -- with a lone GAPILL owned, the cameo was live and production
// ran, but placement was impossible.
//
// ---------------------------------------------------------------------------
// THE MECHANISM (verified by objdump, see encyclopedia
// Building-Placement-Proximity.md)
//
// `DisplayClass::PassesProximityCheck` @ 0x4A8F20 scans the foundation grown by
// `Adjacent + 1` cells. For each cell holding a building:
//
//   4a8fd7:  mov   ecx,[esi+0x21c]           ; pCellBuilding->Owner
//   4a8fe1:  cmp   [ecx+0x30],eax            ; Owner->ArrayIndex == houseIndex?
//   4a8fe4:  jne   0x4a8ffa                  ; not ours -> the ally path
//   4a8fe6:  mov   edx,[esi+0x520]           ; pCellBuilding->Type
//   4a8fec:  cmp   BYTE PTR [edx+0x154f],0x0 ; Type->BaseNormal
//   4a8ff3:  je    0x4a8ffa                  ; BaseNormal=no -> NOT an anchor
//   4a8ff5:  mov   BYTE PTR [esp+0x3c],0x1   ; ACCEPT
//
// Reaching 0x4A8FE6 already proves the building is OURS -- the owner test is
// the branch just above. So all this feature has to do is skip the BaseNormal
// test and fall into the accept.
//
// ---------------------------------------------------------------------------
// ⚠ WHY THE PLACED TYPE HAS TO BE CAPTURED AT THE ENTRY
//
// At 0x4A8FE6 the type being PLACED is no longer in any register: ESI held it
// at 0x4A8F3E but is reassigned at 0x4A8F44 and is a BuildingClass* by the
// loop. Phobos keeps its own copy in a file-global for exactly this reason.
//
// We cannot share Phobos' copy, and we cannot hook 0x4A8F3E alongside it,
// because **Phobos returns non-zero there** (`return SkipGameCode`) and a
// non-zero return aborts the rest of the Syringe chain. Phobos is injected
// before us, so a handler at that address would never run.
//
// Hence the capture at the function ENTRY 0x4A8F20, which nothing else
// occupies. Stolen bytes `55 6a 00 8b ce` (push ebp / push 0 / mov ecx,esi) --
// exactly 5, whole instructions, no absolute operands.
//
// ⚠ REENTRANCY. Phobos calls PassesProximityCheck RECURSIVELY from its own
// 0x4A8F3E handler (the Adjacent.Disallowed.Prohibit path). The nested call
// passes the SAME pType, so a plain last-writer-wins global is correct here --
// but it is correct by luck, not by design, so it is asserted rather than
// assumed: the entry hook only ever writes, never clears.

class BuildOffAnyBuilding
{
public:
	// [SOMEBUILDING] BuildOffAnyBuilding=yes
	static std::set<BuildingTypeClass*> Types;

	// [BuildQueueExt] BuildOffAnyBuilding.Enabled=yes — master switch, off by
	// default so the behaviour reverts with one INI key and no redeploy.
	static bool Enabled;

	static void ReadGlobalConfig(CCINIClass* pINI);
	static void ReadTypeConfig(CCINIClass* pINI);

	// Called from the 0x4A8F20 entry hook. See the reentrancy note above.
	static void CaptureType(BuildingTypeClass* pType);

	// Called from the 0x4A8FE6 hook. True => skip the BaseNormal test and
	// accept this owned building as an anchor.
	static bool ShouldIgnoreBaseNormal(BuildingClass* pCellBuilding);

	// ===================================================================
	// `Adjacent.NotRequired=yes` — a TAMED PlaceAnywhere.
	//
	// The modder's framing: vanilla `PlaceAnywhere` "really does allow things to
	// be placed anywhere ... on top of trees, units, cliffs, other buildings",
	// which is why it is niche. They asked whether a version could respect the
	// other placement rules.
	//
	// It can, because adjacency is isolated: `DisplayClass::PassesProximityCheck`
	// (0x4A8EB0) answers ONLY "is this close enough to something I own". Terrain,
	// cliffs, occupancy, foundation and tiberium checks all live outside it, in
	// the rest of the placement validator. Forcing just this function to pass
	// therefore drops the adjacency requirement and nothing else — so you can
	// build in the middle of nowhere, but still not on a cliff or on a unit.
	//
	// ⚠ IT ALSO BYPASSES Phobos' `Adjacent.Allowed=` / `Adjacent.Disallowed=`
	// for the tagged type, because those are evaluated inside this same function
	// as refinements of the same requirement. That is consistent -- the tag says
	// there is no adjacency requirement at all, so there is nothing left for
	// those keys to refine -- but it is a real interaction worth knowing before
	// combining them.
	static std::set<BuildingTypeClass*> NoProximityTypes;

	// Takes ObjectTypeClass* because that is the function's declared parameter
	// type. Used for pointer identity only -- never dereferenced -- so a
	// non-BuildingType simply fails to match, exactly like IdentifyType's
	// discipline in AlwaysAvailable.
	static bool SkipsProximityCheck(void* pType);

	// ===================================================================
	// `Adjacent.Anchor.<scope>=` — WHOSE buildings may anchor this one.
	//
	//   Adjacent.Anchor.Owner=no     ; DEFAULT YES -- forbid anchoring on your
	//                                ; OWN structures (modder's explicit ask)
	//   Adjacent.Anchor.Team=yes     ; mutual allies
	//   Adjacent.Anchor.Ally=yes     ; allied (one-way, as the engine sees it)
	//   Adjacent.Anchor.Enemy=yes
	//   Adjacent.Anchor.Neutral=yes  ; MultiplayPassive -- the civilian/special
	//                                ; house, which is why there is no separate
	//                                ; `Special` key
	//
	// Scope vocabulary is PrerequisiteExt's `HouseScope` (Owner/Ally/Team/Enemy/
	// Neutral) resolved the SAME way, so one name means one thing across the
	// modder's projects: Team is `IsMutualAlly`, Ally is `IsAlliedWith`, Neutral
	// is `IsNeutral()` i.e. `Type->MultiplayPassive`.
	//
	// ⚠ TRI-STATE, NOT BOOLEAN. Unset must mean "vanilla", not "no", or adding
	// this feature would silently forbid allied anchoring for every building in
	// the game. An unset key leaves the engine's own decision untouched.
	//
	// SEATS (both already mapped, see Building-Placement-Proximity.md):
	//   0x4A8FE6  the OWN-building branch -> Owner=no returns the loop-continue
	//             target 0x4A902C ("skip this building"), which is also what
	//             Phobos uses there.
	//   0x4A8FFA  the NOT-OURS branch, 6 bytes (`mov dl,[0xA8B264]`), free --
	//             Antares' 5-byte stamp at 0x4A8FF5 ends at 0x4A8FF9.
	//
	// ⚠ 0x4A8FFA IS ALSO REACHED BY FALL-THROUGH after an own building is
	// ACCEPTED at 0x4A8FF5, not only by the `jne`/`je` skips. So the handler
	// there must re-test ownership and bail for our own buildings, or it would
	// re-judge them under the ally rules it has no business applying.
	enum class AnchorScope { Owner, Team, Ally, Enemy, Neutral };

	// -1 unset (vanilla), 0 forbidden, 1 allowed.
	static int AnchorRuleFor(BuildingTypeClass* pType, AnchorScope scope);

	// Classifies pCellOwner relative to the asking house and returns that
	// scope's rule. Returns -1 when nothing is configured.
	static int AnchorVerdict(BuildingTypeClass* pPlacing,
		HouseClass* pAsking, HouseClass* pCellOwner);

	static HouseClass* HouseByIndex(int idx);

	// The type captured at the function entry (0x4A8F20). Exposed because the
	// anchor-scope hook at 0x4A8FFA needs it and, like 0x4A8FE6, cannot read it
	// from any register by that point.
	static BuildingTypeClass* PlacingTypeNow();

	// Adjacent.Anchor.Owner=no for the type currently being placed, with the
	// cell building confirmed to belong to the asking house.
	static bool ForbidsOwnAnchor(BuildingClass* pCellBuilding);

	// ===================================================================
	// `Anchor.For<scope>=` — the OTHER SIDE of the same permission.
	//
	// Anchoring has TWO opinions, and vanilla already expresses both:
	//
	//   the PLACED type's      "I may anchor on <scope> buildings"
	//                          -> our Adjacent.Anchor.<scope>
	//   the ANCHOR type's      "<scope> may anchor on me"
	//                          -> vanilla BaseNormal (owner) and
	//                             EligibileForAllyBuilding (allies)
	//
	// The first cut only implemented the placed side, so an accept bypassed the
	// anchor's opinion entirely -- which is exactly what the modder caught:
	// `Adjacent.Anchor.Neutral=yes` anchored on civilian buildings INCLUDING
	// `BaseNormal=no` ones, because nothing consulted the anchor.
	//
	// So this generalises vanilla's two keys to all five scopes:
	//
	//   [SOMEANCHOR]
	//   Anchor.ForOwner=yes    ; generalises BaseNormal
	//   Anchor.ForAlly=yes     ; generalises EligibileForAllyBuilding
	//   Anchor.ForTeam=yes
	//   Anchor.ForEnemy=no
	//   Anchor.ForNeutral=no
	//
	// DEFAULTS PRESERVE VANILLA, which is why each one differs:
	//   Owner        -> BaseNormal
	//   Team, Ally   -> EligibileForAllyBuilding
	//   Enemy, Neutral -> BaseNormal, because vanilla has no dedicated key for
	//                   these and BaseNormal is its nearest notion of "is this a
	//                   base-extending structure at all"
	//
	// ⚠ Both sides must agree. The placed type opting in is necessary but not
	// sufficient -- a defence with BaseNormal=no still will not anchor anything
	// unless its own Anchor.For<scope> says so. That keeps the modder's stated
	// vanilla model intact: "BaseNormal=no ... means the owner cannot build off
	// it, but they may build around it by building off a different nearby
	// structure".
	static bool AnchorAllowsScope(BuildingTypeClass* pAnchorType,
		AnchorScope scope);

	// Exposed so the hook can pass the same scope to both sides rather than
	// classifying twice and risking the two disagreeing.
	static AnchorScope ClassifyScope(HouseClass* pAsking,
		HouseClass* pCellOwner);

	// Tri-state `Anchor.ForOwner` for the own branch: -1 unset (use BaseNormal,
	// i.e. vanilla), 0 forbidden, 1 allowed.
	static int AnchorForOwnerRule(BuildingClass* pCellBuilding);
};
