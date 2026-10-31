#include "KickOutAlt.h"

#include <Utilities/Macro.h>

// BuildingClass::KickOutUnit, alternate-exit search @ 0x4444D6.
//
// WHY HERE AND NOT AT THE COMPARISON. See the long note in KickOutAlt.h: the
// comparison at 0x4444E2 is wholly replaced by Antares, which recomputes the
// predicate from ESI/EBP/EDI in C++ and returns non-zero on BOTH paths -- so
// nothing we write in EDX is read, and no handler of ours there would ever
// run. One playthrough confirmed the EDX approach was inert.
//
// 0x4444D6 is one instruction EARLIER and uncontended by Antares, Ares and
// Phobos alike, with every register the accept path needs already live:
//
//   4444d3:  mov ebp,[edx+ebx*4]      ; candidate                     (EBP)
//   4444d6:  mov edx,[esi+0x520]      ; <- HERE
//   4444dc:  mov ecx,[ebp+0x520]
//   4444e2:  cmp/jne                  ; Antares -- stepped over on accept
//   ...
//   44451f:  mov ebx,[esi+0x524]      ; the engine's own ACCEPT label
//
//   ESI = BuildingClass*   the blocked factory
//   EBP = BuildingClass*   the candidate being considered this iteration
//   EDI = TechnoClass*     the produced object (read by 0x44453F, untouched)
//
// STOLEN BYTES `8b 96 20 05 00 00` = 6, ONE whole instruction. Syringe resumes
// at addr + max(size,5) = 0x4444DC, which is an instruction boundary; a 5-byte
// declaration would split this mov and resume on its last operand byte.
//
// ⚠ WE RETURN A JUMP ONLY TO ACCEPT. Every other verdict returns 0, so the
// stub replays the stolen mov and Antares decides exactly as it always has.
// A tagged factory can therefore only ever GAIN exit candidates, and an
// untagged one is bit-for-bit unchanged. Jumping to the reject label instead
// would be wrong -- Antares may well have accepted that candidate itself.
//
// ⚠ 0x44451F IS A FORWARD JUMP from here, per this project's standing rule for
// functions whose labels are fall-throughs. It reads only ESI, EBP and EDI and
// overwrites EAX/EBX/ECX itself, so nothing the stolen mov would have set is
// needed there. Verified by objdump of 0x44451F-0x444558.

DEFINE_HOOK(0x4444D6, BQExt_BuildingClass_KickOutUnit_AltFactory, 0x6)
{
	enum { AcceptThisBuilding = 0x44451F };

	GET(BuildingClass*, pBlocked, ESI);
	GET(BuildingClass*, pCandidate, EBP);

	if (KickOutAlt::AcceptsAsExit(pBlocked, pCandidate))
		return AcceptThisBuilding;

	return 0;
}
