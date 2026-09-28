#pragma once

#include <BuildingTypeClass.h>
#include <HouseClass.h>
#include <GeneralDefinitions.h>

#include <map>

// Ask #2/#3 — concurrent production queues. See DESIGN.md §6b.
//
//     [SOMEBUILDING]
//     Factory.ExtraQueues=1     ; owning this grants +1 concurrent queue
//                               ; for the channel its Factory= produces
//
// slots(house, channel) = 1 + Σ Factory.ExtraQueues over owned buildings whose
// Factory= matches that channel. Slot 0 is the engine's own and is never
// touched; 1..N-1 would be ours.
//
// ---------------------------------------------------------------------------
// P-a IS ACCOUNTING ONLY. Nothing is dispatched, advanced or displayed. It
// computes the slot counts and reports them when they change, to establish two
// things before any behaviour depends on them:
//
//   1. the per-type tag actually parses (the §7e trap: per-type keys read at
//      the Read_File ENTRY see an empty type array and silently do nothing), and
//   2. the count TRACKS the owned-building set -- rising when the granting
//      building goes up and falling when it is sold or destroyed.
//
// ---------------------------------------------------------------------------
// ⚠ WHY THE COUNT IS NEVER CACHED AT LOAD
//
// [[traitext-project]] materialises "when the applied-Trait set changes -- load
// time, or when a building goes up/down" (its DESIGN §5 Rule 2). A Trait can
// therefore add or remove Factory.ExtraQueues mid-match. Snapshotting the total
// at rules load would silently diverge from what the modder configured.
//
// So the total is always derived from the CURRENT owned-building set. That is
// also why it is safe under TraitExt's Rule 1: queue count is **Logical**, not
// Cosmetic -- it changes production, which is synced state -- and the owned
// building set is itself synced, so every client computes the same number with
// no randomness involved.

class ExtraQueues
{
public:
	// The channel a grant applies to. BuildCat is deliberately NOT part of this
	// key yet -- see the open question in the .cpp.
	struct Channel
	{
		AbstractType Produces;
		bool IsNaval;

		bool operator<(Channel const& other) const
		{
			if (Produces != other.Produces)
				return Produces < other.Produces;

			return IsNaval < other.IsNaval;
		}
	};

	// [BuildQueueExt] ExtraQueues.Report=yes — P-a logging. Off by default.
	static bool ReportEnabled;

	static void ReadGlobalConfig(CCINIClass* pINI);
	static void ReadTypeConfig(CCINIClass* pINI);

	// Grant declared by one building type, 0 if untagged.
	static int GrantOf(BuildingTypeClass* pType);

	// Live total for a house+channel: 1 + the sum of grants over owned
	// buildings. Always walks the current building list; never cached.
	static int SlotsFor(HouseClass* pHouse, Channel const& channel);

	// Recompute every house and report only what changed. Called from the
	// post-loop seat, throttled.
	static void ReportChanges();

private:
	static std::map<BuildingTypeClass*, int> Grants;
	static std::map<HouseClass*, std::map<Channel, int>> LastReported;
	static int FrameCounter;
};
