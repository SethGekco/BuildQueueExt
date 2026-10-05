#include "BuildOffAnyBuilding.h"
#include "AdjacentClasses.h"

#include <MapClass.h>
#include <CellClass.h>
#include <Utilities/Debug.h>

static int BQExt_Repels = 0;

static void BQExt_LogRepel(BuildingTypeClass* pPlacing,
	AdjacentClasses::Scope scope, BuildingTypeClass* pBlocker, int range)
{
	if (++BQExt_Repels == 1 || BQExt_Repels % 500 == 0)
	{
		Debug::Log("[BQExt] Repel #%d placing=%s REFUSED -- %s house's %s"
			" within %d cells\n",
			BQExt_Repels, pPlacing->ID, AdjacentClasses::ScopeName(scope),
			pBlocker->ID, range);
	}
}

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
	GET_STACK(int const, houseArrayIndex, STACK_OFFSET(0x30, 0x8));

	auto const pPlacing = BuildOffAnyBuilding::PlacingTypeNow();

	// Adjacent.Owner.Type on the placed type REPLACES the BaseNormal test the
	// next instruction is about to perform. Reaching 0x4A8FE6 already proves
	// this building belongs to the asking house, so the scope is Owner by
	// construction and needs no re-derivation.
	if (AdjacentClasses::HasAnchorRule(pPlacing, AdjacentClasses::Scope::Owner)
		&& pCellBuilding && pCellBuilding->Type)
	{
		int const range = AdjacentClasses::AnchorRange(
			pPlacing, AdjacentClasses::Scope::Owner, pCellBuilding->Type);

		// -1 means the candidate is in none of the classes the rule names, so
		// the rule refuses it -- a rule REPLACES vanilla rather than adding to
		// it, which is what makes restriction possible at all.
		return range >= 0 ? AcceptAnchor : SkipBuilding;
	}

	if (BuildOffAnyBuilding::ShouldIgnoreBaseNormal(pCellBuilding))
		return AcceptAnchor;

	return 0;
}

// The REPEL scan. Lives here rather than in AdjacentClasses because it is the
// only part that needs the map, and because it is a DIFFERENT QUANTIFIER from
// everything in the engine's loop.
//
// ⚠ WHY A SEPARATE SCAN AT ALL. PassesProximityCheck is an **OR over cells** --
// the accumulator at [ESP+0x3C] is only ever SET, never cleared, so one
// accepting cell carries the whole call. A keep-out is **NOT-EXISTS over
// cells**, which no per-cell accept/skip return can express because a later
// cell always re-accepts. So repel has to fail the whole function, from the
// entry, before any accept can happen.
//
// It also means repel is NOT bound by the engine's Adjacent+1 window, unlike
// the anchor ranges which are clamped to it.
static bool BQExt_RepelScan(
	void* pTypeRaw, int houseArrayIndex, CellStruct* pPosition)
{
	if (!pTypeRaw || !pPosition || !AdjacentClasses::AnyRepelConfigured())
		return false;

	// Pointer identity only -- never dereferenced unless it matches a type we
	// put in the table ourselves.
	auto const pPlacing = reinterpret_cast<BuildingTypeClass*>(pTypeRaw);
	int const range = AdjacentClasses::MaxRepelRange(pPlacing);

	if (range <= 0)
		return false;

	auto const pAsking = AdjacentClasses::HouseByIndex(houseArrayIndex);

	if (!pAsking)
		return false;

	for (int dy = -range; dy <= range; ++dy)
	{
		for (int dx = -range; dx <= range; ++dx)
		{
			CellStruct probe;
			probe.X = static_cast<short>(pPosition->X + dx);
			probe.Y = static_cast<short>(pPosition->Y + dy);

			// TryGetCellAt, not GetCellAt: probing past the map edge is normal
			// here, and the unchecked accessor hands back the out-of-bounds
			// sentinel cell rather than null.
			auto const pCell = MapClass::Instance.TryGetCellAt(probe);

			if (!pCell)
				continue;

			auto const pBld = pCell->GetBuilding();

			if (!pBld || !pBld->IsAlive || pBld->InLimbo
				|| !pBld->Owner || !pBld->Type)
			{
				continue;
			}

			auto const scope = AdjacentClasses::Classify(pAsking, pBld->Owner);
			int const repel = AdjacentClasses::RepelRange(
				pPlacing, scope, pBld->Type);

			if (repel < 0)
				continue;

			// MaxRepelRange sized the window for the WIDEST class; this
			// candidate's own class may repel over a shorter distance, so the
			// per-class range is re-checked here. Chebyshev distance, matching
			// the square window the engine itself uses for Adjacent.
			int const adx = dx < 0 ? -dx : dx;
			int const ady = dy < 0 ? -dy : dy;

			if ((adx > ady ? adx : ady) > repel)
				continue;

			BQExt_LogRepel(pPlacing, scope, pBld->Type, repel);
			return true;
		}
	}

	return false;
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

	enum { ReturnFalse = 0x4A9059 };  // same bare `ret 0x10`, EAX=0 instead

	GET_STACK(void*, pType, 0x4);
	GET_STACK(int const, houseArrayIndex, 0x8);
	GET_STACK(CellStruct*, pPosition, 0x10);

	// ⚠ REPEL FIRST. Checked before Adjacent.NotRequired, or a type carrying
	// both would have its own keep-out rule silently bypassed by its own
	// convenience tag. A ban must beat a grant.
	if (BQExt_RepelScan(pType, houseArrayIndex, pPosition))
	{
		R->EAX(0);
		return ReturnFalse;
	}

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
	// ⚠⚠ ACCEPT IS 0x4A9027 HERE, **NOT** 0x4A8FF5 — AND THE DIFFERENCE HUNG
	// THE GAME. 0x4A8FF5 is the accept instruction `mov [esp+0x3c],1`, and it
	// FALLS THROUGH into 0x4A8FFA, which is this very hook. So returning
	// 0x4A8FF5 from here is a BACKWARD jump into a two-instruction infinite
	// loop: accept -> our hook -> accept -> ... The game froze solid the moment
	// a non-own building with an allow verdict came into range.
	//
	// 0x4A9027 is the byte-for-byte identical accept (`c6 44 24 3c 01`) at the
	// end of the vanilla ally branch, and it falls into the loop-continue at
	// 0x4A902C instead. Forward jump, same effect, terminates.
	//
	// GENERAL RULE for this function: from a hook at address X, only ever jump
	// FORWARD. Every "accept" and "skip" label inside the per-cell loop is a
	// fall-through into the next step, so a backward target re-enters whatever
	// hook sits between it and X.
	// ⚠⚠ ACCEPT IS 0x4A9027 HERE, **NOT** 0x4A8FF5 — AND THE DIFFERENCE HUNG
	// THE GAME. 0x4A8FF5 is the accept instruction and it FALLS THROUGH into
	// 0x4A8FFA, which is this very hook, so returning it is a BACKWARD jump
	// into a two-instruction infinite loop. 0x4A9027 is the byte-for-byte
	// identical accept at the end of the vanilla ally branch and falls into the
	// loop-continue at 0x4A902C.
	//
	// RULE for this function: from a hook at X, only ever jump FORWARD.
	enum { AcceptAnchor = 0x4A9027, SkipBuilding = 0x4A902C };

	GET(BuildingClass*, pCellBuilding, ESI);
	GET_STACK(int const, houseArrayIndex, STACK_OFFSET(0x30, 0x8));

	if (!pCellBuilding || !pCellBuilding->Type)
		return 0;

	auto const pCellOwner = pCellBuilding->Owner;
	auto const pAsking = AdjacentClasses::HouseByIndex(houseArrayIndex);

	if (!pCellOwner || !pAsking)
		return 0;

	// Our own buildings were already decided at 0x4A8FE6. This address is also
	// reached by FALL-THROUGH from that branch's accept, so without this they
	// would be re-judged under rules that do not apply to them.
	if (pCellOwner == pAsking)
		return 0;

	auto const scope = AdjacentClasses::Classify(pAsking, pCellOwner);
	auto const pPlacing = BuildOffAnyBuilding::PlacingTypeNow();

	if (!AdjacentClasses::HasAnchorRule(pPlacing, scope))
		return 0;        // no rule for this scope -> vanilla ally logic stands

	int const range = AdjacentClasses::AnchorRange(
		pPlacing, scope, pCellBuilding->Type);

	return range >= 0 ? AcceptAnchor : SkipBuilding;
}
