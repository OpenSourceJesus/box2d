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

### Cachegrind: cache misses vs Box2D v3

Cachegrind simulates the cache, so these counts are exact and repeat identically run to run. Timing on shared hardware could not resolve differences this small.

Setup:
- Simulated caches: 32 KB 8-way L1, 8 MB 16-way last level, 64-byte lines.
- One worker, identical scenes, `-O3`.
- "Data misses" counts reads and writes.

| Benchmark | Instructions | L1 data misses | Last-level data misses |
| :---- | ----: | ----: | ----: |
| tile_world | -0.1% | -25.0% | **-65.3%** |
| queries | +0.2% | -7.0% | **-60.4%** |
| tree_cast | -0.4% | -2.9% | **-7.3%** |
| smash | +0.2% | -0.1% | **-5.8%** |
| large_pyramid | +0.3% | -0.3% | **-6.6%** |

Negative is better. The biggest wins are in query-heavy scenes: filtered-out leaves no longer read the proxy array, and query callbacks read one shape cache line instead of three. `tree_cast` does not touch shapes, so its change comes from the tree node layout alone. The instruction count is essentially unchanged, so the savings are memory traffic, which matters more on real hardware with larger worlds and more threads.

### Investigated and not changed

- **Body simulation arrays.** In `large_pyramid`, `b2FinalizeBodiesTask` accounts for only about 2% of L1 data misses, and the integrate functions are not in the top 14. `b2BodyState` is already 32 bytes of hot fields, two per cache line.
- **Contact solver ordering.** The wide contact solver (`Solve`, `WarmStart`, `Push`) accounts for 64% of `large_pyramid` L1 read misses. We tested sorting each graph color's contacts by body index, which is safe because a dynamic body appears at most once per color, and all determinism tests passed. Solver misses were identical to the last digit: they are the sequential stream through the 592-byte wide constraints, not body-state gathers. Cachegrind has no hardware prefetcher, so it counts every line of that stream, but real CPUs hide most of it. The sort added 10% instructions for no gain, so it was dropped. Shrinking the constraint stream (about 148 bytes per contact) needs hardware counters on real machines to evaluate.

### Limits

- **One world at a time.** Destroy a world before creating the next one.
- **65535 live bodies, shapes, chains, and joints per world**, each counted separately. Creating one more logs an error and returns a null id instead of wrapping onto an existing object. A chain is rejected up front if its segments would not all fit.
- **16 collision categories.** `B2_DEFAULT_MASK_BITS` is `0xFFFF`. `groupIndex` is `int16_t`.
- `b2RayResult` visit counts are `uint16_t` and saturate at 65535.
- The recording format is 4.2 and the snapshot version is 13. Older `.b2rec` files and snapshots will not load.
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
