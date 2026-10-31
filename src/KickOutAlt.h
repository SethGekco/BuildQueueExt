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
// ⚠ THE SEAT: WHY WE DECIDE AT 0x4444D6 AND JUMP PAST ANTARES
//
// FIRST ATTEMPT, AND WHY IT WAS INERT. The idea was to *feed* the comparison
// at 0x4444E2 by writing EDX at 0x4444D6, so the candidate compared equal.
// One playthrough (2026-10-30) showed our ACCEPT logged and the unit refunded
// on the very next line. Antares' source says exactly why -- it does not read
// EDX at all, it recomputes the whole predicate in C++
// (Antares-src/src/Ext/TechnoType/Hooks.cpp:144):
//
//   DEFINE_HOOK(0x4444E2, BuildingClass_KickOutUnit_FindAlternateKickout, 0x6)
//   {
//       GET(BuildingClass*, Src, ESI); GET(BuildingClass*, Tst, EBP);
//       GET(TechnoClass*, Production, EDI);
//       if (Src != Tst
//        && Tst->GetCurrentMission() == Mission::Guard
//        && Tst->Type->Factory == Src->Type->Factory
//        && Tst->Type->Naval == Src->Type->Naval        // <-- THE BLOCKER
//        && pData->CanBeBuiltAt(Tst->Type)
//        && !Tst->Factory)
//           return 0x44451F;                            // accept
//       return 0x444508;                                // next candidate
//   }
//
// Two things follow. First, Antares has ALREADY relaxed vanilla's exact-type
// rule to a same-`Factory=`-category rule, so a blocked GAWEAP can already
// exit from a second GAWEAP. The single remaining obstacle for the reported
// case is `Tst->Type->Naval == Src->Type->Naval`: GAWEAP is Naval=no, GAYARD
// is Naval=yes. Second, it returns non-zero on BOTH paths, so a handler of
// ours at 0x4444E2 would never run -- Antares is injected first and a non-zero
// return aborts the rest of the chain.
//
// THE FIX. 0x4444D6 runs one instruction EARLIER than Antares' stamp, with
// ESI, EBP and EDI all live, so we decide there and jump straight to the
// engine's own accept label, stepping over Antares entirely:
//
//   4444d6:  <- us. accept -> jump 0x44451F, otherwise return 0
//   4444dc:  mov ecx,[ebp+0x520]
//   4444e2:  cmp/jne                    <- Antares (never reached on accept)
//   ...
//   444502:  je 0x44451F                ; the engine's own ACCEPT
//   444508:  inc ebx                    ; next candidate
//   44451f:  mov ebx,[esi+0x524]        ; hand ESI's Factory to EBP and exit
//
// 0x44451F needs only ESI, EBP and EDI, all of which are already correct at
// 0x4444D6 -- it overwrites EBX, EAX, ECX itself. Verified by objdump.
//
// ⚠ WE ONLY EVER RETURN A JUMP TO **ACCEPT**. On any other verdict we
// return 0 and let Antares decide as it always has. A tagged factory therefore
// only ever gains candidates, and an untagged one is bit-for-bit unchanged.
// Jumping to 0x444508 to reject would be wrong: Antares might have accepted
// that candidate on its own terms.
//
// ⚠ ACCEPTING BYPASSES Ares' `BuiltAt=` (`CanBeBuiltAt`), because that
// lives in Antares' ext map and we cannot call it. For a factory whose
// alternates the modder listed by hand this is the lesser surprise -- the
// explicit list IS the statement of intent -- but it is a real divergence.
//
// Because we bypass Antares on the accept path we must re-apply the engine's
// own conditions ourselves: candidate on Guard mission (the vtable call at
// 0x4444EA, Mission::Guard == 5) and not already running a factory of its own
// (0x4444FA, `Factory` at +0x524). Both are in AcceptsAsExit.

class KickOutAlt
{
public:
	// [BuildQueueExt] KickOut.Alt.Enabled=yes — master switch, off by default.
	static bool Enabled;

	static void ReadGlobalConfig(CCINIClass* pINI);
	static void ReadTypeConfig(CCINIClass* pINI);

	// True => this candidate is a valid alternate exit and the caller should
	// jump to the engine's accept label. False => return 0 and let Antares
	// decide, exactly as without this feature.
	static bool AcceptsAsExit(BuildingClass* pBlocked,
		BuildingClass* pCandidate);

private:
	static std::map<BuildingTypeClass*, std::vector<BuildingTypeClass*>> AltFrom;
	static std::set<BuildingTypeClass*> AnyFactory;

	static bool Accepts(BuildingClass* pBlocked, BuildingClass* pCandidate);
};
