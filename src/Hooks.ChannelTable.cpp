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

	// Read-only cross-check of our key derivation against the engine's own
	// slot. Silent unless they disagree.
	ChannelTable::VerifyAgainstVanilla(pThis, absID, isNaval, cat);

	return 0;
}

DEFINE_HOOK(0x500850, BQExt_HouseClass_SetPrimaryFactory_Shadow, 0x5)
{
	GET(HouseClass*, pThis, ECX);
	GET_STACK(FactoryClass*, pFactory, 0x4);
	GET_STACK(AbstractType, absID, 0x8);
	GET_STACK(bool, isNaval, 0xC);
	GET_STACK(BuildCat, cat, 0x10);

	// queueIndex 0: this is the vanilla channel. Extra queues do not exist yet
	// -- P3 (Factory.Mode=Queue) is what starts allocating indices >= 1.
	ChannelTable::Record(pThis, absID, isNaval, cat, 0, pFactory);

	return 0;
}
