---
title: Hunger and saturation
summary: Three numbers, one of which you cannot see — and it is the one that matters.
---

There are **three** numbers behind the bars at the bottom of the screen, and only two of
them are drawn.

| | |
|---|---|
| **Health** | 10 hearts = **20 points**. Falling and starving take it; it comes back by itself while you are well fed |
| **Hunger** | 10 drumsticks = **20 points**. What you can watch going down |
| **Saturation** | An **invisible reserve**, never larger than your current hunger, and **spent first** |

Everywhere in this manual, a food's "hunger" and "saturation" are in **points**. Two
points is one drumstick.

## Saturation is why a good meal lasts

Nothing you do costs hunger directly. What it costs is a fourth number — **exhaustion** —
and when enough of that has built up, one point is taken **out of saturation** if there is
any, and out of your drumsticks only when there is not.

So the reserve is a buffer in front of the visible bar. Two foods worth the same number of
drumsticks are not worth the same amount of walking:

| | Hunger | Saturation | Effect |
|---|---|---|---|
| **Grilled tomatoes** | 2 | 0 | Fills a drumstick. Nothing behind it |
| **Smoked salmon** | 2 | **4** | Fills the same drumstick, and then two more drumsticks' worth of walking before the bar moves at all |

**Smoked salmon is the travelling food.** The pizza (10 / 8) is the one you eat when you
are actually starving.

<p class="note"><b>Saturation is capped at your current hunger.</b> Four points of reserve
are no use to somebody with one drumstick left — so eat the big things when you are
<em>nearly</em> full, not when you are empty. Eat something cheap first if you have to.</p>

You start a new world with **full drumsticks and no reserve at all**, which is why the
first walk across the map costs hunger straight away.

## What costs what

Exhaustion builds up like this. **4.0 of it spends one point** (half a drumstick).

| Doing this | Costs | Which is |
|---|---|---|
| Walking | 0.010 per block | **400 blocks** per half-drumstick |
| Swimming | 0.015 per block | 267 blocks — the expensive way to travel |
| Jumping | 0.050 each | 80 jumps |
| Breaking a block | 0.005 each | 800 blocks mined |
| Taking damage | 0.100 per point | A ten-point fall is a quarter of a half-drumstick |
| **Healing** | **6.000 per half-heart** | **One and a half points of hunger** |

From a full bar with no reserve, **that is about 8000 blocks of walking** before the last
drumstick goes. In practice you will be mining and jumping too, but the scale is right:
hunger is a thing you manage over a session, not minute to minute.

## Healing is the expensive part

Health comes back **by itself**, half a heart at a time, every 4 seconds — but only while
your hunger is at **9 drumsticks or more**.

And each half-heart costs **6.0 exhaustion**, which is one and a half points of hunger. So
healing up from one heart to ten costs **more than a whole hunger bar**.

> This is what turns food from a formality into a supply line. Falling off a cliff does not
> just cost hearts; it costs the dinner it takes to get them back.

The last drumstick is the warning. Below 9 you simply do not heal.

## Starving

At **zero** drumsticks, you lose a point of health every 4 seconds, and it does **not**
stop at a floor. From full health that is **80 seconds** from empty bar to dead.

You will have watched it coming for half an hour. There is no surprise in it.

## Eating

Point at nothing in particular, hold food, press <kbd>E</kbd>. It is a **tap**, not a key
you hold down — this is a handheld, and <kbd>E</kbd> already means five other things.

- **A full player cannot eat.** It refuses and says so, rather than letting you lean on the
  key and empty a larder.
- Point at a chest or a machine and <kbd>E</kbd> opens it instead. Look at the floor or the
  sky to eat.
- **Milk leaves the empty pail** in your hand. Nothing else leaves anything.

## What you can eat

Only two things in the game need no cooking:

- a **raw tomato** (1 hunger, no saturation)
- **a bucket of milk**

Plus the three things the slow machines make, which need a machine but no stove: **cheese**,
**sausage** and **vegetarian sausage**, all 2 / 2.

Everything else — pork, beef, mutton, all three fish, wheat, rice, beans, potatoes — is an
**ingredient**, not food. A [kitchen stove](../devices/kitchen-stove.html) is what turns it
into a meal, and there are thirteen of those.

See [Every food](../food.html) for the complete table.

## Sleeping is free

Hunger is counted in ticks you actually played, so a night in a [bed](../devices/bed.html)
costs you nothing at all — even though it moves the world's clock forward a whole day and
grows your crops. Sleeping is strictly a gain.
