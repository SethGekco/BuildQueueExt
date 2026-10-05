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

	// ⚠ THE BOOLEAN SCOPE FAMILIES THAT USED TO LIVE HERE ARE GONE.
	// Adjacent.Anchor.<scope>, Anchor.For<scope>, Repel.<scope> and
	// Repel.For<scope> reached 20 keys expressing one idea badly, and the
	// modder replaced them with the class system in AdjacentClasses.h. That
	// design subsumes the two-sided booleans outright: `AdjacentType=` IS the
	// other building's half of the permission, so Anchor.For / Repel.For had
	// nothing left to say.
	//
	// The two tags below survive because they are ORTHOGONAL to classes:
	// BuildOffAnyBuilding is a blunt "ignore BaseNormal entirely" and
	// Adjacent.NotRequired removes the requirement rather than scoping it.
};
