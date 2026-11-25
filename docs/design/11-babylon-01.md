# Babylon-01: the megastructure

The source is your *Megastructure Engineering Brief: BABYLON-01* of 2026-11-25. Sections 1 to 3 are yours, tidied. Section 4, what the game can use, is mine and says which of it exists.

**Classification:** Class-IV stellar/planetary megastructure. **Operator:** Niu Galactic Federation, Bureau of Containment and Penal Spectacle. **Primary function:** petawatt power generation, G.V.E.X. containment, galactic broadcast hub and penal trial arena.

## 1. The figure-8 orbital loop (yours)

Babylon-01 does not rest on a planet's surface: it is a colossal, articulated, closed-loop **figure-8** threaded around a binary planetary/stellar pair in deep space.

**A thermodynamic engine.** Continuous energy extraction needs a big temperature difference, and the structure uses two opposed bodies as a planetary-scale heat engine:

- **The thermal source (hot world).** The upper loop wraps closely round a tidally locked volcanic or magma-crusted body (or a stellar dwarf). Superheated plasma and liquid-metal coolant (molten sodium or salt alloys) circulate through radiation-shielded conduits and absorb its heat.
- **The heat sink (cold world).** The lower loop circles a sub-zero cryogenic ice or ocean world. After the central turbines, the fluid dumps its waste heat into that world's atmosphere and deep oceans and condenses back to cold liquid before it returns to the hot loop.

**Articulation and tidal shear.** Two massive orbiting bodies pull on the structure, so a rigid ring would snap. It is built from kilometre-thick carbon-nanotube and meta-alloy segments joined by hydraulic-magnetic dampers. As the worlds orbit, the structure flexes and groans, and the Federation uses that to shift, reconfigure and collapse internal sectors of the Tower on demand.

## 2. The crossover: the Central Nexus Space Station (yours)

At the centre of the figure-8, where the loops cross, sits the **Central Nexus Space Station**, at the L1 Lagrange point between the hot and cold worlds.

- **A magnetohydrodynamic power core.** Both conduits meet in the core and drive MHD turbines that make petawatt-scale electricity for gravity plating, containment fields, life support and the galaxy-wide holo-broadcast arrays.
- **Command, logistics and broadcast control.** Operational control decks, commentator broadcast booths, drone dispatch bays, and the heavy docking rings where the prisoner transport barges unload the condemned.
- **A zero-G, low-shear anchor.** The pulls of the two worlds cancel at the crossover, so the station is a stable anchor from which the vertical spine of the Tower rises perpendicular to the orbital plane.

## 3. The vertical spine: the Tower of Babylon (yours)

A multi-thousand-floor cylindrical or octagonal spire housing the playable trial sectors.

| Band | What it is like |
|---|---|
| **Lower tiers** (conduit and turbine decks) | Just above the power core: a deafening mechanical hum, exposed plasma and cryo transfer pipes, industrial grating, heavy electromagnetic shielding. |
| **Mid tiers** (containment and observation sectors) | Reinforced viewport bulkheads sometimes show the outside: the glowing figure-8 curving away toward the hot world's inferno on one horizon and the pale blue ice world on the other. |
| **Upper tiers** (deep-space spire and apex vaults) | Far above the nexus in raw deep space. The flexing is most violent here: unstable gravity zones, the high-tier X5 and X6 G.V.E.X. vaults, and the final betrayal kill-boxes. |

**Hazards.** Ruptured conduits or sabotaged valves can flood a sector with flash-freezing cryogenic mist (from the cold loop) or scalding thermal vents (from the hot loop).

**The floor collapse protocol.** When a squad unlocks a floor's stairwell or elevator, the Central Station diverts containment power upward, which triggers mechanical floor compression, toxic venting and heightened G.V.E.X. aggression, to force the survivors to climb or die.

**Vertical transit.** Persistent, stackable zig-zag industrial stairwells (slower, high tension) or active express elevators (fast, high risk, loud, prone to ambush).

## 4. What the game can use (mine)

| From the brief | In the game today | Notes |
|---|---|---|
| Stairwells, persistent and zig-zag | **built** (`dungeon.md`, the staircase and its clock) | the stairs are also the escape from `X6-999` (`10-gvex-dossier.md`) |
| The collapse when a stairwell is unlocked | **built** as the stair countdown and the collapse wave (`sim.md`, "The clock, the door and a new floor") | the brief gives it a reason: containment power diverted upward; "heightened G.V.E.X. aggression" is not built |
| Express elevators | in the design (`05-world-and-lore.md`: "an elevator, not always active"); in the code only a tile type (`TILE_ELEVATOR_UP`) and an unused position on the floor, nothing carries a player | fast, loud, ambush-prone: a good place for the Clanckers and the Slenderen |
| Lower, mid, upper bands | not built as bands | they fit the tier ladder: X0 to X2 low, X3 to X4 in the middle, X5 and X6 high; `03` proposed floors 1 to 5, 6 to 9, 11 to 19, 20 and on |
| Cryogenic mist and thermal vents | not built | floor hazards in the same family as the dead-light patches; they would be status effects (`04-mechanics-and-systems.md`) |
| The mid-tier viewports with the figure-8 outside | not built | a skybox or a baked backdrop on the floors that have a window; cheap and strong for the setting |
| The broadcast hub | the commentator is a voice in the design (`05-world-and-lore.md`) | the brief puts the booths and the drone dispatch in the Central Station |
