# BuildQueueExt — Design (rev 3)

A production **and sidebar** subsystem for Red Alert 2: Yuri's Revenge. Standalone
Syringe DLL, coexists with Phobos, built against **Antares** ([[antares-replaces-ares]]).

**Rev 3 merges the former SidebarExt project into this one** (see §0). The two were
separate designs fighting over the same code: production decides *what is in a queue*,
the sidebar decides *where that queue is shown*, and every interesting question the
modder asked lives exactly on the seam between them.

Scope:

| # | Ask | Verdict |
|---|---|---|
| 1 | Queue lock / hold | primitive may already exist (§5) |
| 2 | Several buildings at once | two different asks (§6) |
| 3 | Several vehicles at once | only N-fronts is new (§6) |
| 4 | New factory type / exit + output control | **BuildCat is the answer** (§7) |
| 5 | 3-column cameo grid | arithmetic, not architecture (§9) |
| 6 | Floating cameo panels | `ControlClass`, per PR #1379's post-mortem (§9) |
| 7 | Exclusive SW sidebar extensions | layer on merged PR #1384 (§8) |
| 8 | >4 tabs | 56 mechanical sites (§9) |
| 9 | Finished building → limbo, no placement | ✅ shipped, `LimboOnComplete=` (§7b) |
| 10 | Buildings tagged to add queues / amplify vs own-queue | `Factory.Mode=` (§7c) |
| 11 | Buildable with no ConYard / outside every build category | `AlwaysAvailable=` + `UseBuildQueue=` (§7e) |

Addresses ✔ (confirmed) / ⚠ (unverified), per `SpawnExt/DESIGN.md` convention.

---

## 0. Revision history — what each audit overturned

**Rev 2** (registry + Antares source audit) killed three rev-1 claims: the production
channel already exists in Antares; parallel factories already ship for the AI; the
4-tab cap is arithmetic, not a wall.

**Rev 3** merges SidebarExt and reframes ask #4 around `BuildCat`.

**Rev 3a (correction).** Rev 3 claimed the non-`Combat` `BuildCat` values were
*dormant* and that a third queue was therefore nearly free. **That was wrong**, caught
by grepping a real `rulesmd.ini` instead of reasoning from the enum:

```
36  BuildCat=Combat      23  BuildCat=Tech      6  BuildCat=Resource      5  BuildCat=Power
```

The values are **populated and in active use** (barracks, war factories, tech centres
all carry `BuildCat=Tech`). The tab/queue split is a *single hardcoded test* —
`ObjectTypeClass::IsBuildCat5` `0x5004E0`, literally "is this BuildCat 5 (Combat)?" —
which is why Antares adds exactly one extra pass, `Update_FactoriesQueues(BuildingType,
isNaval, Combat)`, at `0x509140`.

**✅ CONFIRMED IN-GAME (2026-09-08).** GAPILL flipped from `BuildCat=Combat` to
`BuildCat=Power` **moved from the Defense tab to the Buildings tab**, exactly as rev 3a
predicted. ModEnc agrees: *"BuildCat=Combat makes the structure buildable from the
defense tab; all other options keep the building on the main structure tab."*

**Rev 3b — a second correction.** Rev 3a asserted the non-`Combat` values drive
**AI base-planning priority**. That was another unearned inference; there is no evidence
for it. Neither Antares nor Phobos reads `BuildCat` for anything but sidebar/factory
lookup, and ModEnc describes it as sidebar placement only. Withdrawn.

What the frameworks actually do with it (✔ source-read):

- **The queue key is effectively binary.** Every framework call site passes literal
  `BuildCat::DontCare` for non-defense buildings — Antares `Ext/Building/Body.cpp:68`,
  `Ext/House/Hooks.Queue.cpp:125`, `Ext/Rules/Hooks.CameoList.cpp:59`,
  `Ext/Building/Hooks.Infiltrate.cpp:110`; Phobos `Ext/Sidebar/Hooks.cpp:53-61`. Phobos
  even comments it: *"Vanilla and Ares all only hardcoded to find factory with
  BuildCat::DontCare…"* So `Tech`/`Resource`/`Power`/`Infrastructure` are looked up **as
  `DontCare`** and share its queue.
- **`DontCare` is a sentinel for "unset", and Antares rewrites it.**
  `Misc/Invalidators.cpp:191-201`: any BuildingType within TechLevel carrying
  `DontCare` is reassigned to `Combat` (if `SuperWeapon != -1 || IsBaseDefense || Wall`)
  else **`Infrastructure`**, with a parser warning. So under Antares `Infrastructure` is
  *the* default for ordinary buildings — ModEnc's "no vanilla building uses
  Infrastructure" is true of vanilla only. This also explains ModEnc's note that
  `DontCare` renders the cameo "as if the building was partly built".

> **Net for ask C.** A new `BuildCat` would be **silently normalised into the `DontCare`
> queue** by every one of those hardcoded call sites. Creating a genuine third queue
> means updating that enumerable site list *plus* generalising `IsBuildCat5` and the tab
> routing. Moderate and well-bounded — but a code change at ~8 known sites, not a config
> change. §3 difficulty stands.

Accepted INI spellings (ModEnc): `Combat`, `Infrastructure`, `Resource`, `Power`,
`Tech`, `DontCare`. The YRpp *enum identifier* is misspelled `Resoure`
(✔ `YRpp/GeneralDefinitions.h:644`) — a C++ identifier typo only; the INI string is
`Resource`, which resolves the rev-3a ⚠.

Rev 3 also corrects the rev-2 framing of the channel key. It is not a 5-slot table;
it is a **3-tuple**, and the engine says so itself (✔ `YRpp/HouseClass.h:689,692`):

```cpp
FactoryClass* GetPrimaryFactory(AbstractType absID, bool naval, BuildCat buildCat) const;  // 0x500510
void          SetPrimaryFactory(FactoryClass*, AbstractType, bool naval, BuildCat);        // 0x500850
void          Update_FactoriesQueues(AbstractType factoryOf, bool isNaval, BuildCat);      // 0x509140
```

---

## 1. The one idea — the channel key

```
   channel = ( AbstractType , isNaval , BuildCat )
```

Vanilla instantiates six channels from this key:

| AbstractType | isNaval | BuildCat | Tab | Antares ext slot |
|---|---|---|---|---|
| BuildingType | – | `DontCare` | 0 Buildings | `Factory_BuildingType` |
| BuildingType | – | `Combat` | 1 Defense | *(shares the slot above)* |
| InfantryType | – | – | 2 Infantry | `Factory_InfantryType` |
| UnitType | false | – | 3 Vehicles | `Factory_VehicleType` |
| UnitType | true | – | 3 Vehicles | `Factory_NavyType` |
| AircraftType | – | – | 3 Vehicles | `Factory_AircraftType` |

Three independent groupings, routinely conflated — **channel ≠ tab ≠ factory**:

- **Naval is its own channel, sharing the vehicle tab — and sharing the type list too.**
  Ships are declared in `[VehicleTypes]` alongside land vehicles; there is no separate
  naval list, and `WhatAmI()` returns `Unit` for both. The channel split comes purely
  from the **`Naval=` flag on the type**, which callers pass as the `bool isNaval`
  argument to `GetPrimaryFactory`/`SetPrimaryFactory`, selecting `0x53B8` over `0x53B4`
  (✔ disassembled, §7d). So: same list, same `AbstractType`, same tab — *different
  queue*, split by one flag. This is the single easiest thing in the subsystem to
  mis-observe, because nothing except that flag distinguishes the two.
- **Defenses are their own queue, sharing the ConYard.** Antares comments this at
  `0x509140`; the vanilla test is `ObjectTypeClass::IsBuildCat5` @ `0x5004E0` (✔
  `YRpp/ObjectTypeClass.h:45`) — literally "is this BuildCat 5 (Combat)?", i.e. the
  Buildings-vs-Defense queue split is a **one-value comparison**.
- **A channel names one factory building**, but the finished object may exit from a
  different one (`FindAlternateKickout` `0x4444E2`).

Everything the modder asked for is a modification of one field of this key, or of the
mapping from the key to a tab.

---

## 2. Tab routing — the chokepoint

```cpp
static int __fastcall GetObjectTabIdx(AbstractType abs, int idxType, int unused);        // 0x6ABC60
static int __fastcall GetObjectTabIdx(AbstractType abs, BuildCat buildCat, bool isNaval);// 0x6ABCD0
bool AddCameo(AbstractType absType, int idxType);                                        // 0x6A6300
```
(✔ `YRpp/SidebarClass.h:88,103,107`.)

**The second overload takes the entire channel key and returns a tab index.** It is
the single function that decides "which strip does this cameo live in", and it is the
place every tab question below is answered.

**All of `0x6ABC60`, `0x6ABCD0`, `0x6A6300`, `0x500510`, `0x500850`, `0x5004E0` are
hooked by no framework in the registry** (✔) — Antares, Phobos, Kratos, Ares,
AggressiveStance, CnCNet-Spawner all leave them alone. This is the cleanest ground in
the whole design.

> Phobos *does* hook tab-index sites, but a different set — `0x6A5F6E`, `0x6A614D`,
> `0x6A633D`, `0x6ABC9D`, all in `src/Ext/SWType/Hooks.cpp`, i.e. superweapon routing
> from the merged SW-sidebar work. It never touches the general `GetObjectTabIdx`.

---

## 3. Answers to the eight questions

Difficulty is **relative to this codebase**, assuming P0's probe has run.

| # | Question | Verdict | Difficulty |
|---|---|---|---|
| A | Several build queues for the same factory type | **Already exists** (Buildings + Defense share the ConYard). A *third* is §7. | — |
| B | Two buildings at once *in one tab* | N fronts per channel. The core engine change. | **Hard** |
| C | Third factory type: own queue, rests in either tab, doesn't consume Buildings/Defense | Generalise the single `IsBuildCat5` split (§0 rev 3a — *not* a dormant enum slot) | **Moderate; most tractable big ask** |
| D | SW sidebar on the opposite side of screen | Layer on merged #1384's `SWSidebarClass`. | Medium |
| E | Prerequisites gating exclusive-sidebar display | #1384 has a *boolean*; upgrade to a predicate. | Medium |
| F | Buildings (not just SWs) in the exclusive sidebar | It's a parallel panel with its own item list. | Medium |
| G | Same structure in **both** Buildings and Defense tab | `GetObjectTabIdx` returns *one* tab — needs multi-placement. | Medium-hard |
| H | Per-placement policy: build both at once vs grey out | Same mechanism as B, scoped per placement. | **Hard** (needs B) |

### C — the third factory type (do this first)

The pattern already works: `[BLDG] BuildCat=Combat` puts a structure in the Defense
tab with its own queue, still built by the ConYard. `BuildCat` is parsed as a normal
INI field (`BuildingTypeClass::BuildCat`, `INI_READ(BuildCat, 0x475060)` ✔) and the
Buildings-vs-Defense split is the single comparison at `IsBuildCat5` `0x5004E0`.

**But setting some other `BuildCat` does not by itself make a queue** — see §0 rev 3a.
`Tech`/`Resource`/`Power` are already in use for AI base planning and all share the
Buildings queue. The work below is what actually creates a third one.

So a third queue needs:
1. **A channel slot** — Antares' `HouseExt` has five `BuildingClass*` fields; add one
   per new BuildCat (or replace the five with a keyed map).
2. **Tab routing** — `GetObjectTabIdx` `0x6ABCD0` returns the tab for the new BuildCat.
   *"Rests in either tab"* is exactly this function's return value, so it is an INI
   choice, not new code: `BuildCat.TabIndex=`.
3. **The queue-update pass** — Antares already adds a second `Update_FactoriesQueues`
   call for `Combat` at `0x509140`; a third BuildCat needs a third call, same shape.
4. **Generalise the `IsBuildCat5` split** so "is this the defense queue" becomes "which
   queue is this".

**✅ Settled in-game (2026-09-08).** GAPILL at `BuildCat=Power` moved to the Buildings
tab and shares its queue — only `Combat` is special. The remaining work is the
enumerable call-site list in §0 rev 3b: ~8 hardcoded `GetPrimaryFactory(…, DontCare)`
sites across Antares and Phobos, plus `IsBuildCat5` `0x5004E0` and the tab routing at
`GetObjectTabIdx` `0x6ABCD0`.

### G — one structure in two tabs

`AddCameo(absType, idxType)` adds a type to the strip that `GetObjectTabIdx` chooses,
so a type currently has exactly one home. Two tabs needs:
- **Multi-placement**: call `AddCameo` once per intended tab. Nothing structural
  forbids it — a strip holds a cameo list, and the merged SW sidebar already shows the
  same SW in both its panel and a normal tab. Precedent: PR #1387 gave SWs an explicit
  `TabIndex=`, proving per-item tab assignment is overridable.
- **Placement identity**: with a type in two strips, *"which placement did the player
  click"* stops being derivable from the type alone. This is the real work, and it is
  the same problem as H.
- **Per-placement policy** (H): shared queue → clicking either builds one and greys the
  other; independent → two fronts, which is B. So **H is not a separate feature; it is
  B scoped to a placement.**
- **Per-placement prerequisites** (country etc.): a predicate per placement, which is
  exactly [[prerequisiteext-project]]'s Requirement primitive. **Don't build a second
  prerequisite system here** — consume that one.

### D/E/F — the exclusive sidebar

PR **#1384** merged 2025-06-05 (`SWSidebarClass` / `SWColumnClass`, `ControlClass`-based).
#1383 and #1379 are its closed predecessors. **#1379's closure is the binding design
lesson: reviewers rejected a custom UI class and demanded `ControlClass`/`SelectClass`,
and flagged missing scrolling.** Any new panel here must be `ControlClass`-derived and
scrollable from day one.

- **D** (opposite side): #1384 already has `SWSidebar.LeftOffset`; arbitrary placement
  is an extension of existing positioning, not new machinery.
- **E** (prereq-gated display): #1384 exposes a boolean `AllowInExclusiveSidebar`.
  Replacing a boolean with a Requirement predicate is small — again, PrerequisiteExt.
- **F** (buildings in it): the panel keeps its own item list, so the change is
  broadening the item source from SW-only to any TechnoType, plus click routing into
  the normal production path (`SidebarClass_ProcessCameoClick_*` `0x6AAEDF` / `0x6AAF9D`
  / `0x6AB312`).

---

## 4. Sync — already decided by the engine

Antares at `0x6AB773` implements shift-click-queues-five by adding the **same
`EventClass` five times** to `Networking::AddEvent` (✔ read):

```cpp
auto count = 4 * (modifiers & 1) | 1;
while(count--) { Networking::AddEvent(pEvent); }
```

Production input is a **network event**, never a local state write. Any hold toggle,
multi-placement click, or new-panel button must dispatch through `EventClass::OutList`
or it desyncs ([[kratos-rng-desync-rootcause]]). `modifiers & 1` is shift only, so other
modifier bits are free. `0x6AB773` and `0x6AB312` are Antares-only.

---

## 5. Ask #1 — queue hold

> **✅ Q1/Q2 ANSWERED IN-GAME 2026-09-19 — the hopeful reading was wrong.**
>
> **Q1 — `IsManual` is useless as a player-hold flag.** It read **`1` in all 7,337
> observed samples**, never 0: on AI factories, and on factories that were not suspended
> at all. YRpp's comment (*"whether the current suspension state was caused by the
> player"*) does not describe runtime behaviour. **Feature #1 is therefore not a rename
> of existing engine state** — it needs its own per-slot hold flag.
>
> **Q2 — a suspended item BLOCKS the whole queue.** Vanilla does not skip past a paused
> item; everything behind it stops.
>
> Two consequences, both of which make #1 *more* valuable rather than less:
> - §C2's claim that hold and concurrency are "orthogonal by construction" was
>   optimistic. They are orthogonal **by design intent**, but since vanilla blocks, the
>   skip-held-items behaviour must be actively implemented. Not free.
> - **This is precisely why the 99999-count hack was useless** — and why
>   `LimboOnComplete` (§7b) earns its keep: a finished building sits at `Susp=1` and
>   **blocks the buildings queue until placed.** "Waiting for you to place it" is not a
>   cosmetic annoyance, it is a stall.
>
> The *mechanism* still works — a right-click suspended a power plant mid-build at
> `prog=28`. It is the *flag* that carries no information, not the *call*. And none of
> `Suspend`/`Unsuspend`/`StartProduction`/`CompletedProduction` is hooked by any
> framework (✔), so the ground is still clear.

INI: `Queue.Holdable=no`, `Queue.HoldWhenComplete=no` (both opt-in). Toggle = a modifier
click at the `0x6AB773` idiom, dispatched as an `EventClass` (§4).

**What #1 must now actually do** (revised by Q1/Q2):
1. Carry **our own** hold flag per queue slot — `IsManual` cannot be reused.
2. **Break the block.** The real work is not marking an item held, it is making the
   queue *advance past* a held item, which vanilla refuses to do. That is the same
   engine change as ask B (N fronts), so §5 and P8 share machinery.
3. Park completed items without stalling the channel — the `LimboOnComplete` case
   generalised to any category.

---

## 6. Asks #2/#3 — concurrency

Antares at `0x4502F4` restricts a house to one active factory per channel **only** when
`H->Production && !AllowParallelAIQueues`. `AllowParallelAIQueues` defaults **true**, and
`H->Production` is the AI flag (✔ `YRpp/HouseClass.h:823`: *"AI production has begun"*).

So *"use another idle war factory / take turns exiting"* is **already solved** — vanilla
plus `FindAlternateKickout` `0x4444E2`. **The only new work is N fronts advancing in one
channel** (one `FactoryClass` advances one object). Per-house cap, building-raisable,
recomputed on gain/loss, deterministic credit drain.

`0x4502F4` and `0x4CA07A` are **three-way conflicts** (Antares + Ares + Phobos) — the
live collision being Antares + Phobos. Treat as occupied.

**Parity gap** (✔): Antares implements only the global `AllowParallelAIQueues`; the
per-category `ForbidParallelAIQueues.*` attributed to classic Ares have no
implementation (`grep` → nothing). ⚠ Whether classic Ares really has them is unverified.

---

## 7. Ask #4 — factory types, exit, output

The factory-*type* half is **already Antares**: `Factory.ExplicitOnly=yes` + `BuiltAt=`
(the Kennel pattern). Use `WeaponsFactory=`/`*Barracks=` (not `Factory=`) for walk-out.
Do not reimplement.

**⚠ Dead-code trap.** Antares fully replaces `ObjectTypeClass::FindFactory` at `0x5F7900`
— its handler calls `HouseExt::HasFactory`, writes `EAX`, `return 0x5F7A89`, so the
vanilla body **never runs**. Anything hooked inside is dead whenever Antares is loaded
(same class as `CanBuild` `0x4F7870`). **Extend `HouseExt::HasFactory`.**

New code, each layered on the vanilla search with fallback:
- **Cross-category exit** `[TYPE] ExitFrom=<buildings>` — infantry ⇄ war factory. The
  kick-out sites `0x444119`/`0x444131`/`0x44531F`/`0x443CCA` are **triple-hooked** and
  **read the house from different registers** (`ESI->Owner`/`EAX`/`EAX`/`EDX`) — copying
  a handler between them is a silent wrong-pointer bug.
- **Output cell** — precedent: Phobos `BuildingClass_ExitObject_BarracksExitCell` `0x444B83`.
- **SW-from-factory** — ⚠ largest unknown; dive the SW launch path first.

---

## 7b. Ask #9 — "click a finished building → straight to limbo, no placement"

Feasible. Both halves exist; the hard part is neither of them.

**The payload already exists, in Phobos.** `LimboCreate` (`src/Ext/SWType/FireSuperWeapon.cpp:79`)
is the canonical routine for a building that *fully counts* but is never placed:

```cpp
pBuilding->InLimbo = false;  pBuilding->IsAlive = true;  pBuilding->IsOnMap = true;
pBuilding->DiscoveredBy(pOwner);
pOwner->RegisterGain(pBuilding, false);
pOwner->RecheckTechTree = true;  pOwner->RecheckPower = true;
pOwner->Buildings.AddItem(pBuilding);
if (pType->ConstructionYard) pOwner->ConYards.AddItem(pBuilding);
if (pType->SecretLab)        pOwner->SecretLabs.AddItem(pBuilding);
if (pType->FactoryPlant)   { pOwner->FactoryPlants.AddItem(pBuilding); pOwner->CalculateCostMultipliers(); }
```

> **⚠ Naming trap:** it sets `InLimbo = **false**`. Phobos "LimboDelivery" does not mean
> `InLimbo`; it means *registered with the house, absent from the map*. It grants
> prerequisites, power, superweapons, FactoryPlant discounts and ConYard status while
> occupying no cells. Comments confirm `BuildingClass::Place` is already called inside
> `DiscoveredBy`, which is what books Ore Purifier / self-heal counters.

**The trigger point.** Placement mode is `DisplayClass::CurrentBuilding` /
`CurrentBuildingType` ("Building we're currently placing", ✔ `YRpp/DisplayClass.h:108`),
set from the completed-building cameo click inside `SelectClass::Action`
(`~0x6AB600`–`0x6AB800`). The completed/ready state is `FactoryClass::IsSuspended` with
`Object` set. Confirmed nearby landmarks in that function (✔ disassembled):
`0x6AB619 → GetPrimaryFactory(0x500510)`, `0x6AB656 → ShouldDisableCameo(0x50B370)`,
and at `0x6AB67F` a `cmp $0x7,%ebp` (BuildingType) guarding the vanilla
"one building at a time" rejection at `0x6AB689` — the site Phobos skips to `0x6AB6CE`
for `BuildingProductionQueue`.

**⚠ The actual hard part: this is synced state reached from a local input.**
Entering placement mode is *client-local* (`CurrentBuilding` is UI state); the building
only becomes real when placement emits a network event. Limbo-delivery has no cell and
no placement step, so it would create a `BuildingClass` **directly** — and per §4 that
must travel as an `EventClass`, or clients diverge and the game desyncs.

Phobos solves exactly this with **`EventExt`** (`src/Ext/Event/Body.h`): a custom network
event carrying a `DataBuffer`, with `AddEvent()` / `RespondEvent()` and an interop export
`EventExt_AddEvent`. That is the correct transport, but calling a co-loaded framework's
private event machinery from this DLL is fragile — needs its own decision (§11.7).

**Gameplay consequence worth deciding before coding:** a limbo-delivered building is
invisible, unselectable, unsellable and undestroyable. Phobos can only remove one via
`LimboKill` by explicit ID. So "straight to limbo" means **permanent and irreversible**,
not "stored for later placement."

Phasing: this is P4-class (placement identity / policy), and it should be INI-opt-in
per BuildingType, never global default.

---

## 7c. Ask #10 — what a producing building *is* to its channel (`Factory.Mode`)

Added to scope 2026-09-19. The insight: "a second war factory" is currently one fixed
behaviour, and it should be **a choice of four**.

| Mode | What a 2nd building of the same type does | Status |
|---|---|---|
| **Amplify** | joins the existing queue and makes it *faster* | ✅ vanilla default |
| **Inert** | contributes neither speed nor a queue | ✅ Phobos `ExcludeFromMultipleFactoryBonus=yes` |
| **Queue** | **its own independent queue** for the same types — parallel, not faster | ❌ **new** |
| **Category** | a new factory *type* with its own buildable set + tab (ask C, §3) | ❌ new |

Phobos already implements one value of this mode. That is good evidence the framing is
right: we are completing an existing axis, not inventing one.

### Vanilla machinery underneath (✔ verified)
- `RulesClass::MultipleFactory` (float, ✔ `YRpp/RulesClass.h:424`) — the per-extra-factory
  speed bonus. **This is what "Amplify" *is*.**
- `HouseClass::GetFactoryCount` `0x500910` — counts factories feeding that bonus
  (Phobos hooks it).
- `HouseClass_ExcludeFromMultipleFactoryBonus` `0x4FF9C9` / `0x4FFA99` — Phobos's
  opt-out, maintaining its own `UpdateNonMFBFactoryCounts` tally
  (✔ `Phobos/src/Ext/House/Hooks.cpp:399-416`).

> **⚠ Coexistence.** `Inert` is **Phobos's** tag, not ours — do not reimplement it.
> And `Mode=Queue` must *also* exclude the building from the speed bonus (it is not
> amplifying), so BuildQueueExt has to cooperate with that same tally rather than
> double-count. Get this wrong and a `Queue` building silently grants a queue **and** a
> speed bonus.

### Proposed INI surface
```ini
[SOMEBUILDING]
Factory.Mode=Amplify       ; Amplify (default) | Inert | Queue | Category
Factory.QueueCount=1       ; Mode=Queue: how many independent queues this grants
Factory.Category=          ; Mode=Category: which category it produces for
```
Defaulting to `Amplify` keeps every existing mod byte-identical in behaviour.

### The consequence: queue count becomes dynamic
`Mode=Queue` means the number of queues is **a function of how many such buildings the
house owns**, not a fixed set of categories. Six refineries with `Mode=Queue` means six
queues. No fixed slot set can absorb that.

> **Therefore BuildQueueExt must own a per-house dynamic channel table**, keyed by
> `(AbstractType, isNaval, BuildCat, queueIndex)`, with `GetPrimaryFactory` `0x500510`
> and `SetPrimaryFactory` `0x500850` routed through it. The vanilla slots (§7d) stay as
> the compatibility path for the vanilla channels; ours backs everything beyond. This is
> the same "one owner of the channel table" call as §1 — now **mandatory** rather than
> optional.

**Sidebar consequence.** N parallel queues for one type means N progress states behind
**one cameo**. That is the same placement-identity problem as ask G (§3), so G and this
share a solution — and both inherit the button-pool trap (§9).

---

## 7e. Ask #11 — `AlwaysAvailable` + `UseBuildQueue=no`

Added to scope 2026-09-20. Both are feasible, and **they are the same feature wearing
two hats** — which is the useful finding, because it means one mechanism serves both.

### What actually gates a building on owning a ConYard (✔ source-read)

Not the prerequisite. `HouseExt::HasFactory` (Antares `Ext/House/Body.cpp:377`) walks
`pHouse->Buildings` and keeps only buildings where:

```cpp
pType->Factory == abs          // for a BuildingType, that IS the Construction Yard
&& pType->InOwners(bitsOwners)
&& pType->Naval == (abs == UnitType && isNaval)
&& pExt->CanBeBuiltAt(pType)   // Ares/Antares BuiltAt / Factory.ExplicitOnly
```
…and returns `NoFactory, nullptr` when none match.

So the ConYard is gating things **twice**, and only one of them matters:
- As a **prerequisite** (`Prerequisite=GACNST`) — *already* removable in INI. Not a
  feature.
- As the **factory** — structural. With no ConYard there is no `FactoryClass` to
  produce from, so the cameo is dead regardless of prerequisites. **This is the real
  blocker and the actual ask.**

### Why both tags collapse into one mechanism

`AlwaysAvailable=yes` needs a producer that is not a building.
`UseBuildQueue=no` needs a queue that is not the shared category queue.

Both are *"a channel owned by the house rather than by a factory building"* — and that
is exactly what the §7c dynamic channel table already stores. A `queueIndex >= 1` entry
that is simply **not backed by a `BuildingClass`** satisfies both asks at once.

```ini
[SOMEBUILDING]
AlwaysAvailable=yes     ; needs no factory building to be buildable
UseBuildQueue=no        ; gets a private channel; never blocks a category
```

> **Pairs naturally with `LimboOnComplete=yes` (§7b).** A building with no ConYard still
> has to be *placed* when it finishes, and placement is where the queue stalls (Q2).
> `AlwaysAvailable` + `UseBuildQueue=no` + `LimboOnComplete` gives a structure that is
> always buildable, never blocks anything, and materialises without a cursor — which is
> almost certainly the combination the "utilized well" remark is reaching for.

### ✅ The boundary with PrerequisiteExt — settled 2026-09-20

Decision: **the two DLLs stay separate, and they share an INI contract, not code.**
Merging was considered and rejected — PrerequisiteExt's value is an engine-independent
resolver with 85 host-run unit tests, and folding 11 hooks of live engine state into it
would destroy exactly that property. See [[prerequisiteext-project]].

**The split is cleaner than first assessed, because of one verified fact:**

> `HouseClass::ShouldDisableCameo` `0x50B370` — Antares' full replacement — resolves the
> factory via **`pThis->GetPrimaryFactory(abs, pType->Naval, BuildCat::DontCare)`**
> (✔ `Antares-src/src/Ext/House/Hooks.Queue.cpp:124`).

`GetPrimaryFactory` `0x500510` is the hook BuildQueueExt **already owns** (§7d, P2a). So
returning a house-owned channel from that one function makes the cameo live, the sidebar
strip resolve, and production route — **all three from a single seat.** No separate
`0x50B370` hook is needed, and more importantly:

> **BuildQueueExt never touches `0x4F8361`.** `HasFactory` *calls* `CanBuild`, not the
> reverse (✔ `Ext/House/Body.cpp:382`), so the factory requirement and the prerequisite
> verdict are independent. The promote-vs-veto race that forced the SpawnExt fold simply
> never arises here.

**Division of labour:**

| Half | Owner | Mechanism |
|---|---|---|
| "May it be built?" — prerequisites, tech, house scope | **PrerequisiteExt** | its existing `Prerequisite.*` container family; nothing new required |
| "What produces it?" — factory presence, queue, routing | **BuildQueueExt** | a house-owned channel returned from `GetPrimaryFactory` |

They compose **without linking**, the same way spawn gating composes with SpawnExt: each
reads INI and does its half, and the engine joins them. Dropping `GACNST` from
`Prerequisite=` is a plain INI edit — no DLL is involved in the prerequisite half at all
unless the modder wants *conditional* availability, which PrereqExt's existing tags
already express.

### ⚠ Remaining traps

1. **Dead-code trap.** Antares fully replaces `ObjectTypeClass::FindFactory` `0x5F7900`;
   hooks inside that body are dead whenever Antares is loaded. Our seat is
   `GetPrimaryFactory`, which is unaffected — but anything added around factory *lookup*
   must respect it.
2. **Sync.** A house-owned channel is Logical state (§4): it must serialise and be
   identical on every client. This is open decision #4 arriving early, and it is the
   real cost of the feature.
3. **A channel with no building has no exit cell.** Whatever it produces has nowhere to
   come out — which is why `LimboOnComplete=yes` is the natural partner rather than an
   optional extra.

### Scope call

Entirely **ours**, and it sits directly on the P2 table — arguably its cleanest consumer,
since it needs a channel with no building attached and nothing else. Phased as P4b,
beside `Factory.Mode` (same "channel without a factory building" primitive).

---

## 7d. The primary-factory slots — and why the four chokepoints are the only lever

`HouseClass` carries **nine** `FactoryClass*` primaries (✔ `YRpp/HouseClass.h:928-936`):

```cpp
Primary_ForAircraft;  Primary_ForInfantry;  Primary_ForVehicles;
Primary_ForShips;     Primary_ForBuildings;
Primary_Unused1;      Primary_Unused2;      Primary_Unused3;   // <- wedged here
Primary_ForDefenses;
```

**✅ Q5 ANSWERED 2026-09-19 — and the answer is no.** Settled by disassembling both
accessors rather than waiting for a game run. Each dispatches through a 40-entry byte
table indexed by `AbstractType - 1` into a **five-case** jump table, and the verified
slot map is:

| offset | field | selected when |
|---|---|---|
| `0x53AC` | `Primary_ForAircraft` | `AircraftType` |
| `0x53B0` | `Primary_ForInfantry` | `InfantryType` |
| `0x53B4` | `Primary_ForVehicles` | `UnitType`, naval = false |
| `0x53B8` | `Primary_ForShips` | `UnitType`, naval = true |
| `0x53BC` | `Primary_ForBuildings` | `BuildingType`, cat ≠ `Combat` |
| `0x53C0`–`0x53C8` | `Primary_Unused1/2/3` | **never read, never written** |
| `0x53CC` | `Primary_ForDefenses` | `BuildingType`, cat == `Combat` |

No case in either jump table touches `0x53C0`/`0x53C4`/`0x53C8`. **The three spare slots
are structurally unreachable through the engine's own accessors** — dead storage, not
spare capacity. Their suggestive mid-array position meant nothing. They buy nothing over
our own table, so nothing should plan around them.

Calling conventions (✔ both):
```
GetPrimaryFactory 0x500510  __thiscall, ret 0x0C
  ECX = HouseClass*, [ESP+4] = AbstractType, [ESP+8] = bool naval,
  [ESP+0xC] = BuildCat  → EAX = FactoryClass*
SetPrimaryFactory 0x500850  __thiscall, ret 0x10
  ECX = HouseClass*, [ESP+4] = FactoryClass*, [ESP+8] = AbstractType,
  [ESP+0xC] = bool naval, [ESP+0x10] = BuildCat
```
Stolen bytes at both: `mov`+`dec` = exactly 5, ending on a real instruction boundary,
no relative branch — so `return 0` is safe at these two sites specifically.

**`BuildingClass::Factory` (✔ `YRpp/BuildingClass.h:267`) — every producing building
already owns a `FactoryClass`.** Multiple instances already coexist. So the bottleneck
was never "one item per factory type": it is that the house designates *one primary per
channel* and the sidebar drives only that one. That reframes ask B from a redesign into
a **routing change**, and it is exactly the seam `Mode=Queue` needs.

**Why hooks and not source edits.** The ~8 hardcoded `BuildCat::DontCare` call sites
(§0 rev 3b) live inside *compiled* `Antares.dll` / `Phobos.dll` and cannot be edited.
But every one funnels through game functions — `GetPrimaryFactory` `0x500510`,
`SetPrimaryFactory` `0x500850`, `IsBuildCat5` `0x5004E0`, `GetObjectTabIdx` `0x6ABCD0` —
and **all four are hooked by no framework in the registry** (✔). That is simultaneously
the only available lever and unusually clean ground.

---

## 8. Sidebar prior art (merged SidebarExt)

| PR | State | What |
|---|---|---|
| **#1384** | **MERGED** 2025-06-05 | The real Exclusive SW Sidebar, `ControlClass`-based. The base. |
| #1383 / #1379 | closed | Predecessors. #1379 rejected for a custom UI class + no scrolling. |
| #1703/#1711/#1815 | merged | SWSidebar follow-ups: rectangular arrangement, duplicate SWs, tooltips |
| **#1387** | merged | `[SOMESW] TabIndex=` — per-item tab assignment. Precedent for G. |
| **#1435** | merged 2026-07-26 | `SetTabBySelectingFactory` + per-building `SetTabBySelecting` |
| **#192** | merged 2021 | `CameoPriority=` sorting; sorts *within* hardcoded category groups |
| #1522 | closed | Sidebar scroll-action change — unclaimed |

**Phobos never touches the strip/tab/column structure.** Its `src/Ext/Sidebar/` is extra
SHPs, producing-progress drawing, save/load, and the SW sidebar's *parallel* buttons
outside the strip system. The 2-column grid, 4-tab cap, and button pool are unclaimed.
Shared init contact points: `0x6A5082` (`InitClear`), `0x6A5839` (`InitIO`).

---

## 9. Sidebar engine structure (from SidebarExt's disassembly)

`SidebarClass::Instance` @ `0x87F7E8`. `Tabs[4]` at instance offset `0x1544`,
`sizeof(StripClass) = 0xF94`; cross-check `0x1544 + 4*0xF94 = 0x5394` = YRpp's
`unknown_5394` ✔. 75-cameo cap per strip (`push $0x4b` @ `0x6A4E88`, `0x6A4FBB`).

| Global | Value | Meaning |
|---|---|---|
| `0x886F94` | 158 | sidebar width |
| `0xB0B4FC` | 63 GDI / 64 NOD | column pitch |
| `0xB0B500` | 50 | row pitch (Phobos re-forces @ `0x6A51E9`) |
| `0xB0B4F8` | 227 GDI | strip top Y |

**The engine already has a column-count switch**: every column site is a two-way branch
on `CurrentPlayer == Observer` (observer 1 column, else 2) — explicit at `0x6A8BAF`/
`0x6A8BC2`. Build on that seam with one `GetColumnCount()` rather than 17 byte patches.

### ⚠ The button-pool overflow trap — inherited by any cameo feature

Pool @ `0xB07E80`, stride `0x38`, **60 per tab**, 240 total, ending `0xB0B300`.
**`ToggleRepairButton` sits at `0xB0B3A0`** — under 3 slots of slack. Row count
`(H-260)/50` is **never clamped**.

| Columns | Overflows at | Screen height |
|---|---|---|
| 2 (vanilla) | rows ≥ 31 | ≥ 1810 px — **vanilla is already broken at 4K** |
| 3 | rows ≥ 21 | ≥ 1310 px — **breaks at 1440p** |

Failure is silent before it is loud: tabs bleed into each other's buttons, *then* the
repair toggle dies. **Relocate the pool to a heap array before touching columns.** Same
bug class as [[mapsizeext-astar-pool-overflow]]. Any BuildQueueExt feature that adds
cameos or buttons — including G's multi-placement — inherits this.

Census: `Tabs[]` 26 sites, button pool 27, `TabButtons` 30 (~83 total, all within
`0x6A4C00`–`0x6AC800`). ⚠ The per-tab stride is encoded three ways (`imul $0x3c`,
factored, and folded as `add $0xd20`) — re-derive by data-flow, not grep.

---

## 10. Phasing

Re-ordered 2026-09-19 around §7c: **the channel table is now the trunk.** `Mode=Queue`,
ask C and ask B all reduce to routing on top of one owned table, so building it first
turns three separate engine problems into three mappings.

1. **P0 — probe.** ✅ written, deployed. Answers Q1–Q4.
   *Two false starts already: the DLL wasn't in the live injection list, then the config
   switched itself off on the map-INI pass. Both fixed; awaiting a clean run.*
   **Q5 is already closed** — answered from the disassembly (§7d), no run needed.
2. **P1 — ✅ DONE: the BuildCat experiment.** Confirmed in-game — only `Combat`
   separates. Scope of ask C is now the enumerable site list in §0 rev 3b.
3. **P2 — the channel table. The trunk; everything below depends on it.**
   - **P2a ✅ SHIPPED** (shadow mode). Table + both hooks, observing only —
     every handler returns 0, nothing rerouted. Records on `SetPrimaryFactory`,
     cross-checks our key derivation against the engine's own slot on
     `GetPrimaryFactory`, logs only genuine disagreements (rate-limited to 40).
     Opt-in: `[BuildQueueExt] ChannelTable=yes`. **Needs a game run to confirm zero
     mismatches before P2b.**
   - **P2b — take authority.** Only once the shadow is clean: serve
     `queueIndex >= 1` from our table and return an explicit address instead of 0.
     Vanilla keys keep falling through to the engine's slots, so behaviour without
     any `Factory.Mode` tag stays byte-identical.
4. **P3 — `Factory.Mode` (ask #10, §7c).** `Queue` and `Category` on top of P2;
   `Amplify`/`Inert` already exist. Must cooperate with Phobos's
   `UpdateNonMFBFactoryCounts` so a `Queue` building doesn't also amplify.
5. **P4 — ask C: new factory type.** Now just `Mode=Category` + generalised
   `IsBuildCat5` `0x5004E0` + tab routing `GetObjectTabIdx` `0x6ABCD0`.
   **P4b — ask #11 (§7e):** `AlwaysAvailable` + `UseBuildQueue=no`, i.e. a channel with
   no backing factory building. Same primitive as `Mode=Queue`, so it rides alongside;
   the prerequisite half is deferred to [[prerequisiteext-project]].
6. **P5 — ask #1 hold** (only if the probe says `Suspend(manual)` isn't already it).
7. **P6 — sidebar structure: pool relocation → 3 columns → N tabs.** Pool relocation
   gates anything that adds cameos, and fixes vanilla's 4K bug as a side effect.
8. **P7 — asks G/H: multi-placement + per-placement policy.** Needs P6 for the pool and
   P2 for the queue identity; shares its solution with §7c's "N queues, one cameo".
9. **P8 — asks B/#2/#3: N fronts *within* one queue.** Distinct from `Mode=Queue`
   (which is N queues). The main sync surface.
10. **P9 — asks D/E/F: exclusive-sidebar extensions**, layered on #1384.
11. **P10 — ask #4 exit/output**, then SW-from-factory last.

Prerequisite predicates throughout are consumed from [[prerequisiteext-project]], not
rebuilt.

---

## 11. Open decisions

1. **Name.** Scope is now production + sidebar; `BuildQueueExt` undersells it.
   `ProductionExt`? `SidebarExt`? Repo currently `BuildQueueExt` with code + submodules,
   so renaming costs a repo move.
2. ~~**Is the queue machinery general over `BuildCat`?**~~ **CLOSED 2026-09-08** —
   no. Only `Combat` separates; verified in-game (§0 rev 3b).
3. ~~**Channel storage** — extend Antares' five fields, or a map?~~ **CLOSED
   2026-09-19 (§7c)** — own a dynamic per-house table keyed by
   `(AbstractType, isNaval, BuildCat, queueIndex)`. `Mode=Queue` makes the count
   dynamic, so no fixed slot set works.
4. **Save/load of that table** — the vanilla nine primaries are serialised with the
   house; ours will not be. Follows the [[savegame-stream]] boundaries.
5. **`Mode=Queue` vs the multiple-factory bonus** — confirm a `Queue` building is
   excluded from `MultipleFactory` via Phobos's existing `UpdateNonMFBFactoryCounts`
   tally rather than a second, competing mechanism.
6. **Does `Factory.QueueCount>1` make sense?** Or is one queue per building the honest
   model? Several queues from a single building has no vanilla analogue.
7. **Placement identity** for G/H and for "N queues, one cameo" (§7c) — index into a
   per-house queue list vs a synthetic key. Determines the `EventClass` payload.
8. **Hold modifier key** — shift is taken by queue-5.
9. **Concurrency cap shape** (ask B, N fronts *within* a queue) — flat per-house vs
   per-category, noting naval is its own channel.
