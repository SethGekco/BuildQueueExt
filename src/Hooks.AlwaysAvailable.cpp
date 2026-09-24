#include "AlwaysAvailable.h"

#include <HouseClass.h>
#include <BuildingClass.h>
#include <Utilities/Macro.h>

// ObjectTypeClass::FindFactory EPILOGUE @ 0x5F7A89 — see AlwaysAvailable.h for
// why this is the only reachable seat.
//
// STOLEN BYTES. 0x5F7A89 is `ret 0x10` (3 bytes, `c2 10 00`) followed by four
// `nop`s at 0x5F7A8C-0x5F7A8F, with real code resuming at 0x5F7A90. Syringe's
// 5-byte stamp therefore covers `ret` + two nops — whole instructions, nothing
// but padding sacrificed, and no jump table to clip (unlike the CanBuild
// epilogue 0x4F8361, which does clip one and is safe only because Antares makes
// that body unreachable).
//
// Returning 0 lets the stub replay the stolen bytes; the `ret` executes first
// and returns with whatever is in EAX. That is exactly the pattern
// PrerequisiteExt uses at 0x4F8361.
//
// REGISTERS AT THIS POINT. Antares reached here via `return 0x5F7A89` from its
// full replacement of the entry, having written its verdict to EAX. The
// TechnoType arrived in ECX and -- because `ret 0x10` -- is not on the stack.
// Whether ECX survives Antares' handler is THE open question; this hook exists
// to answer it, not to act on it.
//
//   EAX        = BuildingClass*     Antares' verdict -- the factory BUILDING,
//                                  NOT a FactoryClass (null = none found)
//   ECX        = TechnoTypeClass*   ...if it survived
//   [ESP+0x4]  = bool allowOccupied
//   [ESP+0x8]  = bool requirePower
//   [ESP+0xC]  = bool requireCanBuild
//   [ESP+0x10] = HouseClass*
//
// Nothing is substituted yet and EAX is not written. Strictly diagnostic.

DEFINE_HOOK(0x5F7A89, BQExt_ObjectTypeClass_FindFactory_Epilogue, 0x5)
{
	GET(BuildingClass*, pVerdict, EAX);
	GET(void*, ecx, ECX);
	GET_STACK(HouseClass*, pHouse, 0x10);

	AlwaysAvailable::ProbeEpilogue(ecx, pVerdict, pHouse);

	return 0;
}
