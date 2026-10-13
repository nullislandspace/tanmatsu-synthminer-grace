---
title: Lava
summary: It flows, it burns, and water turns it into stone. It is only found in the Deep Mine.
---

Lava is a **fluid**, and since v0.3.0 it behaves like one: it flows, it pours out of a
breached lake, and it stops being a fluid where water reaches it.

You will only ever meet it in the [Deep Mine](../world/deep-mine.html). It is not
generated anywhere in the overworld, and there is no way to carry it.

## What it is not

| | |
|---|---|
| **Breakable** | **No.** It is unbreakable, like bedrock |
| **Pickable up** | **No.** A bucket does not work on it, in either direction |
| **Fuel** | **No.** It will not run a furnace |
| **Light** | Yes — it gives off light 13, the brightest thing in the game |

## Where it comes from

The Deep Mine's lava has **one level for the whole dimension: y = 12**, exactly as the
overworld's sea has one sea level. Every column whose cave floor lies below that level
is filled up to it, which is what makes it behave like a sea rather than a scatter of
blobs:

- the surface comes out **flat**;
- there is a **shore** where the ground rises out of it;
- each lake is **walled by its own basin**, because the columns around it have ground
  above the level.

So a lava sea is low ground, and the way to avoid one is to walk where the floor is
higher. About **3–7% of the cave floor** is lava.

## It flows

Breach a lake with a pickaxe — dig into the wall or out of the floor — and the lava runs
out of the hole like water does, at **a third of water's speed** and for **three blocks**
before it thins out to nothing.

**A lava flow is not a new source.** Two neighbouring lava flows never become a source
between them, so a lake you drain **stays drained**. Water is not like that, which is the
one place the two fluids have different rules.

## Water meets lava, and the lava becomes stone

**Whichever one moved, the lava is the one that solidifies.** Water reaching lava turns
that lava into **stone**; lava reaching water turns itself into stone. The water survives.

- A wall of stone appears at the edge where they meet, and it is not replaceable, so the
  quench cannot eat its way through itself.
- It is **stone, not cobblestone and not obsidian** — so it drops cobblestone for a
  pickaxe, and it matches the cave floor it now walls.
- This is the answer to a lava lake in the way of some ore: **bring water**, and the lake
  becomes ground.

## What it does to you

**Standing in lava costs 2 health — one heart — every half second.** Full health to dead
in five seconds.

**A creature standing in lava takes exactly the same.** A zombie that walks into a lava
sea burns at your rate, which means a lava lake is a wall it will not happily cross, and
that you cannot outlast one.

Lava does not push you, does not swim you, and does not slow you down. It is the floor,
on fire.

<p class="note"><b>It is the one thing down there that hurts you with nobody else
involved.</b> The HUD says <em>"The lava burns"</em>. Falling and mobs each name
themselves; lava has nobody to blame.</p>
