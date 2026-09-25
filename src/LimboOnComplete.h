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

	// ⚠ DEFERRED, and it must stay that way.
	//
	// The first version delivered inline from the CompletedProduction hook,
	// calling AbandonProduction() and then returning 0 so the vanilla body ran
	// -- on a factory whose Object had just been nulled. That body walks on into
	// HouseClass::UnitFromFactory and dereferences it: FATAL, C0000005 with
	// EIP=0 returning into 0x4FB2B3. Confirmed in-game 2026-09-24.
	//
	// So MarkPending only records; nothing is created or abandoned until
	// ProcessPending runs at the post-loop seat, by which time the engine has
	// finished with the factory.
	static void MarkPending(FactoryClass* pFactory);
	static void ProcessPending();

private:
	// Registers a fresh instance with the house without placing it.
	// Modelled on Phobos's LimboCreate (src/Ext/SWType/FireSuperWeapon.cpp),
	// reimplemented here so this DLL does not depend on Phobos internals.
	static bool Create(BuildingTypeClass* pType, HouseClass* pOwner);
};
