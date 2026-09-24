#pragma once

#include <FactoryClass.h>
#include <HouseClass.h>
#include <GeneralDefinitions.h>

#include <map>

// P2 — the per-house production channel table. See DESIGN.md §7c/§7d.
//
// A "channel" is one production queue, keyed by the tuple the engine itself
// uses plus an index for the extra queues Factory.Mode=Queue creates:
//
//     ( AbstractType , isNaval , BuildCat , queueIndex )
//
// queueIndex 0 is the vanilla channel and stays backed by the engine's own
// storage. Indices >= 1 are ours. That split is deliberate: vanilla behaviour
// must remain byte-identical whether or not this DLL is loaded.
//
// ---------------------------------------------------------------------------
// THE VANILLA SLOT MAP (✔ disassembled from both the getter and the setter,
// which agree exactly)
//
//   HouseClass::GetPrimaryFactory  0x500510   __thiscall, ret 0x0C
//     ECX = HouseClass*,  [ESP+4] = AbstractType,  [ESP+8] = bool naval,
//     [ESP+0xC] = BuildCat   → EAX = FactoryClass*
//
//   HouseClass::SetPrimaryFactory  0x500850   __thiscall, ret 0x10
//     ECX = HouseClass*,  [ESP+4] = FactoryClass*, [ESP+8] = AbstractType,
//     [ESP+0xC] = bool naval, [ESP+0x10] = BuildCat
//
//   offset  field                  selected when
//   0x53AC  Primary_ForAircraft    AircraftType
//   0x53B0  Primary_ForInfantry    InfantryType
//   0x53B4  Primary_ForVehicles    UnitType, naval = false
//   0x53B8  Primary_ForShips       UnitType, naval = true
//   0x53BC  Primary_ForBuildings   BuildingType, BuildCat != Combat
//   0x53C0  Primary_Unused1        -- never read, never written
//   0x53C4  Primary_Unused2        -- never read, never written
//   0x53C8  Primary_Unused3        -- never read, never written
//   0x53CC  Primary_ForDefenses    BuildingType, BuildCat == Combat
//
// Both functions dispatch through a 40-entry byte table indexed by
// (AbstractType - 1) into a FIVE-case jump table. Nothing in either jump table
// touches 0x53C0/0x53C4/0x53C8.
//
// ⚠ **This answers probe question Q5, and the answer is no.** The three
// `Primary_Unused*` slots are structurally unreachable through the engine's own
// accessors — they are dead storage, not spare capacity. Any use of them would
// have to come entirely from our own routing, which means they buy nothing over
// the table below. Do not plan around them.
//
// ---------------------------------------------------------------------------
// P2a IS SHADOW MODE. The hooks observe and record; they do not steer. Every
// handler returns 0 and lets the vanilla body run. The point is to prove the
// key derivation and the slot map against the live engine before any routing
// decision depends on them -- this subsystem has already produced two wrong
// conclusions from plausible inference (DESIGN.md §0).

class ChannelTable
{
public:
	// Vanilla slot offsets, from the disassembly above.
	static constexpr int SlotAircraft  = 0x53AC;
	static constexpr int SlotInfantry  = 0x53B0;
	static constexpr int SlotVehicles  = 0x53B4;
	static constexpr int SlotShips     = 0x53B8;
	static constexpr int SlotBuildings = 0x53BC;
	static constexpr int SlotUnused1   = 0x53C0;
	static constexpr int SlotUnused2   = 0x53C4;
	static constexpr int SlotUnused3   = 0x53C8;
	static constexpr int SlotDefenses  = 0x53CC;

	// [BuildQueueExt] ChannelTable=yes — shadow logging. Off by default.
	static bool ShadowEnabled;

	// [BuildQueueExt] ChannelTable.Authoritative=yes — P2b. Off by default.
	// When set, we answer GetPrimaryFactory ourselves instead of letting the
	// vanilla body run. In P2b-1 the answer is identical to vanilla's, so any
	// observable change is a bug; that is the point of the step.
	static bool Authoritative;
	static void ReadConfig(CCINIClass* pINI);

	// The vanilla slot offset this key resolves to, or 0 when the key has no
	// vanilla slot (i.e. the engine would return null / ignore the call).
	static int VanillaSlotOffset(AbstractType absID, bool isNaval, BuildCat cat);

	// Reads that slot straight out of the house, reproducing what the engine's
	// getter is about to return. Used to check our derivation against reality.
	static FactoryClass* ReadVanillaSlot(
		HouseClass* pHouse, AbstractType absID, bool isNaval, BuildCat cat);

	// Packs the tuple into one int for map keying. queueIndex 0 = vanilla.
	static int EncodeKey(AbstractType absID, bool isNaval, BuildCat cat, int queueIndex);
	static void DescribeKey(char* buffer, size_t size,
		AbstractType absID, bool isNaval, BuildCat cat, int queueIndex);

	// Shadow record, driven by SetPrimaryFactory.
	static void Record(HouseClass* pHouse, AbstractType absID, bool isNaval,
		BuildCat cat, int queueIndex, FactoryClass* pFactory);

	// Returns nullptr when absent. Never inserts — this is called on a hot path
	// and a default-inserting lookup here would grow the map forever.
	static FactoryClass* Lookup(HouseClass* pHouse, AbstractType absID,
		bool isNaval, BuildCat cat, int queueIndex);

	// Driven by GetPrimaryFactory -- the ONLY live population path, because
	// SetPrimaryFactory has no callers (see Hooks.ChannelTable.cpp). Warms the
	// table from the engine's slot and reports the call pattern sparsely.
	static void ObserveGet(
		HouseClass* pHouse, AbstractType absID, bool isNaval, BuildCat cat);

	// The value served when Authoritative. P2b-1: exactly vanilla's answer.
	static FactoryClass* Resolve(
		HouseClass* pHouse, AbstractType absID, bool isNaval, BuildCat cat);

	static void Clear();

private:
	static std::map<HouseClass*, std::map<int, FactoryClass*>> Tables;

	// Divergence reports are rate-limited: the getter is a hot path and an
	// unbounded log would be both useless and enormous.
	static int Mismatches;
	static int Agreements;
	static int Records;
	static int Gets;
	static int UnmappedGets;
	static int Resolves;
	static constexpr int MaxMismatchReports = 40;
};
