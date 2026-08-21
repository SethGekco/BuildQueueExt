#pragma once

#include <FactoryClass.h>
#include <HouseClass.h>

// P0 — the production probe.
//
// BuildQueueExt's design was wrong twice because it assumed engine behaviour
// instead of observing it (see DESIGN.md §0). Before any feature code, this
// probe instruments the production path and reports what the engine actually
// does, so P1/P2 are built on measurements rather than inference.
//
// It writes to the log only. It changes no game state, allocates nothing per
// frame, and every hook returns 0 (chain-safe).
//
// Open questions it exists to answer:
//   Q1  Does FactoryClass::Suspend(manual=true) already deliver feature #1's
//       "hold"? What happens to Production/Balance/OnHold while suspended?
//   Q2  Does a suspended front block the rest of the queue, or do queued items
//       advance past it? (Decides whether #1 and #2 are truly orthogonal.)
//   Q3  Is HouseClass::Production the AI flag? (DESIGN.md §3 marks this
//       unverified; it gates the whole of P2.)
//   Q4  How many FactoryClass instances does one house hold at once, per
//       channel? (Confirms the 5-channel model against reality.)

class ProductionProbe
{
public:
	// [BuildQueueExt] Probe=yes in rulesmd.ini. Off by default: this is a
	// diagnostic build feature and it is chatty.
	static bool Enabled;
	// Probe.Verbose=yes also logs per-step progress ticks (very chatty).
	static bool Verbose;

	static void ReadConfig(CCINIClass* pINI);

	// One-line snapshot of a factory's state, tagged with the call site.
	static void Report(const char* site, FactoryClass* pFactory);

	// Walks FactoryClass::Array and summarises per-house channel occupancy.
	// Answers Q4 (and Q3, by printing Production alongside the house type).
	static void ReportHouseChannels(HouseClass* pHouse, const char* site);

private:
	// Cheap guard so Report() is a no-op in normal play.
	static bool ShouldLog(FactoryClass* pFactory);
};
