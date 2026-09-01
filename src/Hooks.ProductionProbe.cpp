#include "ProductionProbe.h"

#include <FactoryClass.h>
#include <HouseClass.h>
#include <CCINIClass.h>
#include <Utilities/Macro.h>
#include <Utilities/Debug.h>

// P0 hook set — read-only instrumentation of the production path.
//
// Site selection is deliberate. Every address below is, per the registry
// (YR-Hook-Encyclopedia registry/hooks.csv), hooked by NO framework —
// Antares, Ares, Phobos, Kratos, AggressiveStance and CnCNet-Spawner all leave
// FactoryClass::Suspend / Unsuspend / StartProduction / CompletedProduction
// untouched. So this probe cannot collide with anything, and we learn the
// subsystem on ground nobody else is standing on.
//
// Sites we deliberately do NOT hook, and why:
//   0x4502F4  BuildingClass::Update factory section — three-way conflict
//             (Antares + Ares + Phobos). Never probe a contended site.
//   0x4CA07A  FactoryClass::AbandonProduction     — same three-way conflict.
//   0x5F7900  ObjectTypeClass::FindFactory        — Antares replaces the whole
//             function and returns to the epilogue, so a hook inside the
//             vanilla body is dead code whenever Antares is loaded.
//
// All handlers return 0 (chain-safe) and mutate nothing.

// ---------------------------------------------------------------------------
// Config

DEFINE_HOOK(0x668BF0, BQExt_RulesClass_Addition_ReadProbeConfig, 0x5)
{
	GET_STACK(CCINIClass*, pINI, 0x4);

	ProductionProbe::ReadConfig(pINI);

	return 0;
}

// ---------------------------------------------------------------------------
// Q1 — is Suspend(manual) already feature #1's "hold"?
//
// FactoryClass::Suspend(bool manual) @ 0x4C9E60, Unsuspend @ 0x4C9EA0.
// YRpp documents IsManual as "whether the current suspension state was caused
// by the player", which is exactly the semantics DESIGN.md §4 proposed to
// invent. These two hooks record the before-state at each call so the flag
// transitions can be read straight out of the log.

DEFINE_HOOK(0x4C9E60, BQExt_FactoryClass_Suspend, 0x5)
{
	GET(FactoryClass*, pThis, ECX);

	// The stack argument is the `manual` flag; log it alongside the pre-call
	// state so a player-driven suspend is distinguishable from an engine one
	// (out-of-money suspends pass manual=false).
	GET_STACK(bool, manual, 0x4);

	ProductionProbe::Report(manual ? "Suspend(manual)" : "Suspend(auto)", pThis);

	return 0;
}

// Size 0x6, not 0x5: Syringe stamps 5 bytes and resumes at addr+max(size,5),
// and 0x4C9EA0+5 lands inside an instruction. 0x6 covers whole instructions
// (sub esp,0xC / push esi / mov esi,ecx). This handler returns 0, so the stub's copied-bytes path really
// is taken and the resume address really is reached.
DEFINE_HOOK(0x4C9EA0, BQExt_FactoryClass_Unsuspend, 0x6)
{
	GET(FactoryClass*, pThis, ECX);
	GET_STACK(bool, manual, 0x4);

	ProductionProbe::Report(manual ? "Unsuspend(manual)" : "Unsuspend(auto)", pThis);

	return 0;
}

// ---------------------------------------------------------------------------
// Q2 — does a suspended front block the rest of the queue?
//
// StartProduction @ 0x4CA5A0 ("builds an item from the queue") is the moment
// the factory picks the next item. If a suspended item blocks, this never
// fires while one is held; if the queue advances past it, it does.

// Size 0x7, not 0x5: Syringe stamps 5 bytes and resumes at addr+max(size,5),
// and 0x4CA5A0+5 lands inside an instruction. 0x7 covers whole instructions
// (push esi / mov esi,ecx / push edi / mov eax,[esi+0x50]). This handler returns 0, so the stub's copied-bytes path really
// is taken and the resume address really is reached.
DEFINE_HOOK(0x4CA5A0, BQExt_FactoryClass_StartProduction, 0x7)
{
	GET(FactoryClass*, pThis, ECX);

	ProductionProbe::Report("StartProduction", pThis);

	return 0;
}

// CompletedProduction @ 0x4CA1A0 — "checks the progress and updates the state
// if done". Pairs with StartProduction to bracket one item's lifetime, and
// shows whether a finished item parks (IsSuspended) or blocks.

DEFINE_HOOK(0x4CA1A0, BQExt_FactoryClass_CompletedProduction, 0x5)
{
	GET(FactoryClass*, pThis, ECX);

	ProductionProbe::Report("CompletedProduction", pThis);

	return 0;
}

// AbandonProduction @ 0x4C9FF0 — the *function entry*, which is unhooked.
// (The contended three-way site is 0x4CA07A, further into the body; we stay
// well clear of it.)

// Size 0x6, not 0x5: Syringe stamps 5 bytes and resumes at addr+max(size,5),
// and 0x4C9FF0+5 lands inside an instruction. 0x6 covers whole instructions
// (sub esp,0xC / push esi / mov esi,ecx). This handler returns 0, so the stub's copied-bytes path really
// is taken and the resume address really is reached.
DEFINE_HOOK(0x4C9FF0, BQExt_FactoryClass_AbandonProduction_Entry, 0x6)
{
	GET(FactoryClass*, pThis, ECX);

	ProductionProbe::Report("AbandonProduction", pThis);

	return 0;
}

// ---------------------------------------------------------------------------
// Q3 / Q4 — per-house channel occupancy.
//
// HouseClass::UpdateradarSpied / house AI tick would be too frequent. The
// cleanest low-rate trigger is DemandProduction @ 0x4C9C70: it fires when the
// player (or AI) actually asks for something to be built, which is exactly
// when the channel picture is interesting, and never per-frame.

DEFINE_HOOK(0x4C9C70, BQExt_FactoryClass_DemandProduction, 0x5)
{
	GET(FactoryClass*, pThis, ECX);
	GET_STACK(HouseClass*, pOwner, 0x8);

	if (pOwner)
		ProductionProbe::ReportHouseChannels(pOwner, "DemandProduction");
	else
		ProductionProbe::Report("DemandProduction", pThis);

	return 0;
}
