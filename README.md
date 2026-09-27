# Box2D-Packed

## Game-Side & API Performance: Faster Handle Operations

By removing world0 and shrinking index1 to uint16_t, we reduce b2ShapeId from 8 bytes to 4 bytes (32 bits total).

```C
typedef struct b2ShapeId
{
    uint16_t index1;
    uint16_t generation;
} b2ShapeId;
```

- Register Efficiency: A 32-bit struct fits entirely inside a single 32-bit or 64-bit CPU register. Passing a b2ShapeId by value to API functions like b2Shape_SetFriction(shapeId, 0.5f) now takes a single register move operation rather than packing/unpacking multiple registers or passing via stack/pointers.
- Cache Line Packing in Game Code: If your game maintains arrays or components storing b2ShapeId values (e.g., in ECS entities or game object instances), your handle memory footprint is halved. You fit 16 shape IDs per 64-byte cache line instead of 8.


## **Box2D-Packed Architectural Analysis & Bit-Packing Optimizations**

Analysis of structural changes, memory alignment strategies, and architectural paradigms between Box2D legacy definitions and Box2D v3 modern optimizations.

## **1\. Bit-Packing and Memory Layout Optimization**

In the updated b2WheelJointDef, boolean flags are transformed from full-byte representations (bool) into bit-fields (char : 1).

```C  
// OLD: Uses standard 1-byte booleans (often padded due to struct alignment rules)  
bool enableSpring;     // 1 byte \+ alignment padding  
bool enableLimit;      // 1 byte \+ alignment padding  
bool enableMotor;      // 1 byte \+ alignment padding

// NEW: Packed bit-fields sharing a single 8-bit allocation  
char enableSpring : 1;  
char enableLimit  : 1;  
char enableMotor  : 1;
```
### **Padding & Alignment Trade-offs**

| Metric | Legacy bool Approach | Bit-Field Approach |
| :---- | :---- | :---- |
| **Footprint per Flag** | 1 byte (plus potential boundary padding) | 1 bit |
| **Combined Flags Size** | 3 to 4 bytes | 1 byte |
| **Access Latency** | Direct load/store instruction | Bitwise masking required (AND/OR) |
| **Cache Line Utilization** | Lower density | Higher density |

### **Hot-Path Access Considerations**

Bit-packing configuration structures (b2WheelJointDef) reduces initialization memory footprints. However, in performance-critical solver loops:

> * Bit-masking operations (e.g., (flags & ENABLE\_SPRING\_MASK)) add minor CPU bit manipulation overhead.  
> * If flags are read repeatedly inside hot loops, store active runtime states as contiguous bitmask arrays or packed byte structures aligned to CPU cache line boundaries (\$64\\text{ bytes}\$).

## **2\. Single-World Design Architecture**

Box2D-Packed shifts toward a unified, continuous memory allocation model by constraining or optimizing around a single active world instance (or centralized contiguous storage pools).

\+-------------------------------------------------------------------+  
|                        Single b2World Stack                       |  
\+-------------------------------------------------------------------+  
|  \+------------------+  \+------------------+  \+-----------------+  |  
|  | Dynamic Bodies   |  | Static Bodies    |  | Joints Array    |  |  
|  | \[Contiguous Memory\] |  | \[Contiguous Memory\] |  | \[Contiguous\]    |  |  
|  \+------------------+  \+------------------+  \+-----------------+  |  
\+-------------------------------------------------------------------+

### **Key Architectural Benefits**

> 1. **Elimination of Pointer-Chasing**:  
   * Multi-world topologies require indirection tables to isolate physics islands.  
   * A single-world model allows contiguous array storage (SoA \- Structure of Arrays or dense AoS), ensuring near \$100\\%\$ L1/L2 cache line hit rates during iteration.  
> 2. **Sequential Island Solving**:  
   * Bodies, contacts, and constraints reside in pre-allocated cache-friendly buffers.  
   * Multi-threaded task graphs (via thread pools) can partition the global world into non-overlapping spatial islands without cross-world synchronization overhead.  
> 3. **Flat ID References vs. Raw Pointers**:  
   * Instead of allocating individual heap objects with native raw pointers (e.g., b2Body\*), entities are identified by index keys (e.g., b2BodyId containing an index and generation ID).  
   * Reduces dynamic malloc/free calls during world setup and breakdown down to zero inside hot paths.

## **3\. Micro-Optimization Strategies in Box2D-Packed**

Beyond struct alignment and single-world topologies, modern C physics engines employ targeted optimizations:

### **Field Downscaling (internalValue)**

> * Reducing non-critical fields like int internalValue (32-bit) down to uint16\_t (16-bit) saves 2 bytes per struct definition.  
> * When combined with bit-fields, this eliminates interior padding across arrays of definitions.

### **SIMD-Friendly Data Alignment**

> * Struct layout aligns SIMD vector primitives (b2Vec2, b2Rot) on 8-byte or 16-byte boundaries.  
> * Placing float quantities before bit-fields prevents compiler-inserted pad bytes between 32-bit floats and 8-bit integers:

```C  
typedef struct b2WheelJointDef  
{  
    b2JointDef base;          // Aligned base structure  
      
    // 32-bit floating point block (consecutive memory)  
    float hertz;  
    float dampingRatio;  
    float lowerTranslation;  
    float upperTranslation;  
    float maxMotorTorque;  
    float motorSpeed;

    // 16-bit field  
    uint16_t internalValue;

    // Packed 8-bit flag field (fits within trailing padding)  
    uint8_t enableSpring : 1;  
    uint8_t enableLimit  : 1;  
    uint8_t enableMotor  : 1;  
    uint8_t reserved     : 5;  
} b2WheelJointDef;  
```

## **4\. Implementation Status**

Everything below is implemented, and all 24 unit test groups pass in Release and Debug.

### Handles

| Handle | Box2D v3 | Box2D-Packed | Layout |
| :---- | :---- | :---- | :---- |
| b2BodyId, b2ShapeId, b2ChainId, b2JointId | 8 bytes | **4 bytes** | `uint16_t index1; uint16_t generation;` |
| b2ContactId | 12 bytes | **8 bytes** | `int32_t index1; uint32_t generation;` |
| b2WorldId | 4 bytes | 4 bytes | unchanged |

- `b2Store*Id` / `b2Load*Id` use `uint32_t` (`uint64_t` for contacts).
- Contact ids keep 32-bit fields: large piles exceed 65535 contacts, and contacts churn fast enough that a 16-bit generation would wrap.
- `B2_MAX_WORLDS` is 1 and enforced at compile time.

### Def structs and filters

- Flags are `bool name : 1`. A `bool` bit-field converts any non-zero value to 1, so `def.enableMotor = flags & MOTOR_BIT` works for any bit. A `uint8_t : 1` field keeps only the low bit and would silently store 0 for `0x2`.
- `internalValue` is `uint16_t`, and `B2_SECRET_COOKIE` is `0xB2D5`.
- Fields are ordered by alignment. Double-precision builds shrink by the same amounts.

| Struct | Box2D v3 | Box2D-Packed |
| :---- | :---- | :---- |
| b2Filter | 24 | **6** |
| b2QueryFilter | 16 | **4** |
| b2ExplosionDef | 32 | **24** |
| b2RayResult | 36 | **32** |
| b2WorldDef | 120 | **112** |
| b2BodyDef | 88 | **72** |
| b2ShapeDef | 88 | **56** |
| b2ChainDef | 88 | **64** |
| b2DistanceJointDef | 128 | **112** |
| b2PrismaticJointDef / b2RevoluteJointDef | 120 | **104** |
| b2WheelJointDef | 112 | **104** |
| b2MotionLocks | 3 | **1** |
| b2Shape (internal) | 280 | **264** |
| b2TreeProxy (internal) | 24 | **16** |

### Hot-path layout

- **Tree category bits live in the leaf node.** `b2TreeNode` has 8 bytes that 2D never used (3D uses them for AABB z). Leaf category bits now go there. Queries, ray casts, and box casts test the mask on the node they already loaded, and only leaves that pass read the proxy array. `b2TreeProxy` shrinks from 24 to 16 bytes.
- **`b2Shape` hot cache line.** Every field the broad-phase pair filter reads (`bodyId`, `sensorIndex`, `type`, `filter`, `generation`, and the flags) is in the first 64 bytes. It used to touch three cache lines per shape, and now touches one. `aabb` starts cache line 1.
- **No power-of-two stride.** `b2Shape` is 264 bytes, not 256. We measured a 256-byte version: the same field of every shape mapped to a quarter of the cache sets, and `large_pyramid` last-level data misses rose 27%. At 264 bytes each shape shifts by 8 bytes, and fields spread across all sets.

- **Narrow phase reads no shapes for awake contacts.** `b2CollideTask` read `shape->bodyId` for both shapes of every contact, two random cache lines, although the id is only needed in a rare fallback for sleeping bodies. It now reads the shape only in that fallback.
- **`b2ContactSim` is exactly 192 bytes.** The GJK simplex cache (8 bytes) moved to the cold `b2Contact` record, because only chain segment vs polygon/capsule reads it. At 192 bytes, every contact in the 64-byte aligned color arrays starts on a cache line boundary. At 200 bytes, most contacts' hot prefix straddled a fourth line.
- **`b2BodySim` collide-hot fields first.** `transform`, `center`, `invMass`, `invInertia`, `maxExtent`, and `flags` are now in the first 40 bytes instead of spread over all 96, so the collide task usually touches one cache line per body instead of two.

### Cachegrind: cache misses vs Box2D v3

Current Box2D-Packed compared with Box2D v3. Cachegrind simulates the cache, so these counts are exact and repeat identically run to run. Timing on shared hardware could not resolve differences this small.

Setup:
- Simulated caches: 32 KB 8-way L1, 8 MB 16-way last level, 64-byte lines.
- One worker, identical scenes, `-O3`.
- "Data misses" counts reads and writes.

| Benchmark | Instructions | L1 data misses | Last-level data misses |
| :---- | ----: | ----: | ----: |
| tile_world | -0.1% | -25.7% | **-65.3%** |
| queries | +0.2% | -8.6% | **-56.7%** |
| tree_cast | -0.4% | -2.8% | **-7.3%** |
| smash | +0.3% | -2.7% | **-5.4%** |
| large_pyramid | +0.2% | -3.6% | **-18.0%** |
| many_pyramids | +0.2% | -3.6% | **-1.6%** |
| joint_grid | +0.0% | -0.3% | **-4.3%** |

Negative is better. The biggest wins are in query-heavy scenes: filtered-out leaves no longer read the proxy array, and query callbacks read one shape cache line instead of three. `tree_cast` does not touch shapes, so its change comes from the tree node layout alone. The instruction count is essentially unchanged, so the savings are memory traffic, which matters more on real hardware with larger worlds and more threads.

### Cachegrind: narrow phase changes

Compared with the previous Box2D-Packed step, with the same Cachegrind setup:

| Benchmark | Instructions | L1 data misses | Last-level data misses |
| :---- | ----: | ----: | ----: |
| smash | -0.0% | -2.5% | +0.4% |
| large_pyramid | -0.1% | -3.3% | +6.1% |
| many_pyramids | -0.1% | -3.4% | -1.1% |
| tile_world | -0.0% | -1.0% | -0.0% |
| joint_grid | +0.0% | -0.0% | +0.0% |

The `large_pyramid` last-level increase is a side effect, not new traffic. The collide task's shape reads used to keep each shape's first line warm for `b2ComputeFatShapeAABB` later in the step. That scene's working set sits right at the 8 MB simulated cache size. In `many_pyramids`, the largest world, last-level misses drop by 3.7M, more than `large_pyramid` gains. `joint_grid` has no contacts and is unaffected.

The 192-byte `b2ContactSim` then changed nothing measurable except `large_pyramid` last-level misses, which fell 17.3%. The other four scenes moved by 0.1% or less. That scene's working set sits at the 8 MB cache boundary, so the 4% smaller contact arrays keep much more of it cached. Treat this as a working-set effect, not a general 17% gain.

### Investigated and not changed

- **Removing the inverse-mass copies from `b2ContactSim`.** The contact prep pass read them from the body sims instead. It was bit-identical and cut `large_pyramid` last-level misses 23%, but L1 misses rose 3.3% and instructions 0.6%, because prep now gathers body sims at random. `many_pyramids` came out net worse. Moving the simplex cache instead reached the same 192-byte size with no regressions.

- **Body simulation arrays.** In `large_pyramid`, `b2FinalizeBodiesTask` accounts for only about 2% of L1 data misses, and the integrate functions are not in the top 14. `b2BodyState` is already 32 bytes of hot fields, two per cache line.
- **Contact solver ordering.** The wide contact solver (`Solve`, `WarmStart`, `Push`) accounts for 64% of `large_pyramid` L1 read misses. We tested sorting each graph color's contacts by body index, which is safe because a dynamic body appears at most once per color, and all determinism tests passed. Solver misses were identical to the last digit: they are the sequential stream through the 592-byte wide constraints, not body-state gathers. Cachegrind has no hardware prefetcher, so it counts every line of that stream, but real CPUs hide most of it. The sort added 10% instructions for no gain, so it was dropped. Shrinking the constraint stream (about 148 bytes per contact) needs hardware counters on real machines to evaluate.

### Limits

- **One world at a time.** Destroy a world before creating the next one.
- **65535 live bodies, shapes, chains, and joints per world**, each counted separately. Creating one more logs an error and returns a null id instead of wrapping onto an existing object. A chain is rejected up front if its segments would not all fit.
- **16 collision categories.** `B2_DEFAULT_MASK_BITS` is `0xFFFF`. `groupIndex` is `int16_t`.
- `b2RayResult` visit counts are `uint16_t` and saturate at 65535.
- The recording format is 4.3 and the snapshot version is 14. Older `.b2rec` files and snapshots will not load.
- The standalone `b2DynamicTree` API keeps 64-bit category and mask bits for non-physics use.

### Benchmark: Box2D v3 vs Box2D-Packed (timing, before the hot-path layout changes)

This table was measured after the handle, def, and filter packing, before the tree node and `b2Shape` changes above. For those changes, see the Cachegrind results.

Setup:
- Upstream is commit `956ce4e`. Both builds use the same compiler and flags (`-O3 -DNDEBUG`, AVX2 off) and the same scenes.
- Times are the median of 6 runs, interleaved between builds, on one worker thread.
- `tile_world` is sized to 62,850 shapes in both builds to fit the handle limit.
- Both builds simulate identical body, shape, contact, and joint counts in every scene.

| Benchmark | Box2D v3 (ms) | Box2D-Packed (ms) | Time change (negative is faster) |
| :---- | ----: | ----: | ----: |
| compounds | 2554 | 2635 | +3.2% |
| joint_grid | 3334 | 3260 | -2.2% |
| junkyard | 4618 | 4587 | -0.7% |
| large_pyramid | 1825 | 1847 | +1.2% |
| many_pyramids | 2840 | 2856 | +0.5% |
| rain | 9836 | 9797 | -0.4% |
| smash | 1656 | 1663 | +0.5% |
| spinner | 5862 | 5794 | -1.2% |
| tumbler | 1800 | 1824 | +1.4% |
| washer | 6229 | 6218 | -0.2% |
| queries | 2573 | 2566 | -0.2% |
| tree_cast | 1970 | 1983 | +0.7% |
| tile_world | 741 | 745 | +0.6% |
| sleep | 3879 | 3900 | +0.5% |

**Result: simulation step time is unchanged.**
- Every scene is within ±3.2%, which is inside the run-to-run noise (±0.3–3.7%).
- The geometric mean is 0.25% slower, which is noise.

This is expected. The solver runs on internal integer indices and SoA solver sets. Handles, defs, and filters are only touched at the API boundary and during creation, so shrinking them does not change the step itself.

What the work so far does deliver:
- Half-size handles in game-side storage, such as ECS components and entity arrays.
- Handles passed in a single register.
- Smaller creation-time structs.

Reducing step time requires packing the internal hot-path data next: `b2Shape`, the body and contact simulation arrays, and the dynamic tree.

## **5\. Compiling User Code Into the Engine (`box2d_pack.py`)**

Full guide: [INTRUSIVENGINE.md](INTRUSIVENGINE.md). Paper: [paper/box2d_packed.pdf](paper/box2d_packed.pdf).

Game code usually learns about contacts after the step. It walks the event arrays, then calls `b2Shape_GetUserData` for each shape, a random memory read per event. `box2d_pack.py` lets game code run inside the engine instead, at the moment the event happens, while the shapes are still in cache.

The engine sources contain inert marker comments such as `//$b2Collide$CONTACT_BEGIN`. Normal CMake builds ignore them. `box2d_pack.py` copies the sources, replaces markers in the copies with your C code from a JSON file, and builds with gcc.

```sh
python3 box2d_pack.py                               # engine only -> /tmp/libbox2d.a
python3 box2d_pack.py usercode.c                    # engine + your main() -> /tmp/box2d
python3 box2d_pack.py usercode.c userinject.json    # same, with injected code
python3 box2d_pack.py --list-markers                # injection points and what is in scope
```

- Source copies go to `/tmp/box2d_src/` and objects to `/tmp/b2_*.o`. Unchanged files are not recompiled.
- Useful options: `--build-dir`, `-o`, `--lto`, `--debug`, `-D NAME=VALUE`, `--run`.
- With an injection file, the engine and your code are both built with `B2_PACK_INJECTED=1`.
- The repo is also a Python package: `import box2d; box2d.build( "usercode.c", "userinject.json" )`.

Injection file:

```json
{
  "defines": { "B2_PACK_NO_CONTACT_BEGIN_ARRAY": 1 },
  "globals": "void Game_OnContactBegin( void* userDataA, void* userDataB );",
  "inject": [
    { "event": "contact_begin", "code": "Game_OnContactBegin( shapeA->userData, shapeB->userData );" }
  ]
}
```

- Each entry names its target with `"marker"`, `"event"`, or `"function"` + `"point"`.
- Code comes from `"code"` (a string or a list of lines) or `"code_file"`.
- Compiler errors point at the JSON entry or snippet file, not at the patched copy.

| Event | Marker | Thread | In scope |
| :---- | :---- | :---- | :---- |
| `globals` | `pack_hooks$GLOBALS` | | file scope in every engine file with markers, declarations only |
| `pre_step` | `b2World_Step$HEADER` | main | `world`, `worldId`, `timeStep`, `subStepCount`. The world is unlocked. |
| `post_step` | `b2World_Step$FOOTER` | main | `world`, `worldId`, `timeStep`. The world is unlocked. |
| `contact_begin` | `b2Collide$CONTACT_BEGIN` | single | `shapeA`, `shapeB` (with `->userData`), shape ids, `contactFullId`, `contactSim->manifold` |
| `contact_end` | `b2PackContactEnd$CONTACT_END` | single | `shapeA`, `shapeB`, shape ids, `contactFullId` |
| `contact_hit` | `b2Solve$CONTACT_HIT` | single | `shapeA`, `shapeB`, `event` (point, normal, approach speed) |
| `sensor_begin` | `b2PackSensorBegin$SENSOR_BEGIN` | single | `sensorShape`, `visitorShape`, ids |
| `sensor_end` | `b2PackSensorEnd$SENSOR_END` | single | `sensorShape`, `visitorShape` (`NULL` if destroyed), ids |
| `custom_filter` | `b2PackCustomFilter$FILTER` | **workers** | `shapeA`, `shapeB`, `shouldCollide` |
| `pre_solve` | `b2UpdateContact$PRE_SOLVE` | **workers** | `world`, `shapeA`, `shapeB`, `contactSim->manifold` |
| `body_gravity` | `b2PackBodyGravity$BODY_GRAVITY` | **workers** | `world`, `sim`, `gravityScale`, `bodyGravity` |

`B2_PACK_NO_*` defines turn off the engine's own event arrays and callbacks when injected code replaces them. See [INTRUSIVENGINE.md](INTRUSIVENGINE.md) for the list, the threading rules, and `test/pack/run_pack_tests.py`, which checks that every marker behaves exactly like the API it replaces.

### Demo: arena brawl (`examples/pack_game`)

3,000 units on two teams bounce around an arena for 600 steps. Each shape's `userData` points at a 192-byte game object, shuffled in memory. Opposite teams damage each other on contact begin. The same `game.c` builds both ways, and both print the same checksum, so the game logic ran identically.

Cachegrind, standard events vs injected, 72,943 begin events:

| | Standard events | Injected | Difference |
| :---- | ----: | ----: | ----: |
| Instructions | 3,376.1M | 3,370.8M | -0.16% |
| L1 data misses | 75.88M | 75.50M | -0.50% |
| Last-level data misses | 84,080 | 84,081 | 0 |

Each injected event saves about 74 instructions and 5.2 L1 data misses: the array write, the array walk, and two user data lookups. The whole-program total barely moves because the physics step dominates.

### Demo: bullet storm (`examples/pack_bullets`)

20,000 bullets fly into 4,800 static targets. Each hit damages the target and recycles the bullet, 2,112 contact begin events per step. Both builds print the same checksum. Cachegrind, 120 steps:

| | Standard events | Injected | Difference |
| :---- | ----: | ----: | ----: |
| Instructions | 10,644.6M | 10,627.8M | -0.16% |
| L1 data misses | 138.48M | 138.12M | -0.26% |
| Last-level data misses | 41.41M | 41.66M | +0.62% |

With 17 times the arena's event rate, the instruction saving stays at 0.16%, about 66 per event, because each event carries much larger engine work. Last-level misses rise. The handler now touches game objects in the middle of the step, which evicts engine data, and the game's post-step recycle pass no longer finds those objects in cache. Injection pays off when the handler uses engine data that is hot at the marker, or records little. It does not when the game does follow-up work on the same objects after the step. See [INTRUSIVENGINE.md](INTRUSIVENGINE.md).

### Demo: platformer crowd pre-solve (`examples/pack_presolve`)

12,000 circles fall through staggered one-way platforms. Every contact runs a pre-solve handler that reads both shapes' user data. Ghosts pass through everything, and platforms let bodies pass upward. Sleep and contact recycling are off, because Box2D skips pre-solve for recycled contacts. All four builds print the same checksum. Cachegrind, 100 steps:

| Build | Instructions | L1 data misses | Last-level data misses |
| :---- | ----: | ----: | ----: |
| Standard callback | baseline | baseline | baseline |
| Standard + `--lto` | -5.86% | -0.01% | -0.07% |
| Injected | -1.33% | -0.20% | -0.84% |
| Injected + `--lto` | -6.77% | -0.20% | -0.92% |

Injection removes the callback wrapper and two `b2Shape_GetUserData` lookups per contact. That saves about 1% of instructions, with or without `--lto`, and lowers last-level misses. The handler reads user data while the engine holds that contact's shapes, and no game pass after the step touches the same objects. Link-time optimization is the larger, separate effect: compiling the engine and game as one program saves 5.9% of instructions even without injection.

### Demo: platformer (`examples/pack_platformer`)

1,500 AI runners race across a level of one-way platforms. They collect coins and feathers, which grant low gravity, stomp patrolling enemies or get hurt by them, and take hard landings. The rules are `static inline` functions in `platformer.h`. `inject.json` includes that header and injects four of them, so they run inline inside the engine:

| Rule | Standard build | Injected at |
| :---- | :---- | :---- |
| One-way platforms | pre-solve callback | `pre_solve` |
| Coins and feathers | sensor begin events | `sensor_begin` |
| Stomp or get hurt | contact begin events + `b2Contact_GetData` | `contact_begin` |
| Hard landings | hit events | `contact_hit` |

Feather gravity uses `b2Body_SetGravityScale` in both builds. All variants print the same checksum. Cachegrind, 300 steps:

| Build | Instructions | L1 data misses | Last-level data misses |
| :---- | ----: | ----: | ----: |
| Standard | baseline | baseline | baseline |
| Standard + `--lto` | -3.24% | -0.02% | -0.05% |
| Injected | -1.05% | **-2.05%** | +0.01% |
| Injected + `--lto` | -3.95% | **-2.06%** | -0.06% |
| Injected, gravity in `body_gravity` too | +0.07% | **+7.65%** | +0.01% |

Injecting the event rules cuts L1 data misses 2%, the largest injection gain measured so far, and it holds on top of `--lto`. `inject_gravity_hook.json` also moves the feather gravity into the `body_gravity` marker, and that costs more than all the event rules save. The hook reads cold game data for every body in every substep, while `b2Body_SetGravityScale` stores the value in the engine's hot body data. Inject event handlers. Keep per-body, per-substep hooks for rules that need no game data.

## **6\. unity_pack Physics Backend (`box2d_unity.py`)**

crust's [`tools/unity_pack.py`](https://github.com/brentharts/crust) packs a Unity-shaped project (C# scripts and `.unity` scenes) into C. Its 2D physics is Box2D-Packed, through `box2d_unity.py`. unity_pack finds this checkout through `--box2d PATH`, `$BOX2D_PACKED_ROOT`, or a `box2d` directory beside crust:

```sh
python3 tools/unity_pack.py <project> -o /tmp/out
python3 tools/unity_pack.py <project> -o /tmp/out --physics-inject --box2d /path/to/box2d
```

- `box2d_unity.py` generates `physics_box2d.c` from unity_pack's Rigidbody2D and Collider2D tables. Each fixed step, it creates bodies for new Rigidbody2D components (including `AddComponent<Rigidbody2D>`), pushes script changes into the Box2D world, steps it, and pulls positions and velocities back.
- unity_pack keeps sending `OnCollisionEnter2D` / `Stay2D` / `Exit2D` after the step, from Box2D's touching pairs.
- `--physics-inject` records those pairs at the `contact_begin` and `contact_end` injection markers instead of the event arrays.
- Friction and bounciness combine as in Unity, through Box2D's material callbacks. Triggers become sensors. Body rotation is locked, because packed rigidbodies have no rotation yet.

`test/unity/run_unity_tests.py --crust PATH` packs `test/unity/Bounce` (a ball bouncing three times and a stack of six crates) with Box2D event arrays and with `--physics-inject`. Both give Unity's 4 Enter and 3 Exit messages, and the injected build matches the standard build exactly. Before unity_pack's own 2D physics was removed, it gave the same 4 Enter and 3 Exit.

This integration found a bug in the `contact_end` marker. When a fast body leaves a contact, Box2D destroys the contact instead of reporting that it stopped touching, and that path had no marker. Both paths now go through one hook, `b2PackContactEnd`, and `test/pack/markers.c` checks contact begin and end counts.

# Box2D 

Box2D is a 2D physics engine for games.

![Box2D Logo](https://box2d.org/images/logo.svg)

[![Box2D Version 3.0 Release Demo](https://img.youtube.com/vi/dAoM-xjOWtA/0.jpg)](https://www.youtube.com/watch?v=dAoM-xjOWtA)

## Build Status

[![Build Status](https://github.com/erincatto/box2d/actions/workflows/build.yml/badge.svg)](https://github.com/erincatto/box2d/actions)

## Features

### Collision

- Continuous collision detection
- Contact events
- Convex polygons, capsules, circles, rounded polygons, segments, and chains
- Multiple shapes per body
- Collision filtering
- Ray casts, shape casts, and overlap queries
- Sensor system

### Physics

- Robust _Soft Step_ rigid body solver
- Continuous physics for fast translations and rotations
- Island based sleep
- Revolute, prismatic, distance, mouse joint, weld, and wheel joints
- Joint limits, motors, springs, and friction
- Joint and contact forces
- Body movement events and sleep notification

### System

- Data-oriented design
- Written in portable C17
- Extensive multithreading and SIMD
- Optimized for large piles of bodies

### Samples

- OpenGL with GLFW
- Graphical user interface with imgui
- Many samples to demonstrate features and performance

## Building All Platforms

- Install [CMake](https://cmake.org/)
- Install [git](https://git-scm.com/)
- Ensure these run from the command line

## Building with CMake presets

The presets in `CMakePresets.json` give one build flow on every platform and are picked up automatically by Visual Studio, VS Code, and CLion (open the folder and choose a preset). From the command line:

- Windows: `cmake --preset windows` then `cmake --build --preset windows-release`
- Linux: `cmake --preset linux-release` then `cmake --build --preset linux-release`
- macOS: `cmake --preset macos` then `cmake --build --preset macos-release`

Use the `*-debug` build presets for a debug build (not recommended for the replay viewer). The presets use the default native toolchain (the installed Visual Studio on Windows, Make on Linux, Xcode on macOS), so no specific compiler version is required.

## Building for Visual Studio

- Install [Visual Studio](https://visualstudio.microsoft.com/)
- Run `build_vs2026.bat` for Visual Studio 2026, or use the `windows` preset above for other versions
- Open and build the generated solution in the `build` folder

## Building for Linux

- Run `build.sh` from a bash shell
- Results are in the build sub-folder

## Building for Xcode

- mkdir build
- cd build
- cmake -G Xcode ..
- Open `box2d.xcodeproj`
- Select the samples scheme
- Build and run the samples

## Building and installing

- mkdir build
- cd build
- cmake ..
- cmake --build . --config Release
- cmake --install . (might need sudo)

Installing also provides a pkg-config file, so `pkg-config --modversion box2d` reports the installed version and `pkg-config --cflags --libs box2d` gives the build flags.

## Building with zig

Fetch and link Box2D from a Zig project:

- `zig fetch --save git+https://github.com/erincatto/box2d`

In `build.zig`:

```zig
const box2d_dep = b.dependency("box2d", .{});
exe.root_module.addImport("box2d", box2d_dep.module("box2d"));
```

In Zig code, start using box2d

```zig
const box2d = @import("box2d");

pub fn main(init: std.process.Init) !void {
    var world_def = box2d.b2DefaultWorldDef();
    world_def.gravity.y = 9.8;
    const world_id = box2d.b2CreateWorld(&world_def);
}
```

## Building with Swift Package Manager

Add box2d as a dependency in your `Package.swift`:

```swift
.package(url: "https://github.com/erincatto/box2d.git", from: "main")
```

And add the product to your target:

```swift
.target(
    name: "MyGame",
    dependencies: [.product(name: "box2d", package: "box2d")]
),
```

In Swift code, the C API is imported directly:

```swift
import box2d

var worldDef = b2DefaultWorldDef()
let worldId = b2CreateWorld(&worldDef)
```

C structs are passed as `inout` arguments using `&`.

## Replay viewer

The samples app doubles as a viewer for Box2D recordings (`.b2rec` files). Any preset above builds it. Pass a recording on the command line to open it directly:

- Windows: `build\bin\Release\samples.exe path\to\session.b2rec`
- Linux: `build/bin/samples path/to/session.b2rec`
- macOS: `build/bin/Release/samples path/to/session.b2rec`

On Windows you can also drag a `.b2rec` file onto `samples.exe`. The viewer runs from any directory. Without an argument, open a recording from the **Replay** menu. See [docs/recording.md](docs/recording.md) for how to make a recording.

## Compatibility

The Box2D library and samples build and run on Windows, Linux, and Mac.

You will need a compiler that supports C17 to build the Box2D library.

You will need a compiler that supports C++20 to build the samples.

Box2D uses SSE2 and Neon (AArch64) SIMD math to improve performance. This can be disabled by defining `BOX2D_DISABLE_SIMD`.

## Documentation

- [Manual](https://box2d.org/documentation/)
- [Migration Guide](https://github.com/erincatto/box2d/blob/main/docs/migration.md)

## Community

- [Discord](https://discord.gg/NKYgCBP)

## Contributing

Please do not submit pull requests. Instead, please file an issue for bugs or feature requests. For support, please visit the Discord server.

## Giving feedback

Please file an issue or start a chat on discord. You can also use [GitHub Discussions](https://github.com/erincatto/box2d/discussions).

## License

Box2D is developed by Erin Catto and uses the [MIT license](https://en.wikipedia.org/wiki/MIT_License).

## Sponsorship

Support development of Box2D through [Github Sponsors](https://github.com/sponsors/erincatto).

Please consider starring this repository and subscribing to my [YouTube channel](https://www.youtube.com/@erin_catto).

## LLM Usage

LLMs are used in the following areas:

- unit tests
- samples app
- migrating code between Box2D and Box3D
- build configuration
- code reviews
- benchmarking

Elsewhere all code is developed and written by me. I take responsibility for every line of code in Box2D/3D.

## External ports, wrappers, and bindings (unsupported)

- Beef bindings - https://github.com/EnokViking/Box2DBeef
- C++ bindings - https://github.com/HolyBlackCat/box2cpp
- WASM - https://github.com/Birch-san/box2d3-wasm
