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

DEFINE_HOOK(0x500510, BQExt_HouseClass_GetPrimaryFactory_Shadow, 0x5)
{
	GET(HouseClass*, pThis, ECX);
	GET_STACK(AbstractType, absID, 0x4);
	GET_STACK(bool, isNaval, 0x8);
	GET_STACK(BuildCat, cat, 0xC);

	// Observe the key and warm the table from the engine's own slot. This is
	// now the ONLY population path -- see the note on SetPrimaryFactory below.
	ChannelTable::ObserveGet(pThis, absID, isNaval, cat);

	return 0;
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
