---
title: Mine shaft
summary: Sixteen iron ingots, sixteen iron blocks, and the only way into the Deep Mine.
---

| | |
|---|---|
| **Recipe** | **16 iron ingots**, at a [crafting table](crafting-table.html) |
| **Makes** | 1 **mine shaft** — one item that places a 16-block frame |
| **Takes up** | A **3×3 footprint, three blocks tall**: 12 post cells and 4 braces |
| **Breaks with** | A pickaxe. 1.5 s with a stone one. **Drops nothing** |
| **Does** | Carries you to the same column in the [Deep Mine](../world/deep-mine.html), and back |

This is the door to the second dimension, and it is the only one. Sixteen ingots in,
sixteen blocks out — the recipe and the shape are the same number.

## The shape

Four posts, one on each corner of a 3×3 square, three blocks tall, joined by four
braces along the edges at the top. The middle is left open, and the open cell one
above the ground is where you stand.

```
      P---B---P          P  post        B  brace
      |       |          .  open, and where you stand
      |   .   |
      |   .   |
      P       P
   ___ground___
```

## Placing one

**<kbd>E</kbd> pointing at the ground.** It wants:

- **solid ground under all nine cells** of the footprint, not just the middle — a post
  with nothing under it is a post in the air;
- **clear space for all sixteen blocks** of the frame;
- **an open centre**, or the shape is a decoration rather than a door.

If any of that is missing it refuses and says **"There is not room for that here"**. It
also refuses, silently and on purpose, if part of the footprint is outside the loaded
world — a half-built frame at the edge of what is streamed in would never come back.

**You can place one in either dimension** — but the Deep Mine will not prepare ground
for you. It levels the ground for a shaft the game builds on a crossing, never for one
you put down, so down there you clear and flatten a 3×3 patch first.

## Crossing

**Stand in the open centre cell.** After a moment the game says *Crossing over*, saves
everything you have changed, unloads the world you are in, and streams in the other one
around the shaft you arrive at.

- **Same column, different height.** The mapping is **1:1 in x and z** — not Minecraft's
  1:8 — so the shaft you arrive at is directly above or below the one you stood in. The
  height is different, because each end has to stand on its own ground.
- **You arrive inside the frame**, in the same cell you would have placed yourself in.
- **Step out before you cross again.** The pairing has no memory, so the cell you land
  in is a shaft too; you have to leave the centre cell once, or it fires again.

<p class="note"><b>The way back always exists.</b> The far end of a column is built the
first time anybody crosses to it, with the same sixteen blocks and the same shape. In
the Deep Mine it levels the ground it stands on first — a 5×5 patch around the centre,
never a hole in the bedrock roof. A crossing that genuinely cannot build its frame says
<b>"The shaft would not open"</b> rather than putting you somewhere you cannot get out
of.</p>

## Taking one apart

A post and a brace are iron blocks: a pickaxe takes them down, in about 1.5 seconds
each with a stone one.

**They drop nothing.** A shaft you break is sixteen ingots gone, so choose the column
before you commit to it — and remember that the far end in the other dimension stays
standing whether you break this one or not.

## Two shafts, one column

| | |
|---|---|
| **You place** | The overworld end, where you want it |
| **The game builds** | The Deep Mine end, the first time you cross to that column |
| **The generator builds** | **Nothing.** There are no free shafts down there |

Because the pairing is by column, a shaft works in both directions as soon as both ends
exist — and there is nothing to remember about which one is linked to which.

<p class="note"><b>One shaft is a one-way trip if you break it.</b> Break the overworld
end and the Deep Mine end is still standing with nothing to walk back into. Fill it in
with cobblestone or leave it alone; either way, do not take the frame apart while you
still have things down there.</p>
