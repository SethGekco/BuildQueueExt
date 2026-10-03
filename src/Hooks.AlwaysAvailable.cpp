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
// EAX IS WRITTEN when AlwaysAvailable.Enabled=yes and the type is tagged and
// the engine found nothing. Returning 0 lets Syringe's stub replay the stolen
// `ret 0x10`, which hands back whatever EAX holds -- so writing EAX here and
// returning 0 is the substitution.

DEFINE_HOOK(0x5F7A89, BQExt_ObjectTypeClass_FindFactory_Epilogue, 0x5)
{
	GET(BuildingClass*, pVerdict, EAX);
	GET(void*, ecx, ECX);
	GET_STACK(HouseClass*, pHouse, 0x10);

	AlwaysAvailable::ProbeEpilogue(ecx, pVerdict, pHouse);

	// Resolve returns pVerdict unchanged unless this is a tagged type with no
	// factory, so the common path writes EAX back to the value it already had.
	if (auto const pResolved = AlwaysAvailable::Resolve(ecx, pVerdict, pHouse))
		R->EAX(pResolved);

	return 0;
}

// HouseClass::ShouldDisableCameo EPILOGUE @ 0x50B669 — step (e), and the seat
// that actually decides whether the cameo is dark and refuses clicks. See
// AlwaysAvailable.h for why no CanBuild seat can substitute for this one.
//
// STOLEN BYTES. Phobos stamps this same address with size 0x5
// (Phobos/src/Ext/House/Hooks.cpp:349) and always returns 0, so 5 bytes is
// known-safe here and the chain is cooperative. Injection order from
// syringe.log is Phobos.dll then BuildQueueExt.dll, so Phobos' raise runs
// first and our lower runs after it -- deliberate, since we must be able to
// clear a disable that Antares set for the no-factory reason.
//
//   ECX       = HouseClass*
//   [ESP+0x4] = TechnoTypeClass*
//   EAX       = bool disable, as computed by Antares and then Phobos

DEFINE_HOOK(0x50B669, BQExt_HouseClass_ShouldDisableCameo_Epilogue, 0x5)
{
	GET(HouseClass*, pThis, ECX);
	GET_STACK(TechnoTypeClass*, pType, 0x4);

	// ⚠ READ AS int AND MASK, never GET(bool, .., EAX). The function returns a
	// bool in AL and the upper three bytes of EAX are not guaranteed clean, so
	// casting the whole register to bool can read true where AL is 0.
	// PrerequisiteExt masks with 0xFF at this same address for this reason.
	GET(int const, incoming, EAX);
	bool const disable = (incoming & 0xFF) != 0;

	AlwaysAvailable::ProbeDisableCameo(pThis, pType, disable);

	bool const resolved =
		AlwaysAvailable::ResolveDisableCameo(pThis, pType, disable);

	// Written only on an actual change, so the common path leaves EAX exactly
	// as the earlier handlers in the chain left it. Writing an explicit 0/1
	// rather than a bool keeps the upper bytes defined for whoever reads next.
	if (resolved != disable)
		R->EAX(resolved ? 1 : 0);

	return 0;
}
