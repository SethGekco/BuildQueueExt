# BuildQueueExt — Design

A production / build-queue subsystem for Red Alert 2: Yuri's Revenge, built as a
standalone Syringe DLL that **coexists with Phobos** and is built against
**Antares** (not classic Ares — see [[antares-replaces-ares]]).

Four asks, all touching the same engine state — the per-house production queues:

- **#1 Queue lock / hold.** Park a queued (or completed) item indefinitely, cameo
  showing a hold glyph, instead of the guessed 99999-build-count hack.
- **#2 Multiple simultaneous constructions**, capped, cap raisable by a building.
- **#3 Multiple vehicles building at once**, taking turns exiting one factory or
  using another idle one.
- **#4 New factory types + exit/output control** — where a factory outputs, a tag
  letting infantry exit a war factory (or vice-versa), superweapon delivery.

Status: **design draft, revision 2.** No code. Revision 2 followed an audit of the
YR-Hook-Encyclopedia registry and the Antares source, which **overturned three
conclusions of revision 1** — see §0.

Addresses are ✔ (confirmed) or ⚠ (unverified) per `SpawnExt/DESIGN.md` convention.

---

## 0. What the audit changed (read this first)

Revision 1 was written from a framework-docs survey. Checking the registry and
Antares source per [[hook-encyclopedia-workflow]] corrected it on three points:

1. **The "production channel" primitive already exists — I don't get to invent
   it.** Antares' `HouseExt::ExtData` already carries exactly five channel slots
   (§1). Rev 1 proposed this as a new abstraction; it is in fact the shape of
   existing code, which is a validation but also means BuildQueueExt must *extend
   Antares' table*, not introduce a parallel one.
2. **Parallel factories already ship, for the AI.** `AllowParallelAIQueues`
   (default **true**) already governs whether a house may run several factories of
   one category at once. Rev 1 scoped #2/#3 as "raise concurrency from 1" — wrong
   framing: for AI houses that already happens, and the real gap is the *human*
   player and the *per-item* front (§3).
3. **The 4-tab cap is not a wall, and tabs are not this DLL's territory.** The
   research doc concluded a 5th tab needs deep new engine work. **SidebarExt**
   (sibling DLL, decided 2026-08-20) has since disassembled the region and found
   the 4-tab and 2-column caps are *arithmetic, not architecture* (~83 mechanical
   indexing sites in one contiguous module). So a 5th tab is feasible — and it
   belongs to SidebarExt, not here (§7).

Net effect: BuildQueueExt gets **smaller and better-defined**. It owns queue
*semantics*; Antares owns the channel table; SidebarExt owns pixels.

---

## 1. The one idea — the production channel (as the engine already models it)

The state is a per-house table of **production channels**. Antares implements it
literally, at `src/Ext/House/Body.h:97-101` (✔ read):

```cpp
BuildingClass *Factory_BuildingType;
BuildingClass *Factory_InfantryType;
BuildingClass *Factory_VehicleType;
BuildingClass *Factory_NavyType;      // NOTE: naval is its own channel
BuildingClass *Factory_AircraftType;
```

Five channels, serialized and pointer-invalidated with the house. The engine's own
per-house update takes the matching key (✔ from Antares' hook at `0x509140`):

```cpp
HouseClass::Update_FactoriesQueues(AbstractType factoryOf, bool isNaval, BuildCat buildCat)
```

> **Correction worth internalizing:** naval is a **separate channel** from vehicles
> even though it shares the *vehicle tab*. Rev 1 said "naval folds into vehicles" —
> true of the tab, false of the queue. Antares comments the analogous case at
> `0x509140`: *"defenses live in their own queue, but share the building factory."*
> **Channel ≠ tab ≠ factory building.** Three different groupings; conflating any
> two is the classic bug in this subsystem.

Each channel has four properties, and vanilla fixes all four. Each ask unfixes one:

| Vanilla assumption | Ask | Becomes | Already done? |
|---|---|---|---|
| one **factory building** advances per channel | #3 | several may | ✔ **AI only**, via `AllowParallelAIQueues` |
| one **item** (the front) advances per channel | #2, #3 | first N non-held | ✘ the real gap |
| queued item **auto-completes** | #1 | may be **held** | ✘ new |
| **exit** is category-implicit | #4 | explicit exit rule | ~ partly (§6) |

---

## 2. The sync path is already decided by the engine

The single most useful thing the audit found. Antares' hook at `0x6AB773`
(`SelectClass_ProcessInput_ProduceUnsuspended`, ✔ read) implements shift-click =
queue five:

```cpp
GET(EventClass* const, pEvent, EAX);
GET_STACK(byte const, modifiers, 0xB8);
auto count = 4 * (modifiers & 1) | 1;
while(count--) { Networking::AddEvent(pEvent); }
```

Three consequences, all binding on this design:

- **Production commands are network events**, not local state writes. A hold toggle
  must therefore be dispatched as an `EventClass` through `Networking::AddEvent`,
  never a direct flip of a queue flag on the clicking client. That is the whole of
  §C1's sync problem, already solved by the existing idiom — reuse it rather than
  re-deriving it (the [[kratos-rng-desync-rootcause]] lesson: never let client-local
  input steer synced state).
- **Modifier-click is the established input idiom at this exact hook**, so #1's
  toggle has an obvious home — but Antares already owns `0x6AB773`, so BuildQueueExt
  must chain, not replace (§8).
- `0x6AB773` and `0x6AB312` are hooked by **Antares only** (✔ registry) — no Phobos
  or Kratos collision at these two addresses.

---

## 3. What #2/#3 actually need (the corrected gap)

Antares at `0x4502F4` (✔ read) restricts a house to one active factory per channel:

```cpp
if(H->Production && !RulesExt::Global()->AllowParallelAIQueues) { … return 0x4503CA; }
```

`AllowParallelAIQueues` defaults to **true** (✔ `src/Ext/Rules/Body.h:175`), and the
guard is gated on `H->Production` — YRpp declares this as
`bool Production; // AI production has begun.` (✔ `YRpp/HouseClass.h:823`), so it is
the AI flag as suspected. *Corroborate in-game via the P0 probe (Q3) before P2 leans
on it — a YRpp comment is good evidence, not proof.* So:

- **"Use another idle war factory" is already solved** for AI, and the alternate-exit
  search exists in vanilla/Antares regardless — `BuildingClass_KickOutUnit_FindAlternateKickout`
  at `0x4444E2` (✔ Antares + Ares). #3 does **not** need to reimplement exit hand-off.
- **The genuine gap is the per-item front:** one `FactoryClass` advances one object.
  "Build 4 tanks at once" means *N fronts advancing in one channel*, which no
  framework does. **That, and only that, is BuildQueueExt's #2/#3 contribution.**

**Verified gap worth noting:** classic Ares documented per-category
`ForbidParallelAIQueues.Infantry/.Vehicle/.Navy/.Aircraft/.Building` plus a
per-TechnoType override (⚠ *search-sourced, not verified against Ares source*).
**Antares implements only the global flag** — `grep ForbidParallel` over the Antares
tree returns nothing (✔ verified). If those per-category tags matter, that is either
an Antares parity gap to report upstream or a small, well-scoped BuildQueueExt
feature. Worth confirming before building anything larger.

---

## 4. Feature #1 — queue lock / hold (first deliverable)

> **⚠ The hold primitive may already exist.** `FactoryClass` carries `OnHold`,
> `IsSuspended`, and `IsManual` — YRpp documents the last as *"whether the current
> suspension state was caused by the player"* — plus `Suspend(bool manual)`
> `0x4C9E60` / `Unsuspend(bool manual)` `0x4C9EA0` (✔ `YRpp/FactoryClass.h`). That is
> close to the semantics this section proposed to invent, and vanilla already
> suspends production from the cameo (Antares' hook at `0x6AB773` is literally named
> `…_ProduceUnsuspended`). **P0's probe exists to settle this before any code is
> written here** (Q1/Q2). If confirmed, #1 shrinks from "build a hold system" to
> "extend suspension to queued items + free the front + draw a glyph."
>
> None of `Suspend`/`Unsuspend`/`StartProduction`/`CompletedProduction` is hooked by
> any framework in the registry (✔) — unclaimed ground.

### Behavior
- A queued item may be toggled **Held**: no progress, no credit drain. A
  completed-but-held item stays parked (generalizing the building "ready" state).
- Cameo shows a hold glyph. Concurrency (§3) advances the first N **non-held**
  items, so hold and concurrency are orthogonal by construction.
- Replaces the 99999 hack, which still drains credits, still occupies the single
  front, and can't be un-held.

### Control surface
```ini
[SOMETECHNO]
Queue.Holdable=no          ; opt-in, default no → fully backward compatible
Queue.HoldWhenComplete=no  ; park on completion instead of blocking the front
```
Toggle input: modifier-click at the `0x6AB773` idiom (§2), dispatched as an
`EventClass`. **Which** modifier depends on what Antares leaves free — shift is
taken by queue-5.

### Hook points
| Purpose | Address | Status |
|---|---|---|
| Skip accrual for held slots | `FactoryClass` update | ⚠ TBD |
| Toggle dispatch (chain after Antares) | `0x6AB773` | ✔ exists, Antares-owned |
| Cameo click resolution | `0x6AB312` | ✔ exists, Antares-owned |
| Hold glyph | strip draw | → **SidebarExt boundary** (§7) |

---

## 5. Feature #2/#3 — N fronts per channel

- Per-house, per-channel concurrency cap `N`, default 1. Raisable by owning a
  building: `[BLDG] BuildQueue.ConcurrencyBonus=2`, summed over owned buildings,
  clamped by a rules max. Recompute on building gain/loss (power-like).
- Channel advances the first N non-held items; each accrues and drains
  independently and **deterministically** (§2 — synced state).
- Finished fronts eject through the **existing** kick-out / alternate-kickout path
  (§3). Do not rewrite it.
- Buildings channel: layer on Phobos `BuildingProductionQueue` (§8), and keep
  "next building can't start until the current is *placed*" unless deliberately
  relaxed.

`0x4502F4` (the channel-restriction site) is a **three-way conflict** — Antares,
Ares, *and* Phobos all hook it (✔ `conflicts.md`). Same for `0x4CA07A`
(`FactoryClass_AbandonProduction`). Treat both as occupied territory.

---

## 6. Feature #4 — factory types, exit, output

**The factory-*type* half is already done by Antares** — `Factory.ExplicitOnly=yes`
+ `BuiltAt=` (the documented "Kennel" pattern). Do not reimplement. Use
`WeaponsFactory=`/`*Barracks=` (not `Factory=`) for correct walk-out.

**⚠ Dead-code trap — the most important hook finding for #4.** Antares replaces
`ObjectTypeClass::FindFactory` **wholesale** at `0x5F7900` (✔ read): its handler
calls `HouseExt::HasFactory(...)`, writes `EAX`, and `return 0x5F7A89` — jumping to
the epilogue so **the entire vanilla body never runs**. Anything hooked *inside*
that body is dead code whenever Antares is loaded. This is the same trap the
Encyclopedia already documents for `HouseClass::CanBuild` at `0x4F7870`.
→ **#4 must extend `HouseExt::HasFactory` / chain at the epilogue, not hook vanilla
factory-selection.**

New code, each layered *on top of* the vanilla search (fall back, never replace):

- **Cross-category exit** — `[TYPE] ExitFrom=<building list>`: eject from those
  buildings even if they aren't the category's factory (infantry ⇄ war factory).
  The per-category kick-out sites `0x444119` (Unit), `0x444131` (Infantry),
  `0x44531F` (Building), `0x443CCA` (Aircraft) are all **triple-hooked**
  (Antares + Ares + Phobos) (✔ registry) — the busiest cluster in this design.
- **Output cell** — per-factory rally/exit override. Phobos already has
  `BuildingClass_ExitObject_BarracksExitCell` at `0x444B83` (✔ registry): precedent,
  and a coexistence question.
- **SW-from-factory** — ⚠ largest unknown; dive the SW launch path before scoping.
  May reduce to a thin bridge to the existing SW system.

---

## 7. UI boundary: BuildQueueExt vs SidebarExt

Revision 1 declared "no new sidebar tab" as a permanent non-goal on the grounds
that the 0–3 cap was effectively immovable. **That reasoning was wrong** (§0.3):
SidebarExt found the caps are arithmetic, and is already scoping >4 tabs with
custom icons, a 3-column grid, and floating cameo panels.

The correct split is by **layer, not by feasibility**:

| Layer | Owner |
|---|---|
| Queue state, hold flags, concurrency, exit routing, INI tags | **BuildQueueExt** |
| Strips, tabs, columns, button pool, cameo pixels, glyph drawing | **SidebarExt** |

So #1's hold glyph is *specified* here and *rendered* there. If both DLLs ship,
BuildQueueExt should expose the hold state and let SidebarExt draw it; if only
BuildQueueExt ships, it draws a minimal glyph at the strip-draw site and cedes that
hook the moment SidebarExt lands.

> **Inherited constraint from SidebarExt's audit:** the `SelectClass` button pool at
> `0xB07E80` is a fixed 240 entries (60/tab, stride `0x38`) and the row count
> `(H-260)/50` is never clamped — it silently corrupts adjacent tabs and then the
> repair/sell toggles at high resolutions. **Any BuildQueueExt feature that adds
> cameos or buttons inherits this overflow trap.** Same bug class as
> [[mapsizeext-astar-pool-overflow]]. Don't add sidebar buttons without reading
> SidebarExt's `DESIGN.md` first.

Also relevant: PR **#1384** (Exclusive SuperWeapon Sidebar) **merged 2025-06-05**;
**#1383** and **#1379** are its *closed predecessors* (✔ verified via `gh`). #1379
died because reviewers demanded `ControlClass`/`SelectClass` reuse instead of a
custom UI class, and flagged missing scrolling — **treat that as the binding design
constraint for any new sidebar UI element in either DLL.** Phobos's SW-sidebar work
is also what hooks the tab-index sites (`0x6A5F6E`, `0x6A614D`, `0x6A633D`,
`0x6ABC9D` — all in `src/Ext/SWType/Hooks.cpp`, ✔ registry), so tab-index territory
is Phobos-adjacent, not virgin.

---

## 8. Coexistence — the dominant risk

Ranked by how occupied the ground is:

| Site | Address | Occupants | Note |
|---|---|---|---|
| Channel restriction | `0x4502F4` | Antares + Ares + Phobos | ✔ real 3-way conflict |
| Abandon production | `0x4CA07A` | Antares + Ares + Phobos | ✔ real 3-way conflict |
| Kick-out (4 categories) | `0x444119/444131/44531F/443CCA` | Antares + Ares + Phobos | ✔ triple-hooked |
| FindFactory | `0x5F7900` | Antares (**full replacement**) | ✔ dead-code trap |
| ShouldDisableCameo | `0x50B370` | Antares + Ares (full replacement) | ✔ |
| Cameo click / input | `0x6AB312`, `0x6AB773` | Antares only | ✔ clearest ground |
| Strip draw (de-hardcoded) | `0x6A9C54`, `0x6AA88D` | Phobos `…FindFactoryDehardCode` | ✔ Phobos already de-hardcoding factory lookup in the strip |

Rules: **chain after / layer on; never replace.** Antares and Phobos mostly divide
the strip region by hooking *different* addresses — only `0x6A99F3` collides there
(✔ `conflicts.md`) — so the free space is narrow but real. Antares carries ~1483
release hooks, 73 of them strip/sidebar/cameo.

---

## 9. Phasing

1. **P1 — #1 hold.** Per-slot flag, skip accrual, `EventClass` toggle chained after
   Antares at `0x6AB773`, INI opt-in. Smallest, and settles the UI boundary (§7)
   and the event idiom (§2) that everything later depends on.
2. **P2 — #2/#3 N fronts.** The core engine change and the main sync surface. Reuse
   kick-out/alternate-kickout for hand-off (§3). Layer on Phobos for the buildings
   channel.
3. **P3 — #4 exit/output.** `ExitFrom=` + output cell, via `HouseExt::HasFactory`
   (not vanilla FindFactory — §6).
4. **P4 — #4 SW-from-factory.** Only after the SW path is dived.
5. **Possible P0 —** the `ForbidParallelAIQueues.*` parity gap (§3), if confirmed:
   small, self-contained, and may belong upstream in Antares instead.
6. **(not ours)** Tabs/columns/glyph rendering → SidebarExt.

---

## 10. Encyclopedia debt

Per [[hook-encyclopedia-workflow]], this design consumed the registry and owes a
page back. There is **no production/factory page** in `encyclopedia/` today.
→ Contributing `encyclopedia/Production-Queues-Factories.md` covering the channel
table, `0x4502F4`, `0x4CA07A`, `0x5F7900` (the replacement trap), the kick-out
cluster, and the `EventClass` production-command path.
*(SidebarExt separately owes `Sidebar-Strips-Tabs.md` — don't duplicate it here.)*

---

## 11. Open decisions

1. **Name** — `BuildQueueExt` vs `ProductionExt` (scope is really production).
2. **`H->Production` semantics** (§3) — ⚠ must be verified before P2.
3. **`ForbidParallelAIQueues.*`** — Antares parity gap or BuildQueueExt feature?
   And is it real in classic Ares at all (⚠ unverified)?
4. **Hold modifier key** — shift is taken by queue-5; which is free?
5. **Concurrency cap shape** — flat per-house vs per-category
   (`ConcurrencyBonus.Vehicles=`), noting naval is its own channel (§1).
6. **UI split** (§7) — does BuildQueueExt draw a minimal glyph standalone, or hard-
   depend on SidebarExt?
