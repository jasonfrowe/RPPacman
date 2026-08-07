# Championship Pac-Man for the RP6502

This is a port of the classic Championship Pac-Man game to the RP6502 platform using the LLVM-MOS SDK. The game features the original maze layout, graphics, and gameplay mechanics, providing an authentic Pac-Man experience on the RP6502 hardware.


## Building the Game

Created a blank binary file for the text map data:

```bash
dd if=/dev/zero of=Text_map.bin bs=1 count=600
```

This will make a 600-byte file filled with zeros, which can be used as a placeholder for the text map data in the game.



The **NES demake of *Pac-Man Championship Edition*** (featured in *Namco Museum Archives Vol. 1*) builds on the rules of the 2007 arcade/Xbox 360 release, adapted for 8-bit hardware mechanics.

Reference : https://pacman.fandom.com/wiki/Pac-Man_Championship_Edition 

### 1. Pac-Dots & Escalating Multiplier

* **Base Value:** 10 points per Pac-Dot or Power Pellet.
* **Multiplier Scaling:** The dot value increases after every **60 Pac-Dots** eaten within a single life:
* 0–59 dots: **10 pts** each
* 60–119 dots: **20 pts** each
* 120–179 dots: **30 pts** each
* 180–239 dots: **40 pts** each
* 240+ dots: **50 pts** each (Maximum)


* **Penalty:** Dying resets the dot multiplier back to **10 points**.

---

### 2. Scared Ghost Combo System

In traditional *Pac-Man*, ghost points double per ghost up to 1,600 points. In *Championship Edition*, the sequence continues scaling far past 4 ghosts, and eating a new Power Pellet while the energized timer is still active **extends the combo chain**:

| Ghost # in Chain | Point Value |
| --- | --- |
| **1st Ghost** | 400 pts |
| **2nd Ghost** | 800 pts |
| **3rd Ghost** | 1,200 pts |
| **4th Ghost** | 1,600 pts |
| **5th Ghost** | 2,000 pts |
| **6th Ghost** | 2,400 pts |
| **7th Ghost** | 2,800 pts |
| **8th+ Ghost** | **3,200 pts** each (Cap) |

*Since cleared half-mazes constantly spawn new Power Pellets, maintaining an active ghost-eating chain is the key to high-score runs.*

---

### 3. Bonus Items (Fruit & Items)

Bonus items spawn on the opposite side of the maze when you clear all dots from one side. Collecting the bonus item refreshes and updates the layout of the cleared half-maze.

Item point values scale progressively as you cycle through layout regenerations:

Bonus Items:

    🍒 Cherry: 1000 points
    🍓 Strawberry: 1200 points
    🍊 Orange: 1400 points
    🍎 Apple: 1600 points
    🍈 Melon: 1800 points
    🍌 Banana: 2000 points
    🍑 Peach: 2200 points
    PM Galaxian Galaxian Boss: 2400 points
    🔔 Bell: 2600 points
    🔑 Key: 4000 points
    ☕ Coffee: 4200 points
    🍰 Cake: 4400 points
    PM Galaxian Boss Galaga: 4600 points
    PM Galaxian Gaplus Drone: 4800 points
    🍔 Hamburger: 5000 points
    🍳 Fried Egg: 5200 points
    🍬 Candy: 5400 points
    🍀 Four-Leaf Clover: 5600 points
    💎 Diamond: 5800 points
    ❤️ Heart: 6000 points
    PM Galaxian Samurai Helmet: 6200 points
    👑 Crown: 7650 points

---

### 4. Game Loop & Extra Lives

* **Time Limit:** Championship Mode runs on a **5-minute countdown timer**. Challenge/Extra modes run on **10-minute timers**.
* **Speed Dynamic:** Gameplay speed scales upward as total points increase. Losing a life decreases the overall game speed.
* **Extra Lives:** Awarded every **20,000 points**.

### 5. Ghost Despawn & Reset Mechanics

In the 8-bit NES demake of Pac-Man Championship Edition (featured in Namco Museum Archives), the rules governing how ghosts "despawn," reset, and return to their starting spawn positions diverge significantly from traditional classic Pac-Man rules.

1. The Primary Despawn Trigger: Getting EatenGhosts do not randomly despawn during normal navigation. The main mechanism that forces a ghost to despawn and return to its spawn state occurs when Pac-Man eats a scared (frightened) ghost after collecting a Power Pellet:  State Transition (Physical Body Despawn): When Pac-Man collides with a blue/scared ghost, the ghost’s sprite body is immediately "despawned" or cleared from the active entity rendering layer.Eye Entity Generation: In its place, an "eyes-only" sprite entity is instantiated.Movement Mode Override: The AI pathfinding for those eyes drops its chase/scatter algorithms and switches strictly to a shortest-path floodfill routing back to the Ghost House door coordinates.Respawn & Regeneration: Once the eyes hit the tile directly inside/above the Ghost House, the eyes despawn, the full ghost body sprite respawns, and the ghost exits back into the active maze (reverting to normal state even if the Power Pellet timer is still running).  

2. Side-Maze Clear & Regeneration DespawnIn Pac-Man Championship Edition, the maze is split into left and right halves. Clearing all Pac-Dots on one half causes a Bonus Item (fruit) to spawn on the opposite side.Maze Reset Trigger: Eating that fruit instantly refreshes and rearranges the layout of the cleared half-maze.Ghost Repositioning: If a ghost is trapped or currently residing inside the bounds of the half-maze section being regenerated, the engine forces an immediate reset on that ghost’s coordinates:It instantly clears/despawns from its current tile.It snaps back to its default starting position inside or above the Ghost House.This prevents ghosts from getting stuck inside new wall tiles generated by the fresh maze layout.

3. Life Loss / Death ResetWhen Pac-Man collides with an active (non-frightened) ghost:All ghost movement routines freeze.Every ghost entity is despawned from the maze array.Upon restarting the life, all four ghosts are re-instantiated at their hardcoded home coordinates (inside/just outside the Ghost House) in their default scatter/chase states.