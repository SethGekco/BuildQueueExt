#include "KickOutAlt.h"

#include <Utilities/Macro.h>

// BuildingClass::KickOutUnit, alternate-exit search @ 0x4444D6.
//
// The search at 0x4444B3-0x444509 accepts only candidates of the EXACT SAME
// BuildingType. This hook feeds that comparison rather than replacing it:
//
//   4444d3:  mov ebp,[edx+ebx*4]      ; candidate                  (EBP)
//   4444d6:  mov edx,[esi+0x520]      ; <- HERE: blocked factory's Type
//   4444dc:  mov ecx,[ebp+0x520]      ; candidate's Type
//   4444e2:  cmp ecx,edx              ; Antares/Ares REPLACE from here
//
// STOLEN BYTES `8b 96 20 05 00 00` = 6, ONE whole instruction. A 5-byte stamp
// would split it.
//
// ⚠ RETURNS A JUMP, NOT 0, AND THAT IS LOAD-BEARING. Returning 0 would let the
// stub replay the stolen `mov edx,[esi+0x520]`, overwriting the EDX we just
// wrote. Jumping to 0x4444DC skips the replay and keeps our value. Forward
// jump, consistent with this project's rule for the proximity function.
//
// ⚠ SEAT IS UNCONTENDED. 0x4444E2 itself is Antares + Ares (size 0x6);
// 0x4444B3 is Antares; 0x4444A0 is Phobos (size 0xA, ending at 0x4444AA).
// 0x4444D6..0x4444DB is clear of all three.
//
//   ESI = BuildingClass*   the blocked factory
//   EBP = BuildingClass*   the candidate being considered this iteration

DEFINE_HOOK(0x4444D6, BQExt_BuildingClass_KickOutUnit_AltFactory, 0x6)
{
	enum { Resume = 0x4444DC };

	GET(BuildingClass*, pBlocked, ESI);
	GET(BuildingClass*, pCandidate, EBP);

	R->EDX(KickOutAlt::TypeForCompare(pBlocked, pCandidate));

	return Resume;
}
