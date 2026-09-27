# Intrusive Engine

Box2D-Packed can compile your game code into the physics engine. `box2d_pack.py` copies the engine sources, puts your C code at marked points inside the engine, and builds everything together. Your game logic then runs where the engine produces an event, instead of after the step through event arrays and lookups.

This document covers why this helps, how the pipeline works, how to write injections, and how to add new injection points to the engine.

## Why

Box2D reports contacts through event arrays. The usual game loop looks like this:

```c
b2World_Step( worldId, dt, 4 );

b2ContactEvents events = b2World_GetContactEvents( worldId );
for ( int i = 0; i < events.beginCount; ++i )
{
    GameObject* a = b2Shape_GetUserData( events.beginEvents[i].shapeIdA );
    GameObject* b = b2Shape_GetUserData( events.beginEvents[i].shapeIdB );
    OnContactBegin( a, b );
}
```

For every event, the engine writes an entry to the array during the step. After the step, the game reads the entry back and resolves two shape ids to user data. Resolving an id is a random read into the shape array, and by then the shape has usually left the cache.

Inside the engine, at the moment a contact begins, both shapes are already loaded, and `userData` sits in the first cache line of each shape (see `src/shape.h`). An injected handler reads it for free:

```c
// injected at b2Collide$CONTACT_BEGIN
OnContactBegin( shapeA->userData, shapeB->userData );
```

Measured in the arena brawl demo, this saves about 74 instructions and 5 L1 data misses per begin event. It also removes the event array entirely when you define `B2_PACK_NO_CONTACT_BEGIN_ARRAY`. How much that matters depends on how many events your game produces per step. See "When it helps" below.

## Quick start

```sh
python3 box2d_pack.py --list-markers
python3 box2d_pack.py examples/pack_game/game.c                                   # standard build
python3 box2d_pack.py examples/pack_game/game.c examples/pack_game/inject.json   # injected build
python3 box2d_pack.py examples/pack_game/game.c examples/pack_game/inject.json --lto --run
```

The only requirement on your code is that it defines `main()`. From Python, with the directory containing the repo on `sys.path`:

```python
import box2d
result = box2d.build( "game.c", "inject.json", out="/tmp/game" )
print( result.exe )
```

## Pipeline

```
flowchart LR
    A[Engine sources with marker comments] --> C[Copy to /tmp/box2d_src]
    B[inject.json+ snippet files] --> D[Replace markers in the copies]
    C --> D
    D --> E[gcc: engine objects<br/>/tmp/b2_*.o]
    E --> F[/tmp/libbox2d.a]
    G[usercode.c] --> H[gcc: link]
    F --> H
    H --> I[/tmp/box2d]
```

1. **Find markers.** Scan `src/` and `include/box2d/` for lines of the form `//$scope$POINT`. Marker names are unique across the engine.
2. **Load the injection file.** Parse the JSON, resolve every entry to a marker name, and read `code_file` snippets relative to the JSON file.
3. **Patch in memory.** Replace each marker line with the injected code. Code at a function marker is wrapped in braces, so it gets its own scope. Code at a `GLOBALS` marker is placed verbatim at file scope. Several entries for one marker are placed in file order.
4. **Write the copies.** Write the sources to `BUILD_DIR/box2d_src/`, touching only files whose contents changed. The repository is never modified.
5. **Compile.** Compile each engine `.c` file to `BUILD_DIR/b2_<name>.o` in parallel. An object is rebuilt only if its source, the headers, or the flags changed. The objects are archived into `BUILD_DIR/libbox2d.a`.
6. **Link.** When a user file is given, it is compiled with the same flags and linked against the library into `BUILD_DIR/box2d`.

With an injection file, both the engine and your code are compiled with `B2_PACK_INJECTED=1`, so one source file can support both builds:

```c
#if B2_PACK_INJECTED == 0
    // standard API: walk the event arrays after the step
#endif
```

### Options

| Option | Default | Meaning |
| :---- | :---- | :---- |
| `--build-dir DIR` | `/tmp` | Copies go to `DIR/box2d_src`, objects to `DIR/b2_*.o` |
| `-o PATH` | `DIR/box2d` | Executable |
| `--lib PATH` | `DIR/libbox2d.a` | Static library |
| `--opt FLAG` | `-O3` | Optimization level |
| `--lto` | off | Link-time optimization, lets gcc inline your functions into the engine |
| `--debug` | off | `-O0 -g`, Box2D asserts enabled |
| `-D NAME[=VALUE]` | | Extra define for engine and user code |
| `-j N` | CPU count | Parallel compile jobs |
| `--run` | off | Run the executable after building |
| `--list-markers` | | Print every marker, its location, and what is in scope |
| `-v` | off | Print compiler commands |

## Injection file

```json
{
  "comment": "anything, ignored",
  "defines": { "B2_PACK_NO_CONTACT_BEGIN_ARRAY": 1 },
  "cflags": [ "-march=native" ],
  "globals": "void OnContactBegin( void* a, void* b );",
  "inject": [
    { "event": "contact_begin", "code": "OnContactBegin( shapeA->userData, shapeB->userData );" },
    { "marker": "b2World_Step$FOOTER", "code_file": "post_step.c" },
    { "function": "b2World_Step", "point": "HEADER", "code": [ "g_stepCount += 1;" ] }
  ]
}
```

| Key | Type | Meaning |
| :---- | :---- | :---- |
| `defines` | object | Preprocessor defines for engine and user code. `null` or a value. |
| `cflags` | list of strings | Extra compiler flags. |
| `globals` | string | Shorthand for an entry at `physics_world$GLOBALS`. |
| `inject` | list | Injection entries, applied in order. |
| `comment` | any | Ignored. |

Each `inject` entry has exactly one target and one code source.

| Target | Example |
| :---- | :---- |
| `"marker"` | `"b2Collide$CONTACT_BEGIN"` |
| `"event"` | `"contact_begin"`, see the table below |
| `"function"` + `"point"` | `"b2World_Step"` + `"FOOTER"` |

| Code source | Meaning |
| :---- | :---- |
| `"code"` | A string, or a list of lines. JSON has no multi-line strings. |
| `"code_file"` | A C snippet file, relative to the JSON file. Better for anything longer than a line or two. |

Unknown keys, unknown markers, and unknown events are errors. An unknown marker lists the markers in the same scope, so typos are easy to fix.

## Injection points

| Event | Marker | Runs | In scope |
| :---- | :---- | :---- | :---- |
| `globals` | `pack_hooks$GLOBALS` | File scope of `src/pack_hooks.h`, which every engine file with markers includes | Declarations only. Declare your functions and `extern` variables here. A definition would be duplicated in each file. |
| `pre_step` | `b2World_Step$HEADER` | Start of every step, world unlocked | `world`, `worldId`, `timeStep`, `subStepCount` |
| `contact_begin` | `b2Collide$CONTACT_BEGIN` | When two shapes with contact events enabled start touching. Single threaded, world locked. | `shapeA`, `shapeB` (`const b2Shape*`, use `->userData`), `shapeIdA`, `shapeIdB`, `contactFullId`, `contactSim->manifold` |
| `contact_end` | `b2Collide$CONTACT_END` | When they stop touching. Single threaded, world locked. | `shapeA`, `shapeB`, `shapeIdA`, `shapeIdB`, `contactFullId` |
| `contact_hit` | `b2Solve$CONTACT_HIT` | For each hit event (approach speed above the world's hit threshold). Single threaded, world locked. | `shapeA`, `shapeB` (`b2Shape*`), `event` (`b2ContactHitEvent`: `point`, `normal`, `approachSpeed`, ids) |
| `sensor_begin` | `b2PackSensorBegin$SENSOR_BEGIN` | When a shape starts overlapping a sensor. Single threaded, world locked. | `sensorShape`, `visitorShape` (`const b2Shape*`), `sensorId`, `visitorId` |
| `sensor_end` | `b2PackSensorEnd$SENSOR_END` | When it stops overlapping. Single threaded, world locked. | `sensorShape`, `visitorShape` (`NULL` if the visitor was destroyed), `sensorId`, `visitorId` |
| `custom_filter` | `b2PackCustomFilter$FILTER` | **Worker threads.** For pairs where either shape enables custom filtering, in the broad phase, sensor overlaps, and continuous collision. | `shapeA`, `shapeB` (`const b2Shape*`), `shouldCollide` (set `false` to reject the pair) |
| `pre_solve` | `b2UpdateContact$PRE_SOLVE` | **Worker threads.** For touching contacts where either shape enables pre-solve events, when the narrow phase updates them. Recycled contacts skip the update, so pre-solve does not run for them, the same as the callback. | `world`, `shapeA`, `shapeB` (`b2Shape*`), `contactSim` (change `->manifold`, set `->manifold.pointCount = 0` to disable the contact this step) |
| `post_step` | `b2World_Step$FOOTER` | End of every step, world unlocked, events available | `world`, `worldId`, `timeStep` |

These defines turn off the engine's own path when injected code replaces it:

| Define | Effect |
| :---- | :---- |
| `B2_PACK_NO_CONTACT_BEGIN_ARRAY` | `b2World_GetContactEvents` returns no begin events |
| `B2_PACK_NO_CONTACT_END_ARRAY` | `b2World_GetContactEvents` returns no end events |
| `B2_PACK_NO_CONTACT_HIT_ARRAY` | `b2World_GetContactEvents` returns no hit events |
| `B2_PACK_NO_SENSOR_BEGIN_ARRAY` | `b2World_GetSensorEvents` returns no begin events |
| `B2_PACK_NO_SENSOR_END_ARRAY` | `b2World_GetSensorEvents` returns no end events from the step. End events from destroying a sensor or visitor shape still go to the array. |
| `B2_PACK_NO_CUSTOM_FILTER_FCN` | The filter set with `b2World_SetCustomFilterCallback` is not called |
| `B2_PACK_NO_PRE_SOLVE_FCN` | The callback set with `b2World_SetPreSolveCallback` is not called |

Without an injection, the filter and pre-solve markers compile to nothing, and normal builds are unchanged: the determinism hash is identical. `python3 box2d_pack.py --list-markers` is always the current list, and marks worker-thread markers with `[WORKER THREADS]`.


## Rules for injected code

Injected code runs inside the engine, so it must follow the engine's rules.

1. **Respect the lock.** During the contact markers, the world is locked, just as during callbacks. Do not create or destroy bodies, shapes, or joints there, and do not call functions that change the world. Queue the work and do it in `post_step` or after `b2World_Step` returns. The arena brawl demo queues respawns this way.
2. **Respect threading.** `custom_filter` and `pre_solve` run on worker threads, possibly on several at once. Code there must be thread-safe: read shared data, write only to the contact it was given, or use atomics. `box2d_pack.py` prints a note whenever it injects into a worker-thread marker. All other markers run on a single thread.
3. **Use what is documented in scope.** The in-scope variables are part of the marker's contract. Other engine locals and struct fields are internal and may change between versions. For game data, prefer `shape->userData`.
4. **Do not keep engine pointers.** `shapeA`, `contactSim`, and similar pointers point into arrays that move when they grow. Use them during the injected code only. Store ids if you need something later.
5. **Keep physics deterministic.** Reading engine state never changes the simulation. If injected code changes physics state, it must do so deterministically, or replays and cross-platform determinism break.
6. **Keep it short.** Code at a hot marker runs inside the engine loop. Call a function for anything longer than a few lines. With `--lto`, gcc can still inline it.

## Testing injections

`test/pack/run_pack_tests.py` checks that the markers behave exactly like the API they replace. `test/pack/markers.c` uses sensors, hit events, a custom filter, and a pre-solve one-way platform. The test builds it three ways:

- **Standard:** callbacks and event arrays.
- **Injected:** `test/pack/markers.json`, with the callbacks and arrays compiled out.
- **Control:** without the filter and pre-solve rules.

The standard and injected builds must print identical output, including a hash of every body's final position. The control build must print a different hash, which shows the test detects the rules. Test your own injections the same way: one source file, built both ways with `B2_PACK_INJECTED`, with identical results.

## Debugging

- **Compiler errors point at your code.** Injected code is preceded by a `#line` directive, so `inject.json:inject[0]:1:9: error: ...` means line 1, column 9 of the first entry's code. Snippets from `code_file` report their own file and line.
- **Read the patched source.** Every injection is bracketed by `/* box2d_pack: begin <marker> */` in `BUILD_DIR/box2d_src/`.
- **Build with `--debug`.** It enables Box2D's asserts, which catch many misuses, such as world changes while locked.
- **Use `-v`.** It prints every compiler command.

## When it helps

Each injected event saves the event path: the array write during the step, the array walk after it, and two random shape lookups. Two demos measure it under Cachegrind, and both builds of each produce identical game results.

| Demo | Events per step | Instructions | L1 data misses | Last-level data misses |
| :---- | ----: | ----: | ----: | ----: |
| Arena brawl, `examples/pack_game` | 121 | -0.16% (74 per event) | -0.50% | 0% |
| Bullet storm, `examples/pack_bullets` | 2,112 | -0.16% (66 per event) | -0.26% | **+0.62%** |
| Platformer crowd pre-solve, `examples/pack_presolve` | about 40,000 contacts | -1.33% | -0.20% | **-0.84%** |

The platformer crowd replaces a pre-solve callback that runs for every touching contact every step. It is the best fit measured so far. Injection saves 1.3% of instructions and 0.84% of last-level misses: the handler reads user data while the engine holds that contact's shapes, and no game pass after the step touches the same objects. Link-time optimization (`--lto`) is a separate and larger effect, covered below.

Two lessons come from the event demos.

**The event path is a small share, even with many events.** The bullet storm has 17 times the arena's event rate, yet saves the same 0.16% of instructions. Each event there comes with much larger engine work: a contact is created and destroyed, and the recycled bullet's broad-phase proxy moves. The saving per event is real and consistent, about 70 instructions, but it is small next to that work.

**Injection moves game work away from the game's own hot data.** In the bullet storm, last-level misses went up. The event walk and user data lookups disappeared, saving about 190,000 last-level misses. But the handler now touches game objects in the middle of the step, which costs about 450,000 misses elsewhere:

- The game's post-step recycle pass writes to the same bullets. In the standard build, the event walk had just loaded them. In the injected build they were loaded mid-step and evicted by the rest of the step.
- Game objects loaded mid-step evict engine data. Tree updates and contact creation miss more.

Guidelines that follow from this:

| Situation | Expect |
| :---- | :---- |
| The handler uses engine data, such as the manifold, normal, or approach speed | Good fit. That data is hot at the marker. |
| The handler only records something small, such as a counter, a flag, or an id in a queue | Good fit. Little game data is pulled into the step. |
| The game does follow-up work on the same objects after the step | Weak or negative. Keep that work together, or queue ids and do it all after the step. |
| Game logic called through a function pointer per pair or contact (`custom_filter`, `pre_solve`) | Good fit, with `--lto`. The indirect call is removed. It is still worker-thread code. |

### Link-time optimization

`--lto` compiles the engine and the game as one program, so gcc can inline across Box2D's own source files as well as into user code. In the platformer crowd, measured with Cachegrind:

| Build | Instructions | L1 data misses | Last-level data misses |
| :---- | ----: | ----: | ----: |
| Standard callback | baseline | baseline | baseline |
| Standard + `--lto` | -5.86% | -0.01% | -0.07% |
| Injected | -1.33% | -0.20% | -0.84% |
| Injected + `--lto` | -6.77% | -0.20% | -0.92% |

Most of the instruction saving comes from link-time optimization of the engine itself, not from injection. Injection adds about 1% on top of it. Both are worth having, and `--lto` helps even without an injection file. Wall-clock medians over five interleaved runs agreed in direction: -1.8% for standard + `--lto` and -2.8% for injected + `--lto`. That machine's run-to-run noise is several percent, so treat the times as supporting evidence.

Measure your own game. Build it both ways from one source with `B2_PACK_INJECTED`, check that both builds produce the same results, and compare them.

## Adding an injection point

Markers are cheap to add and inert in normal builds. To add one to the engine:

1. **Place it on its own line**, indented like the code around it, in the form `//$scope$POINT`. Use the enclosing function name as the scope, and an upper-case point name such as `HEADER`, `FOOTER`, or an event name. Use `<file>$GLOBALS` for file scope. Names must be unique across the engine.
2. **Document it** in the comment lines directly above. `--list-markers` prints them. State the thread context, the lock state, and every variable the injected code may use.
3. **Make the in-scope variables real.** They must be declared and valid at that point in every build configuration, including validation and double precision builds.
4. **Consider an array guard.** If the marker replaces an event array, guard the array write with a `B2_PACK_NO_...` define, as the contact markers do.
5. **Add an event alias** in `EVENTS` in `box2d_pack.py` if the point is generally useful.
6. **Test both builds.** A normal CMake build must not change, and the unit tests must pass. An injected build of a small program that uses the marker must compile and produce the same results as the standard API.

## Roadmap

Done: sensor begin and end, hit events, custom filter, and pre-solve markers, with equivalence tests, and the bullet storm and platformer crowd benchmarks.

- **Hardware measurement.** Time and hardware counters on real multi-core machines, where prefetching and larger caches may change the balance measured above.
- **Worker-thread tests.** Run the equivalence test with several workers, to check injected filter and pre-solve code under real concurrency.
- **Custom filter benchmark.** The filter runs once per new candidate pair, so it needs a scene with heavy pair churn.
- **Link-time optimization in CMake.** Measure the engine-wide `--lto` gain on the standard benchmark suite, independent of injection.
- **Sensor-heavy benchmark.** Pickups and trigger volumes at scale.
