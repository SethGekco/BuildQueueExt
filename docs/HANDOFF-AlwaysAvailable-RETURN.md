> ## ✅ STEP (a) WORKS — tested in-game 2026-09-27
>
> `AddCameo` **returned TRUE** and the GAPILL cameo **appeared**. The experiment in
> §5 was right, and so was the diagnosis behind it: nothing was asking, so the cameo
> had to be pushed.
>
> ```
> [BQExt] AlwaysAvailable PUSH #1 GAPILL idx=66 -> AddCameo returned TRUE  [accepted 1]
> ```
>
> **Neither predicted failure mode appeared.** `RecheckCameos` did not drop it (no
> flicker), and the tab stayed reachable.
>
> **Observed behaviour, all of it correct:**
> - Cameo present at launch (MCV undeployed = no ConYard), **greyed**.
> - **Disappears on deploying the MCV.** Expected: the push no-ops once a usable
>   ConYard exists, and the normal path then correctly hides GAPILL because there is
>   still no Barracks (`Prerequisite=BARRACKS`).
> - **Returns on undeploying.** The push resumes.
> - Normal play unaffected — GAPILL builds as usual with a ConYard and a Barracks.
>
> **It is greyed rather than live because step (b) is currently disarmed** (Test 25).
> The cameo exists; `CanBuild` still answers 0. Re-arming the
> `Prerequisite.NoFactory=` container is the next thing to test — that is the step
> that should turn it live.
>
> Step (c) logged **0** substitutions this run, which is consistent: a greyed cameo
> is never clicked, so `FindFactory` is never reached for it. It should begin firing
> once (b) makes the cameo live.

---

# Handoff back to BuildQueueExt — `AlwaysAvailable` belongs here after all

**From:** PrerequisiteExt, 2026-09-27. **Status:** root cause found, **feature not
built**, one cheap experiment identified.

This answers `PrerequisiteExt/docs/HANDOFF-AlwaysAvailable.md`. That document's
investigation was good and its facts held up — but **its central conclusion was
wrong in a way that only in-game testing could reveal**, and the corrected answer
puts the feature back in BuildQueueExt's hands with most of the work already done
here.

Read §1 and §5 if you read nothing else.

---

## 1. TL;DR

**It is not solvable at `CanBuild`.** The original handoff said the only seat where
the no-factory verdict is *observable* is the `CanBuild` epilogue `0x4F8361`. True —
and irrelevant, because **once a house has no Construction Yard the engine stops
calling `CanBuild` for that house's BuildingTypes at all.** There is no verdict to
override. PrerequisiteExt implemented the promote, tested it in game, and the cameo
was still completely absent.

**BuildQueueExt already owns 2 of the 3 remaining pieces.** The missing one is
*adding the cameo*, and the function to do it (`SidebarClass::AddCameo`, `0x6A6300`)
is a normal callable in YRpp.

**Your `FindFactory` substitution was not useless.** It is step (c) of four. It
looked like a no-op because the cameo it would have served never appeared.

---

## 2. The evidence (so you don't have to re-run it)

Test: barracks built, then Construction Yard sold. Result: GAPILL's cameo
**completely absent**, and the whole Defense strip emptied — which kicked the player
out of that tab entirely.

From `debug.20260927-221101.log`:

- `frame 492: GAPILL house 0 -> ALLOWED (engine said buildable)` — with the ConYard
  alive, everything is normal.
- After the sale: **zero** PrerequisiteExt activity for house 0 across the remaining
  **~784,000 log lines**, while other houses keep being evaluated to frame 37951 and
  house 0 is still alive at frame 42300.
- Our verdict **never flipped** `ALLOWED -> BLOCKED`. It would have logged if it had,
  because an inert container plus the engine's `0` *is* BLOCKED.

The only reading that fits all three: **nothing asks `CanBuild` about that house's
BuildingTypes any more.**

---

## 3. THE MECHANISM — why nothing asks

`BuildingClass::UpdateConstructionOptions` is the function that populates the
building strips. Confirmed by disassembly:

```
4456d0:  mov eax,[ecx+0x21c]     ; ECX = BuildingClass* this ; +0x21C = Owner
4456d6:  mov edx,ds:0xa83d4c     ; CurrentPlayer
4456dc:  cmp eax,edx
4456df:  jne 0x44583e            ; not the current player -> bail immediately
...
445758:  call 0x4f7870           ; CanBuild(type, 0, 1)
44575d:  test eax,eax / je       ; skip if unbuildable
445769:  call 0x6a6300           ; SidebarClass::AddCameo   <-- the add happens HERE
```

**It is a `virtual` on `BuildingClass`** (`YRpp/BuildingClass.h:59`), vtable slot
**`0x7E439C`** — immediately after `Place` (`0x445F80`), matching YRpp's declaration
order. There are **no direct calls** to `0x4456D0` anywhere in `gamemd.exe` (one
occurrence of the address, the vtable entry itself).

> **So the building strips are populated by iterating the player's own buildings and
> calling a virtual on each.** With no ConYard there is no building that drives it,
> so `AddCameo` is never reached and no amount of changing `CanBuild`'s answer
> matters. The gate is not the verdict; it is whether anything asks.

⚠ Antares hooks `0x4456E5` here (`BuildingClass_UpdateConstructionOptions_ExcludeDisabled`,
`src/Ext/WarheadType/Hooks.EMP.cpp:72`) for EMP exclusion — so this function is
already co-hooked. Not a full replacement; it returns 0.

---

## 4. The four-step chain, and who has each piece

| Step | What it needs | Status |
|---|---|---|
| **(a) Cameo present in the strip** | something must call `AddCameo` | ❌ **THE MISSING PIECE** |
| (b) `CanBuild` answers "yes" | override the `NoFactory` veto | ✅ PrerequisiteExt, works when asked |
| (c) Production can start | `FindFactory` must not return null | ✅ **your `0x5F7A89` substitute** |
| (d) Placement anchor | finished building has nowhere to go | ✅ **your shipped `LimboOnComplete=yes`** |

You built (c) and (d) before (a) existed. That is exactly why (c) fired ~26× and
appeared to do nothing.

---

## 5. THE NEXT STEP — one cheap, decisive experiment

`SidebarClass::AddCameo(AbstractType absType, int idxType)` at **`0x6A6300`** is a
normal `JMP_THIS` callable in `YRpp/SidebarClass.h:88` — **not** an `R0` stub. So the
cameo can be **pushed** rather than waited for.

**Experiment:** from any driver that runs regardless of buildings (a per-frame or
1 Hz hook), for each `AlwaysAvailable`-tagged BuildingType, when the house is
`CurrentPlayer` and has no usable factory, call
`MouseClass::Instance.AddCameo(AbstractType::BuildingType, idx)`.

**Judge it purely on "does the cameo appear."** Not on being able to build — that
still needs step (c). Same discipline that kept the terrain test honest.

**Two predicted failure modes, so they are recognisable:**

1. **`StripClass::RecheckCameos` (`0x6AA600`) may drop it again.** Probably
   survivable: it keeps a cameo on a *nonzero* `CanBuild` (`test eax,eax / jne` at
   `0x6AA786`, advancing by stride `0x34` at `0x6AAA68`), and a promote returns `1`.
   First suspect if the cameo flickers.
2. **The tab may still be inaccessible.** The player reported being *kicked to the
   Infantry tab* because the Defense strip was empty. Re-adding one cameo may or may
   not restore tab access — check that separately from cameo presence.

---

## 6. What PrerequisiteExt now offers you

Shipped, tested (149/149 off-target), deployed:

```ini
[SomeContainer]
Prerequisite.NoFactory=yes     ; container applies ONLY when the house has no factory
Prerequisite.HasFactory=yes    ; ...or only when it does. Omit both = don't care.
```

- It is a **gate**, not a test — a failed factory condition makes the container
  *inert*, not merely abstaining. (As a test it counted as an active `Enable` group
  member and **blocked the type for everyone who did own a ConYard**. A unit test
  caught that; do not re-introduce it.)
- The factory scan mirrors `HasFactory`'s own filters: skips `InLimbo` / `Selling`,
  requires `Factory == AbstractType` and matching `Naval`-ness, **and honours
  `requirePower`** (`!HasPower || Deactivated` does not count). That last one was
  initially missing and made our answer disagree with Antares' for a powered-down
  ConYard — worth mirroring if you write your own scan.
- Combined with `Absolute=yes` it delivers step (b), scoped so the promote cannot
  also bypass `Owner=` / `TechLevel` / `Prerequisite=` while a factory exists.

So the division that makes sense: **PrerequisiteExt states the condition,
BuildQueueExt owns the sidebar and production.** Same shape as the SpawnExt merge —
the DLL that owns the subsystem should own the feature.

---

## 7. Corrections to the original handoff

Keeping these explicit so they are not re-derived. Its §1–§5 facts all held.

- **§3 "the only seat is `0x4F8361`" — insufficient.** Observability was never the
  binding constraint; being *consulted* is.
- **§4 "it worked mechanically and was useless" — half right.** It worked
  mechanically and was *premature*. It is step (c) and still needed.
- **§6's suggested `Prerequisite.IgnoreFactory=`** shipped as
  `Prerequisite.NoFactory=`, because the key does not ignore anything — it *scopes*
  a promote.
- **§2's "dropping `GACNST` alone does nothing" — correct and already done** in the
  live `rulesmd.ini`: `[GAPILL] Prerequisite=BARRACKS`, original preserved commented.

---

## 8. Verified vs not

**Verified by disassembly of `/home/rex/gamemd.exe`:** the `0x4456D0` prologue and
its `CurrentPlayer` bail; the `CanBuild` → `AddCameo` sequence at
`0x445758`/`0x445769`; the vtable slot `0x7E439C`; the absence of any direct call to
`0x4456D0`.

**Verified in game:** the cameo is absent after selling the ConYard; the strip
empties; the player is kicked out of the tab; PrerequisiteExt is not consulted
afterwards.

**⚠ NOT verified:** that `AddCameo` from an external driver actually produces a
usable cameo; whether `RecheckCameos` then removes it; whether tab access returns.
That is precisely what §5's experiment is for — **nothing below §5 has been run.**

**Live test fixture** (currently armed in `rulesmd.ini`, disarm if it gets in the
way): `[GAPILL] Prerequisite.Containers=T25_NoConYard`, container at the bottom of
the file with the three-state recipe and the "judge by the cameo" warning.
`AlwaysAvailable=yes` is *also* still enabled on GAPILL from your side, so expect
both DLLs to log about the same type.
