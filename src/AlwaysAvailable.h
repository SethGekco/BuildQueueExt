#pragma once

#include <TechnoTypeClass.h>
#include <HouseClass.h>
#include <FactoryClass.h>

#include <set>

// Ask #11 — a building buildable with no Construction Yard. See DESIGN.md §7e.
//
// ---------------------------------------------------------------------------
// WHY THIS SEAT, AND WHY IT IS AWKWARD
//
// The gate is not the prerequisite (a plain INI edit) but the FACTORY:
// `HouseExt::HasFactory` keeps only buildings whose `Type->Factory == abs`,
// which for a BuildingType is the Construction Yard, and returns null when
// there is none.
//
// Two earlier seats were rejected on evidence:
//   * `GetPrimaryFactory` 0x500510 — a null primary means *idle*, not "no
//     factory available" (the slot is written in the production-BEGIN path),
//     and it never sees the TYPE, only the category. Both fatal.
//   * `FindFactory` ENTRY 0x5F7900 — Antares fully replaces it and returns
//     non-zero, and a non-zero return **aborts the rest of the Syringe chain**
//     (verified from the stub in PrerequisiteExt's HOOKS_LOG). Antares is
//     injected BEFORE us, so a handler here would never run.
//
// That leaves the epilogue `0x5F7A89`, where Antares' verdict is in EAX. Same
// pattern PrerequisiteExt uses at the `CanBuild` epilogue 0x4F8361. It is a
// bare `ret 0x10` (3 bytes) followed by nops, so the 5-byte stamp costs only
// padding — cleaner than the CanBuild case, which clips a jump table.
//
// ---------------------------------------------------------------------------
// ⚠ THE OPEN QUESTION THIS STEP EXISTS TO ANSWER
//
// `FindFactory` is `__thiscall`: the TechnoType arrives in **ECX**, and
// `ret 0x10` means it is not on the stack. Antares' handler reads ECX, writes
// EAX and jumps here. Syringe's stub restores registers before jumping, so ECX
// *should* still hold the type — but "should" is exactly what has been wrong
// three times in this subsystem (a dead function, unreachable fields, a slot
// whose null meant something else).
//
// So this increment does NOT substitute anything. It identifies ECX by
// **pointer comparison against the known type arrays** — crash-safe, since a
// garbage value simply matches nothing and is never dereferenced — and reports
// whether the type survived. One run decides whether the whole approach works.

class AlwaysAvailable
{
public:
	// [SOMEBUILDING] AlwaysAvailable=yes — parsed now, consumed once the
	// epilogue is proven to carry the type.
	static std::set<TechnoTypeClass*> Types;

	// [BuildQueueExt] AlwaysAvailable.Probe=yes — the diagnostic below.
	static bool ProbeEnabled;

	static void ReadConfig(CCINIClass* pINI);
	static bool IsEnabledFor(TechnoTypeClass* pType);

	// Returns the argument only if it is genuinely one of the engine's
	// TechnoTypes. Pure pointer comparison: never dereferences the candidate,
	// so an arbitrary register value is safe to pass in.
	static TechnoTypeClass* IdentifyType(void* candidate);

	// Called at the FindFactory epilogue with the incoming verdict.
	static void ProbeEpilogue(void* ecx, FactoryClass* pVerdict,
		HouseClass* pHouse);

private:
	static int Calls;
	static int TypeRecovered;
	static int TypeLost;
	static int NullVerdicts;
};
