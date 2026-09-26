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
