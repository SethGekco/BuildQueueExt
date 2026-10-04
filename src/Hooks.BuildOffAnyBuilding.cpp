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
	// 0x4A902C is the loop-continue -- "skip this building" -- and is what
	// Phobos uses from its own handler in this function.
	enum { AcceptAnchor = 0x4A8FF5, SkipBuilding = 0x4A902C };

	GET(BuildingClass*, pCellBuilding, ESI);

	// Adjacent.Anchor.Owner=no. Checked FIRST: a forbid must beat
	// BuildOffAnyBuilding's accept, or the two tags would contradict each other
	// on a BaseNormal=no building owned by the asking house.
	if (BuildOffAnyBuilding::ForbidsOwnAnchor(pCellBuilding))
		return SkipBuilding;

	if (BuildOffAnyBuilding::ShouldIgnoreBaseNormal(pCellBuilding))
		return AcceptAnchor;

	return 0;
}

// DisplayClass::PassesProximityCheck TRUE ENTRY @ 0x4A8EB0 — `Adjacent.NotRequired`.
//
// ⚠ 0x4A8EB0 is the real function entry (YRpp DisplayClass.h:26). 0x4A8F20,
// which the BuildOffAnyBuilding capture hook above uses, is already PAST the
// prologue -- by then `sub esp,0x20` has run and esi/edi are pushed, so the
// frame there is NOT the caller's and an early return would corrupt the stack.
//
// STOLEN BYTES `a1 4c 3d a8 00` = `mov eax,ds:0xa83d4c`. Exactly 5, ONE whole
// instruction, absolute operand so position-independent and safe to replay.
//
// THE EARLY RETURN IS VALID ONLY HERE. At this instruction nothing has been
// pushed and esp has not moved, so the frame is still the caller's:
//
//   [ESP+0x00] = return address
//   [ESP+0x04] = ObjectTypeClass*  the type being placed
//   [ESP+0x08] = int houseArrayIndex      (the engine reads this at 0x4A8EB5)
//   [ESP+0x0C] = CellStruct* foundationData
//   [ESP+0x10] = CellStruct* currentPosition
//
// so jumping to the bare `ret 0x10` at 0x4A9059 pops the return address and
// discards exactly the four arguments. Jumping to the OTHER true-exit at
// 0x4A905C would be wrong: it begins `pop edi` / `pop esi`, unwinding pushes
// that have not happened yet.
//
// SEAT IS UNCONTENDED, unlike the rest of this function -- Phobos holds
// 0x4A8F3E and 0x4A8FD7, Antares 0x4A8FF5, and Kratos 0x4A904E with a 5-byte
// stamp that also swallows 0x4A9052, which is additionally a Phobos jump target.
// The entry avoids all four.

DEFINE_HOOK(0x4A8EB0, BQExt_DisplayClass_PassesProximityCheck_TrueEntry, 0x5)
{
	enum { ReturnTrue = 0x4A9059 };   // bare `ret 0x10`

	GET_STACK(void*, pType, 0x4);

	if (BuildOffAnyBuilding::SkipsProximityCheck(pType))
	{
		R->EAX(1);                    // AL is the bool result
		return ReturnTrue;
	}

	return 0;
}

// The NOT-OURS anchor branch @ 0x4A8FFA — `Adjacent.Anchor.<scope>=`.
//
//   4a8fe4:  jne 0x4a8ffa                  ; cell building is not ours
//   4a8ff5:  mov BYTE PTR [esp+0x3c],0x1   ; ...or ours and accepted
//   4a8ffa:  mov dl,BYTE PTR ds:0xa8b264   ; <- HERE: ally-build global
//   4a9004:  mov edx,ds:0xa8022c           ; HouseClass::Array
//   4a900a:  mov eax,[edx+eax*4]           ; the asking house, by index
//   4a900e:  call 0x4f9a50                 ; IsAlliedWith
//   4a901d:  mov al,[ecx+0x1550]           ; EligibileForAllyBuilding
//   4a9027:  mov BYTE PTR [esp+0x3c],0x1   ; ACCEPT
//
// STOLEN BYTES `8a 15 64 b2 a8 00` = 6, ONE whole instruction, absolute operand.
// Free: Antares' 5-byte stamp at 0x4A8FF5 ends at 0x4A8FF9.
//
// ⚠ THIS ADDRESS IS ALSO REACHED BY FALL-THROUGH from the own-building ACCEPT
// at 0x4A8FF5, not only by the branches. So ownership is re-tested here and our
// own buildings are handed straight back to vanilla -- judging them under the
// ally rules would re-decide a case 0x4A8FE6 has already settled.
//
//   ESI       = BuildingClass*   the candidate anchor in this cell
//   [ESP+..]  = houseArrayIndex  via STACK_OFFSET(0x30, 0x8), same frame as
//               0x4A8FD7 since nothing is pushed in between

DEFINE_HOOK(0x4A8FFA, BQExt_DisplayClass_PassesProximityCheck_AnchorScope, 0x6)
{
	enum { AcceptAnchor = 0x4A8FF5, SkipBuilding = 0x4A902C };

	GET(BuildingClass*, pCellBuilding, ESI);
	GET_STACK(int const, houseArrayIndex, STACK_OFFSET(0x30, 0x8));

	if (!pCellBuilding)
		return 0;

	auto const pCellOwner = pCellBuilding->Owner;
	auto const pAsking = BuildOffAnyBuilding::HouseByIndex(houseArrayIndex);

	if (!pCellOwner || !pAsking)
		return 0;

	// Our own buildings were already decided at 0x4A8FE6. See the fall-through
	// warning above.
	if (pCellOwner == pAsking)
		return 0;

	int const verdict = BuildOffAnyBuilding::AnchorVerdict(
		BuildOffAnyBuilding::PlacingTypeNow(), pAsking, pCellOwner);

	if (verdict < 0)
		return 0;        // unset -> leave the engine's own ally logic alone

	return verdict > 0 ? AcceptAnchor : SkipBuilding;
}
