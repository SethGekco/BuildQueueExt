#pragma once

// Include order copied from AlwaysAvailable.h, which compiles: BuildingClass.h
// pulls in Helpers/Cast.h, whose generic_cast needs FootClass COMPLETE.
#include <TechnoTypeClass.h>
#include <HouseClass.h>
#include <FootClass.h>
#include <BuildingTypeClass.h>
#include <BuildingClass.h>

#include <map>
#include <set>
#include <vector>

// ===========================================================================
// `KickOut.AltFrom=` — unclog a walled-in factory. Ask #19.
//
// THE PROBLEM, measured 2026-10-30. Wall in a lone war factory and finished
// units do not wait and do not re-route: the engine refunds them.
//
//   Failed to exit object from factory - refunding money
//   AbandonProduction obj=ROBO prog=54/1 queued=2
//
// THE CAUSE, from objdump of 0x4444B3-0x444509. When an object cannot leave,
// the engine walks the owner's buildings for somewhere else to exit from, and
// the only candidates it accepts are of the **exact same BuildingType**:
//
//   4444d6:  mov edx,[esi+0x520]      ; THIS factory's Type
//   4444dc:  mov ecx,[ebp+0x520]      ; candidate's Type
//   4444e2:  cmp ecx,edx              ; POINTER EQUALITY OF THE TYPES
//   4444e4:  jne 0x444508             ; different type -> skip entirely
//
// So a blocked GAWEAP looks only for another GAWEAP. It never considers "any
// building whose Factory= matches", and it never reads `Naval` here at all --
// which is why `Naval.EitherFactory` could not help: buildability and exit
// selection are unrelated code paths.
//
// ---------------------------------------------------------------------------
// ⚠ THE TAG GOES ON THE FACTORY, NOT ON THE UNIT — and that is forced
//
// The natural phrasing is "let ROBO also use a ship yard". It cannot be done
// that way at this seat: **EDI and every other register holding the produced
// object are untouched throughout the whole search loop** (verified: the loop
// at 0x4444B3-0x444509 reads only ESI, EBP, EBX, EAX, ECX, EDX). The object
// simply is not a party to the decision. ESI, by contrast, is confirmed
// `BuildingClass*` by Phobos' own hook in this function
// (Phobos/src/Ext/Building/Hooks.cpp:278).
//
// So the blocked factory declares where its output may alternatively leave:
//
//   [GAWEAP]
//   KickOut.AltFrom=GAYARD,GATECH   ; in priority order, and this answers the
//                                   ; "shipyard on land as a second vehicle
//                                   ; queue" case directly
//   KickOut.AltAnyFactory=yes       ; or: any owned building whose Factory=
//                                   ; matches this one's, ignoring Naval
//
// It is also less typing in practice: one tag on the factory instead of one on
// every unit it builds.
//
// No locomotor or SpeedType check, by explicit instruction -- a land unit sent
// to a ship yard will spawn on water, and that is the modder's call to make.
//
// ---------------------------------------------------------------------------
// ⚠ UNVERIFIED: WHETHER ANTARES HONOURS THIS
//
// `0x4444E2` -- the comparison itself -- is **fully replaced by Antares and
// Ares** (`BuildingClass_KickOutUnit_FindAlternateKickout`, size 0x6, which
// swallows the compare, its `jne`, and the `cmp ebp,esi` after it). We cannot
// hook there: Antares loads first and a replaced compare-and-branch almost
// certainly returns a jump, which aborts the rest of the chain.
//
// So this feeds the comparison instead of fighting it, from `0x4444D6` --
// uncontended, one whole instruction, before Antares' stamp, with both ESI and
// EBP live. Writing EDX there makes the candidate's type compare equal.
//
// **If Antares recomputes from ESI/EBP rather than reading EDX, this is inert**
// and the log will show decisions with no change in behaviour. That is the one
// thing a playthrough has to answer, so every decision is logged.

class KickOutAlt
{
public:
	// [BuildQueueExt] KickOut.Alt.Enabled=yes — master switch, off by default.
	static bool Enabled;

	static void ReadGlobalConfig(CCINIClass* pINI);
	static void ReadTypeConfig(CCINIClass* pINI);

	// The value to leave in EDX for the comparison at 0x4444E2. Returns the
	// CANDIDATE's type to accept it, or the blocked factory's own type to leave
	// vanilla behaviour exactly as it was.
	static BuildingTypeClass* TypeForCompare(BuildingClass* pBlocked,
		BuildingClass* pCandidate);

private:
	static std::map<BuildingTypeClass*, std::vector<BuildingTypeClass*>> AltFrom;
	static std::set<BuildingTypeClass*> AnyFactory;

	static bool Accepts(BuildingClass* pBlocked, BuildingClass* pCandidate);
};
