#include "AdjacentClasses.h"

#include <CCINIClass.h>
#include <Utilities/Debug.h>

std::map<std::string, int> AdjacentClasses::ClassIds;
std::vector<std::string> AdjacentClasses::ClassNames;
std::map<BuildingTypeClass*, AdjacentClasses::TypeRules> AdjacentClasses::Rules;
bool AdjacentClasses::RepelConfigured = false;

static int ClampedRanges = 0;

// ---------------------------------------------------------------------------
// INI list helpers

static std::vector<std::string> SplitList(char const* raw)
{
	std::vector<std::string> out;
	std::string cur;

	for (char const* p = raw; *p; ++p)
	{
		if (*p == ',')
		{
			if (!cur.empty())
				out.push_back(cur);

			cur.clear();
		}
		else if (*p != ' ' && *p != '\t')
		{
			cur += *p;
		}
	}

	if (!cur.empty())
		out.push_back(cur);

	return out;
}

static std::vector<std::string> ReadList(
	CCINIClass* pINI, char const* section, char const* key)
{
	char buffer[1024] = {};

	if (pINI->ReadString(section, key, "", buffer, sizeof(buffer)) <= 0)
		return {};

	return SplitList(buffer);
}

// ---------------------------------------------------------------------------
// Config

void AdjacentClasses::ReadGlobalConfig(CCINIClass* pINI)
{
	if (!pINI)
		return;

	// [AdjacentTypes] is an indexed list: 0=, 1=, 2=, ... Read until a gap, the
	// same convention the engine's own type lists use. Accumulates across
	// passes rather than clearing, so a map may append classes -- clearing here
	// would drop the rulesmd registry on the map pass and silently invalidate
	// every membership resolved against it.
	for (int i = 0; ; ++i)
	{
		char key[16] = {};
		_snprintf_s(key, sizeof(key), "%d", i);

		char buffer[128] = {};

		if (pINI->ReadString("AdjacentTypes", key, "", buffer, sizeof(buffer)) <= 0)
			break;

		auto const name = SplitList(buffer);

		if (name.empty())
			continue;

		if (ClassIds.find(name[0]) != ClassIds.end())
			continue;

		ClassIds[name[0]] = int(ClassNames.size());
		ClassNames.push_back(name[0]);

		Debug::Log("[BQExt] AdjacentTypes: class %d = %s\n",
			int(ClassNames.size()) - 1, name[0].c_str());
	}
}

// Reads `<prefix>.<scope>.Type` + `.Range` into a RuleList.
static bool ReadRule(CCINIClass* pINI, char const* section,
	char const* prefix, char const* scope,
	std::map<std::string, int> const& classIds,
	int defaultRange, int rangeCap,
	std::vector<std::pair<int, int>>& out, char const* typeId)
{
	char keyType[64] = {};
	char keyRange[64] = {};
	_snprintf_s(keyType, sizeof(keyType), "%s.%s.Type", prefix, scope);
	_snprintf_s(keyRange, sizeof(keyRange), "%s.%s.Range", prefix, scope);

	auto const names = ReadList(pINI, section, keyType);

	if (names.empty())
		return false;

	auto const ranges = ReadList(pINI, section, keyRange);

	out.clear();

	for (size_t i = 0; i < names.size(); ++i)
	{
		auto const it = classIds.find(names[i]);

		if (it == classIds.end())
		{
			Debug::Log("[BQExt] [%s]%s: unknown adjacency class '%s'"
				" -- not listed in [AdjacentTypes]\n",
				section, keyType, names[i].c_str());
			continue;
		}

		// SHORT RANGE LISTS REPEAT THE LAST VALUE, so `Type=A,B,C` with
		// `Range=12` reads as 12 for all three. An absent Range list falls back
		// to the type's own Adjacent.
		int range = defaultRange;

		if (!ranges.empty())
		{
			size_t const idx = i < ranges.size() ? i : ranges.size() - 1;
			range = atoi(ranges[idx].c_str());
		}

		if (range < 0)
			range = 0;

		// Anchors cannot exceed the engine's own scan window; see the header.
		if (rangeCap >= 0 && range > rangeCap)
		{
			if (++ClampedRanges <= 20)
			{
				Debug::Log("[BQExt] [%s]%s: class '%s' range %d CLAMPED to %d"
					" -- the engine scans only Adjacent+1 cells and that read"
					" (0x4A8F3E) belongs to Phobos, so it cannot be widened."
					" Raise Adjacent= on %s instead.\n",
					section, keyRange, names[i].c_str(), range, rangeCap,
					typeId);
			}

			range = rangeCap;
		}

		out.emplace_back(it->second, range);
	}

	return !out.empty();
}

void AdjacentClasses::ReadTypeConfig(CCINIClass* pINI)
{
	if (!pINI)
		return;

	static char const* const scopeKeys[] =
		{ "Owner", "Team", "Ally", "Enemy", "Neutral" };

	// Read at the Read_File TAIL, where the type array exists. At the entry it
	// is still empty on the rulesmd pass -- the bug that silently swallowed
	// three earlier per-type tags in this project.
	for (auto const pType : BuildingTypeClass::Array)
	{
		if (!pType)
			continue;

		auto const membership = ReadList(pINI, pType->ID, "AdjacentType");

		bool const hasAny = !membership.empty()
			|| pINI->ReadString(pType->ID, "Adjacent.Owner.Type", "",
				nullptr, 0) != 0;

		// Cheap pre-filter: a type with no membership and no rules at all is
		// the overwhelming majority, and touching Rules[] for each would make
		// every per-cell lookup walk a map of every building in the game.
		bool anyRule = !membership.empty();

		TypeRules candidate;

		for (auto const& name : membership)
		{
			auto const it = ClassIds.find(name);

			if (it == ClassIds.end())
			{
				Debug::Log("[BQExt] [%s]AdjacentType: unknown class '%s'"
					" -- not listed in [AdjacentTypes]\n",
					pType->ID, name.c_str());
				continue;
			}

			candidate.Membership.push_back(it->second);
		}

		for (int s = 0; s < int(Scope::Count); ++s)
		{
			// Anchors are capped at the engine's scan window; repel is not,
			// because it runs as our own scan.
			if (ReadRule(pINI, pType->ID, "Adjacent", scopeKeys[s], ClassIds,
				pType->Adjacent, pType->Adjacent, candidate.Anchor[s],
				pType->ID))
			{
				candidate.HasAnchor[s] = true;
				anyRule = true;
			}

			if (ReadRule(pINI, pType->ID, "Repel", scopeKeys[s], ClassIds,
				pType->Adjacent, -1, candidate.Repel[s], pType->ID))
			{
				anyRule = true;
				RepelConfigured = true;

				for (auto const& entry : candidate.Repel[s])
					if (entry.second > candidate.MaxRepel)
						candidate.MaxRepel = entry.second;
			}
		}

		(void)hasAny;

		if (!anyRule)
			continue;

		Rules[pType] = candidate;

		Debug::Log("[BQExt] AdjacentClasses %s: %d class(es), maxRepel=%d\n",
			pType->ID, int(candidate.Membership.size()), candidate.MaxRepel);
	}
}

// ---------------------------------------------------------------------------
// Resolution

AdjacentClasses::Scope AdjacentClasses::Classify(
	HouseClass* pAsking, HouseClass* pOther)
{
	// Neutral is tested before Ally/Enemy because a passive civilian house
	// reads as "not allied" and would otherwise be judged an enemy.
	if (pOther == pAsking)
		return Scope::Owner;

	if (pOther->IsNeutral())
		return Scope::Neutral;

	if (pAsking->IsMutualAlly(pOther))
		return Scope::Team;

	if (pAsking->IsAlliedWith(pOther))
		return Scope::Ally;

	return Scope::Enemy;
}

HouseClass* AdjacentClasses::HouseByIndex(int idx)
{
	// DEFINE_REFERENCE yields a reference to the vector itself, not a pointer.
	if (idx < 0 || idx >= HouseClass::Array.Count)
		return nullptr;

	return HouseClass::Array.Items[idx];
}

char const* AdjacentClasses::ScopeName(Scope scope)
{
	switch (scope)
	{
	case Scope::Owner:   return "Owner";
	case Scope::Team:    return "Team";
	case Scope::Ally:    return "Ally";
	case Scope::Enemy:   return "Enemy";
	case Scope::Neutral: return "Neutral";
	default:             return "?";
	}
}

int AdjacentClasses::Resolve(TypeRules const*, RuleList const& rule,
	BuildingTypeClass* pCandidate)
{
	if (rule.empty() || !pCandidate)
		return -1;

	auto const it = Rules.find(pCandidate);

	if (it == Rules.end())
		return -1;      // candidate declares no membership -> cannot match

	// ⚠ WALK THE CANDIDATE'S ORDER, NOT THE RULE'S. This is the whole point of
	// the design: the candidate's own AdjacentType order decides which of its
	// classes wins, so one rule gives different ranges for GACNST and NACNST.
	// Classes the rule does not name are skipped, not treated as a miss.
	for (int classIdx : it->second.Membership)
	{
		for (auto const& entry : rule)
			if (entry.first == classIdx)
				return entry.second;
	}

	return -1;
}

bool AdjacentClasses::HasAnchorRule(BuildingTypeClass* pPlacing, Scope scope)
{
	if (!pPlacing || scope >= Scope::Count)
		return false;

	auto const it = Rules.find(pPlacing);

	return it != Rules.end() && it->second.HasAnchor[int(scope)];
}

int AdjacentClasses::AnchorRange(
	BuildingTypeClass* pPlacing, Scope scope, BuildingTypeClass* pCandidate)
{
	if (!pPlacing || scope >= Scope::Count)
		return -1;

	auto const it = Rules.find(pPlacing);

	if (it == Rules.end())
		return -1;

	return Resolve(&it->second, it->second.Anchor[int(scope)], pCandidate);
}

int AdjacentClasses::RepelRange(
	BuildingTypeClass* pPlacing, Scope scope, BuildingTypeClass* pCandidate)
{
	if (!pPlacing || scope >= Scope::Count)
		return -1;

	auto const it = Rules.find(pPlacing);

	if (it == Rules.end())
		return -1;

	return Resolve(&it->second, it->second.Repel[int(scope)], pCandidate);
}

int AdjacentClasses::MaxRepelRange(BuildingTypeClass* pPlacing)
{
	if (!pPlacing)
		return 0;

	auto const it = Rules.find(pPlacing);

	return it != Rules.end() ? it->second.MaxRepel : 0;
}

bool AdjacentClasses::AnyRepelConfigured()
{
	return RepelConfigured;
}
