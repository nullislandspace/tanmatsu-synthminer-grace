---
title: Version history
summary: What each release added, newest first.
---

| Version | Name | Date | |
|---|---|---|---|
| **v0.4.0** | **Longer Sword Diplomacy** | 18 October 2026 | [What's in it →](v0.4.0.html) |
| **v0.3.0** | **The Deep Mine Update** | 13 October 2026 | [What's in it →](v0.3.0.html) |
| **v0.2.1** | **What Playing It Found** | 1 October 2026 | [What's in it →](v0.2.1.html) |
| **v0.2.0** | **Fred's Farm Update** | 30 September 2026 | [What's in it →](v0.2.0.html) |
| **v0.1.0** | **The Base Game** | 28 September 2026 | [What's in it →](v0.1.0.html) |

<p class="note"><b>Your worlds keep working.</b> Nothing about the way a world is saved has
changed since the first release in a way that could cost you a world, so one made in v0.1.0
opens in any later version and gains everything the new version added. Nothing has to be
converted and nothing is lost — a world upgrades gradually as you walk around in it. v0.3.0
did widen one detail of how a single chunk is written: old chunks are still read as they
are, and rewritten in the new form the next time you change one.</p>

## Where the game keeps your things

Worlds, `settings.txt`, replays and screenshots live in **`/sd/synthminer`**, which is *not*
the folder the app installs into. So updating or reinstalling SynthMiner cannot touch them.

## How to read a version number

There is no compatibility promise attached to the numbers yet — the game is pre-1.0 and the
plan is still being worked through. What the numbers do mean:

- the **middle** number goes up when a release adds things you can do;
- the **last** one goes up for fixes.

The name is the useful part. *The Base Game* is the world, the mining and the crafting.
*Fred's Farm Update* is everything about keeping yourself alive in it.
*The Deep Mine Update* is the second dimension, and the first things in the game that fight
back.
*Longer Sword Diplomacy* is the gear: a forge, a sword, four pieces of armour, and the ore
armour is made of.

## What is coming

**The numbers are the thing to be tuned.** How much iron and how much titanium the cave
holds, how many packs of mobs, how tall the ceilings are, how hard a zombie hits, what a
chestplate is worth, how long a job in the forge takes — all of it was written down before
any of it was played with, and all of it is expected to move after some playing.

Not built yet, in rough order of how likely they are: more kinds of hostile mob, a skeleton
that shoots, falling sand that flows toward a hole, and red flowers you can grow.
