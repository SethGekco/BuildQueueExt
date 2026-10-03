#include "BuildOffAnyBuilding.h"

#include <Utilities/Macro.h>

// DisplayClass::PassesProximityCheck ENTRY @ 0x4A8F20 — capture the type being
// placed, because by the time the anchor loop runs it is in no register.
//
// STOLEN BYTES `55 6a 00 8b ce` = push ebp / push 0x0 / mov ecx,esi. Exactly 5,
// three whole instructions, no absolute operands, safe to replay.
//
// NOT 0x4A8F3E, where the type is also in ESI: Phobos occupies that address and
// returns NON-ZERO (`return SkipGameCode`), which aborts the rest of the Syringe
// chain. Phobos is injected before us, so a handler there would never run.
//
//   ESI = BuildingTypeClass*   the type being placed

DEFINE_HOOK(0x4A8F20, BQExt_DisplayClass_PassesProximityCheck_Entry, 0x5)
{
	GET(BuildingTypeClass*, pType, ESI);

	BuildOffAnyBuilding::CaptureType(pType);

	return 0;
}

// The BaseNormal anchor test @ 0x4A8FE6.
//
//   4a8fe4:  jne 0x4a8ffa                  ; <- not-ours already branched away
//   4a8fe6:  mov edx,[esi+0x520]           ; pCellBuilding->Type      <- HERE
//   4a8fec:  cmp BYTE PTR [edx+0x154f],0x0 ; Type->BaseNormal
//   4a8ff3:  je  0x4a8ffa                  ; BaseNormal=no -> not an anchor
//   4a8ff5:  mov BYTE PTR [esp+0x3c],0x1   ; ACCEPT
//
// Reaching this instruction already proves the building belongs to the asking
// house — the owner comparison is the branch immediately above — so the only
// thing to do is skip the BaseNormal test and land on the accept.
//
// STOLEN BYTES `8b 96 20 05 00 00` = `mov edx,[esi+0x520]`, 6 bytes, ONE whole
// instruction. Size 0x6 rather than 0x5 because a 5-byte stamp would split it.
//
// ⚠ Jumping to 0x4A8FF5 enters ANTARES' hook stamp at that address. That is
// intended, not a hazard: execution flows through Syringe's stub, Antares'
// handler gets its say (it can still refuse this anchor), and the stolen bytes
// are replayed. Skipping past Antares instead would silently cancel whatever
// its MapClass_CanBuildingTypeBePlacedHere_Ignore handler exists to do.
//
//   ESI = BuildingClass*   the candidate anchor in this cell

DEFINE_HOOK(0x4A8FE6, BQExt_DisplayClass_PassesProximityCheck_BaseNormal, 0x6)
{
	enum { AcceptAnchor = 0x4A8FF5 };

	GET(BuildingClass*, pCellBuilding, ESI);

	if (BuildOffAnyBuilding::ShouldIgnoreBaseNormal(pCellBuilding))
		return AcceptAnchor;

	return 0;
}
