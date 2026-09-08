#pragma once

#include <BuildingTypeClass.h>
#include <FactoryClass.h>
#include <HouseClass.h>

#include <set>

// Feature: a finished building goes straight into "limbo" instead of handing
// you a placement cursor. See DESIGN.md §7b.
//
// "Limbo" here follows Phobos's LimboDelivery vocabulary, which is a misnomer
// worth stating plainly: the building's InLimbo flag is set to FALSE. What
// makes it limbo is that it is registered with the house -- Buildings list,
// tech tree, power, ConYards/SecretLabs/FactoryPlants -- while never being
// placed on the map. It therefore grants prerequisites, power and superweapons
// while occupying no cells.
//
// Consequence the modder accepted when choosing this over auto-placement: such
// a building is invisible, unselectable, unsellable and undestroyable. There is
// no "un-limbo" path here. Only enable it on types meant as pure stat grants.
//
// Opt-in per BuildingType, default off:
//     [SOMEBUILDING]
//     LimboOnComplete=yes

class LimboOnComplete
{
public:
	// Types flagged LimboOnComplete=yes. Populated once per rules load by
	// scanning BuildingTypeClass::Array, so no per-type ext container is
	// needed for a single boolean.
	static std::set<BuildingTypeClass*> Types;

	static void ReadConfig(CCINIClass* pINI);

	static bool IsEnabledFor(BuildingTypeClass* pType);

	// Returns true if it consumed the completed item (caller should expect the
	// factory to have been cleared).
	static bool TryDeliver(FactoryClass* pFactory);

private:
	// Registers a fresh instance with the house without placing it.
	// Modelled on Phobos's LimboCreate (src/Ext/SWType/FireSuperWeapon.cpp),
	// reimplemented here so this DLL does not depend on Phobos internals.
	static bool Create(BuildingTypeClass* pType, HouseClass* pOwner);
};
