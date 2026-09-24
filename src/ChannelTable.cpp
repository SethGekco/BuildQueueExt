#include "ChannelTable.h"

#include <CCINIClass.h>
#include <Utilities/Debug.h>

#include <cstdio>

bool ChannelTable::ShadowEnabled = false;
std::map<HouseClass*, std::map<int, FactoryClass*>> ChannelTable::Tables;
int ChannelTable::Mismatches = 0;
int ChannelTable::Agreements = 0;
int ChannelTable::Records = 0;
int ChannelTable::Gets = 0;
int ChannelTable::UnmappedGets = 0;

void ChannelTable::ReadConfig(CCINIClass* pINI)
{
	if (!pINI)
		return;

	// Current value as the default — RulesClass::Addition runs once per INI and
	// a literal default would switch this off on the map pass. See
	// ProductionProbe::ReadConfig for the full account of that bug.
	ShadowEnabled = pINI->ReadBool("BuildQueueExt", "ChannelTable", ShadowEnabled);
}

int ChannelTable::VanillaSlotOffset(AbstractType absID, bool isNaval, BuildCat cat)
{
	// Mirrors the engine's five-case dispatch exactly (see header). Any key not
	// covered here is one the engine itself ignores.
	switch (absID)
	{
	case AbstractType::AircraftType:
		return SlotAircraft;

	case AbstractType::InfantryType:
		return SlotInfantry;

	case AbstractType::UnitType:
		return isNaval ? SlotShips : SlotVehicles;

	case AbstractType::BuildingType:
		return cat == BuildCat::Combat ? SlotDefenses : SlotBuildings;

	default:
		return 0;
	}
}

FactoryClass* ChannelTable::ReadVanillaSlot(
	HouseClass* pHouse, AbstractType absID, bool isNaval, BuildCat cat)
{
	if (!pHouse)
		return nullptr;

	auto const offset = VanillaSlotOffset(absID, isNaval, cat);

	if (!offset)
		return nullptr;

	return *reinterpret_cast<FactoryClass**>(
		reinterpret_cast<char*>(pHouse) + offset);
}

int ChannelTable::EncodeKey(
	AbstractType absID, bool isNaval, BuildCat cat, int queueIndex)
{
	// absID is small (< 0x28), BuildCat < 8, naval is a bit. Pack them low and
	// leave the queue index room to grow, since Factory.Mode=Queue makes the
	// count unbounded in principle.
	return (static_cast<int>(absID) & 0xFF)
		| ((isNaval ? 1 : 0) << 8)
		| ((static_cast<int>(cat) & 0x7) << 9)
		| (queueIndex << 12);
}

void ChannelTable::DescribeKey(char* buffer, size_t size,
	AbstractType absID, bool isNaval, BuildCat cat, int queueIndex)
{
	_snprintf_s(buffer, size, _TRUNCATE, "abs=%d naval=%d cat=%d q=%d",
		static_cast<int>(absID), isNaval ? 1 : 0,
		static_cast<int>(cat), queueIndex);
}

void ChannelTable::Record(HouseClass* pHouse, AbstractType absID, bool isNaval,
	BuildCat cat, int queueIndex, FactoryClass* pFactory)
{
	if (!pHouse)
		return;

	// Only the setter inserts. Houses are long-lived (one per player for the
	// whole game), so this map stays small and bounded -- unlike a per-techno
	// map, which is the shape that leaked in AggressiveStance.
	Tables[pHouse][EncodeKey(absID, isNaval, cat, queueIndex)] = pFactory;

	// Same reasoning as the agreement counter: prove the setter fires at all.
	if (++Records == 1 || Records % 500 == 0)
	{
		char key[96];
		DescribeKey(key, sizeof(key), absID, isNaval, cat, queueIndex);
		Debug::Log("[BQExt] ChannelTable record #%d %s -> %p\n",
			Records, key, pFactory);
	}
}

FactoryClass* ChannelTable::Lookup(HouseClass* pHouse, AbstractType absID,
	bool isNaval, BuildCat cat, int queueIndex)
{
	if (!pHouse)
		return nullptr;

	// find(), never operator[] -- this runs on the getter's hot path and a
	// default-inserting lookup would grow the map on every call forever.
	auto const itHouse = Tables.find(pHouse);

	if (itHouse == Tables.end())
		return nullptr;

	auto const itKey = itHouse->second.find(
		EncodeKey(absID, isNaval, cat, queueIndex));

	return itKey == itHouse->second.end() ? nullptr : itKey->second;
}

void ChannelTable::ObserveGet(
	HouseClass* pHouse, AbstractType absID, bool isNaval, BuildCat cat)
{
	if (!pHouse)
		return;

	++Gets;

	auto const offset = VanillaSlotOffset(absID, isNaval, cat);

	// A key the engine itself ignores. Worth counting separately rather than
	// silently dropping -- if this ever dominates, the key derivation is wrong.
	if (!offset)
	{
		++UnmappedGets;
		return;
	}

	auto const pVanilla = ReadVanillaSlot(pHouse, absID, isNaval, cat);

	// Warm the table from the engine's own storage. This is the only
	// population path: SetPrimaryFactory has no callers (see
	// Hooks.ChannelTable.cpp), so every slot write is inlined and unobservable
	// without hooking ~30 scattered sites.
	if (pVanilla)
		Record(pHouse, absID, isNaval, cat, 0, pVanilla);

	if (!ShadowEnabled)
		return;

	// Report the call pattern sparsely. The useful signal now is *which*
	// channels get queried and how hot the path is -- P2b has to serve every
	// one of these -- not a comparison, which would be circular once the table
	// is populated from the same slot it would be checked against.
	if (Gets == 1 || Gets % 2000 == 0)
	{
		char key[96];
		DescribeKey(key, sizeof(key), absID, isNaval, cat, 0);
		Debug::Log("[BQExt] ChannelTable get #%d %s -> slot 0x%X = %p"
			"  [records %d, unmapped %d]\n",
			Gets, key, offset, pVanilla, Records, UnmappedGets);
	}
}

void ChannelTable::Clear()
{
	Tables.clear();
	Mismatches = 0;
	Agreements = 0;
	Records = 0;
	Gets = 0;
	UnmappedGets = 0;
}
