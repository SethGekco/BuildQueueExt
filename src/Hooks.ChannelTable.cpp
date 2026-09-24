#include "ChannelTable.h"

#include <FactoryClass.h>
#include <HouseClass.h>
#include <Utilities/Macro.h>

// P2a — shadow hooks for the channel table. See DESIGN.md §7c/§7d.
//
// Both sites are hooked by NO framework in the registry (Antares, Ares, Phobos,
// Kratos, AggressiveStance, CnCNet-Spawner) despite being the funnel every one
// of them passes through. That is what makes this the available lever: the ~8
// hardcoded `BuildCat::DontCare` call sites live inside compiled Antares.dll /
// Phobos.dll and cannot be edited, but they all end up here.
//
// STOLEN-BYTE SAFETY (✔ both checked against the disassembly):
//   0x500510  mov eax,[esp+4] (4) + dec eax (1) = 5  → resumes at 0x500515
//   0x500850  mov eax,[esp+8] (4) + dec eax (1) = 5  → resumes at 0x500855
// Both are exactly 5 bytes ending on a real instruction boundary, and neither
// stolen sequence contains a relative branch — so `return 0`, which re-runs the
// copied bytes from Syringe's stub, is safe here. (That is not automatic: the
// same pattern crashes at sites whose first five bytes include a jcc.)
//
// Nothing below alters behaviour. Every handler returns 0.

// ---------------------------------------------------------------------------
// P2b — TAKING AUTHORITY.
//
// ⚠ SCOPE CORRECTION. The plan said "serve queueIndex >= 1 from our table
// here". That is not expressible at this seat: `GetPrimaryFactory(abs, naval,
// cat)` has **no queue-index parameter**. The engine asks for *the* primary of
// a channel and expects exactly one factory back; it has no vocabulary for a
// second queue, so it never asks for one. Multi-queue therefore cannot be
// delivered by redirecting this function alone -- it needs the CALLER to know
// which queue it is asking about, which is the same placement-identity problem
// as ask G (DESIGN §3). What this seat *can* do is decide **which** factory
// answers, including supplying one when vanilla has none -- which is exactly
// what `AlwaysAvailable` needs (§7e).
//
// So P2b lands in two steps, and this is the first:
//
//   P2b-1 (here)  AUTHORITATIVE PASS-THROUGH. Take over the return path and
//                 answer with the same value vanilla would. Behaviour must be
//                 bit-identical; the point is to prove this hot seat can hold
//                 authority at all -- correct EAX, correct stack, correct
//                 return -- before anything depends on it. ~310,000 calls per
//                 game means a mistake here is instant and total.
//   P2b-2 (next)  SUBSTITUTION. Answer with a house-owned factory when the
//                 vanilla slot is null, gated per type. That is the real
//                 AlwaysAvailable mechanism.
//
// Returning `0x500570` -- a bare `ret 0xC` (✔ disassembled) -- hands back
// whatever we put in EAX. A non-zero return also means Syringe's stub does NOT
// replay the stolen `mov eax,[esp+4]; dec eax`, which is required: those bytes
// would clobber the EAX we just set.

DEFINE_HOOK(0x500510, BQExt_HouseClass_GetPrimaryFactory, 0x5)
{
	enum { ReturnEAX = 0x500570 };

	GET(HouseClass*, pThis, ECX);
	GET_STACK(AbstractType, absID, 0x4);
	GET_STACK(bool, isNaval, 0x8);
	GET_STACK(BuildCat, cat, 0xC);

	// Observe the key and warm the table from the engine's own slot. This is
	// the ONLY population path -- see the note on SetPrimaryFactory below.
	ChannelTable::ObserveGet(pThis, absID, isNaval, cat);

	if (!ChannelTable::Authoritative)
		return 0;

	// Off by default, and one INI key reverts it. Until P2b-2 this resolves to
	// precisely the vanilla answer, so a behaviour change here is a bug by
	// definition -- which is what makes it a usable test of the seat.
	R->EAX(ChannelTable::Resolve(pThis, absID, isNaval, cat));

	return ReturnEAX;
}

// ---------------------------------------------------------------------------
// ⚠ THERE IS DELIBERATELY NO HOOK ON SetPrimaryFactory (0x500850).
//
// It is a real, correctly-disassembled function that **nothing calls.**
// `grep -c 'call *0x500850'` over the whole disassembled executable returns
// **0**, while GetPrimaryFactory 0x500510 has 6 call sites. The compiler
// inlined every write instead: each slot is written at five scattered sites
// plus once inside the uncalled function itself. For the Buildings slot:
//
//   0x4F59C2, 0x4FA59A, 0x4FA7EC, 0x4FAC5E, 0x4FBCB5   <- the real writes
//   0x5008B0                                           <- inside 0x500850
//
// and 0x53B4 / 0x53B0 show the identical 5+1 pattern.
//
// P2a shipped with a hook here and it recorded **nothing across a full
// playthrough** -- 0 records, 0 agreements, 0 mismatches. The hook was not
// broken; there was simply no traffic. A function having an address, a clean
// prologue and a correct signature says nothing about whether it is *live*.
//
// Consequence: the shadow-comparison design cannot work as intended. It
// compared a setter-populated table against a direct slot read, and the setter
// never fires. Mirroring writes would mean hooking ~30 inlined sites. The
// mapping itself is already established statically -- getter and setter were
// disassembled independently and agree, and the offsets line up exactly with
// YRpp's field order -- so P2b validates it by behaving correctly in-game,
// behind an INI switch that reverts instantly, rather than by a shadow that
// structurally cannot observe what it needs.
