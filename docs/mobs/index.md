---
title: Hostile mobs
summary: Zombies and skeletons — where they come from, what they do, and what killing one gets you.
---

**Two kinds, and both of them live in the [Deep Mine](../world/deep-mine.html).** The
overworld has no hostile mobs at all: nothing on the surface hunts you, and no animal
fights back.

| | [**Zombie**](zombie.html) | [**Skeleton**](skeleton.html) |
|---|---|---|
| **Health** | 20 — 10 hearts | 16 — 8 hearts |
| **Damage per swing** | **10** — five hearts | **8** — four hearts |
| **Swings** | One a second | One a second |
| **Notices you at** | 16 blocks | 16 blocks |
| **Height** | 1.8 blocks | 1.9 blocks |
| **Drops** | **Nothing** | **Nothing** |

## They are generated once, and they do not come back

Mobs are placed **with the terrain**, exactly as a herd of pigs is: when a chunk of the
Deep Mine is generated for the first time, about **one chunk in eight** gets a pack of
**two or three**. A pack lives in one chunk.

**Nothing respawns.** A corridor you cleared stays cleared, for good. That is the whole
shape of the play: you conquer ground by lighting it, killing what is in it, and mining
it — and it stays yours.

It also means a pack you have killed is gone from that chunk permanently, and a pack you
never walked into is still standing exactly where the world put it.

**Zombies are 60% of the members and skeletons 40%**, rolled per creature, so a pack of
three can be any mix.

## What they do

**They come to you.** Within 16 blocks a hostile mob picks you as its target and walks
at you — at 1.4× its ambling speed, which is **slower than you walk**. It stops when it
is within a swing (1.7 blocks) and swings once a second.

**One target, never a list.** Each one is after either you or exactly one other creature.
So the one that noticed you is the one coming, and a pack of three behaves like three
things rather than like a swarm.

**They hurt each other.** If a zombie's swing lands on a skeleton because the skeleton
happened to be in the way, **the skeleton is now after the zombie** — and the other way
round. Stand still between two of them and they will deal with each other.

**They forget you at 26 blocks**, which is deliberately further than the 16 that notices
you: a mob should not give up mid-chase because you rounded a corner.

**They burn in lava** at exactly the rate you do — 2 health every half second.

**They cannot leave the loaded world.** A mob walks to the edge of what has been streamed
in and stops there, like everything else.

## What they do not do

- **They do not drop anything.** No bone, no string, nothing. See below.
- **A skeleton has no ranged attack yet.** It walks and swings like a zombie.
- **They do not pathfind around obstacles.** They steer toward you, which means a fence,
  a wall or a two-block ledge works as a defence even when the mob can see you.
- **They do not spawn in the dark.** Nothing in this game spawns by darkness any more; a
  [torch](../devices/torch.html) is for seeing.
- **They do not appear in the overworld**, and they do not follow you through a
  [mine shaft](../devices/mine-shaft.html).

## Why they drop nothing

**Nothing a mob drops may be the only source of anything in the game.** A mob is a
hazard, not a resource: the reason to kill one is that it is between you and the iron,
not that it carries something you need.

Everything craftable already has another source — bone from a sausage maker, string from
wool — so nothing in the game depends on a body. If a drop is ever added it will be
something worth having as loot, never an ingredient you cannot get another way.

## Fighting them

**A sword, or nothing.** A pickaxe, an axe, a shovel, a hoe and a fist all do **zero**
damage to a creature, so the fight starts when you can forge one:

| In hand | Damage | Skeleton (16) dies in | Zombie (20) dies in |
|---|---|---|---|
| Everything that is not a sword | **0** | never | never |
| [Iron sword](../tools/sword.html) | 7 | 3 swings | 3 swings |
| [Titanium sword](../tools/sword.html) | **11** | **2 swings** | **2 swings** |

**And they hit far harder than they used to.** A zombie's swing is **10** — five of your
twenty health — and a skeleton's is 8. Unarmoured, **a zombie kills you in two swings and
a skeleton in three**, one swing a second each. That is the reason [armour](../tools/armour.html)
exists: a full iron set turns a zombie's 10 into 6, and a full titanium set turns it into
**1, half a heart**. See [Armour](../tools/armour.html).

One swing a second, from either side. A sword spends one use per blow that lands, so an
iron one is good for about eighty zombies.

**Hitting costs nothing to eat.** Swinging does not spend hunger in this game — mining
does. What a fight costs is health, and healing afterwards is the expensive part: half a
heart every four seconds, and one and a half points of hunger for each. See
[Health, falling and death](../mechanics/health.html).

<p class="note"><b>Running works.</b> They are slower than you. A corridor you cannot
hold, you can run past — and with nothing respawning, running past is only a delay, not
a defeat.</p>
