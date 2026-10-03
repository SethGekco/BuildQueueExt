#pragma once

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
};
