## Box2D-Packed: triggers and the Rigidbody2D API

2D physics is Box2D-Packed (`box2d_unity.py` in its checkout generates
`physics_box2d.c`, which steps a Box2D world over the packed tables).

**Triggers.** `OnTriggerEnter2D`, `OnTriggerStay2D` and `OnTriggerExit2D
(Collider2D other)` are sent -- Unity mode's trigger colliders were Box2D
sensors that told no one. When a script has one, the plan's
`physics2d_triggers` has the glue enable sensor events and report each
step's overlapping sensor pairs with `engine_col2d_trigger(a, b)`, apart
from the touching pairs; the engine sends Enter / Stay / Exit by comparing
them with the step before, as it does collisions. With `--physics-inject`
the sensor begin / end events are injected into Box2D-Packed, as the
contacts are.

**The other collider.** In a collision or trigger handler, the parameter
-- `Collision2D coll` or `Collider2D other`, the other collider's index --
reads its GameObject: `other.gameObject` (`_col2d_go`), and its `name`,
`tag`, `CompareTag(..)`, `GetComponent<T>()`, `SetActive(..)` and
`Destroy(other.gameObject)`; on a Collider2D those members are its own,
and mean the same. `gameObject.CompareTag(..)` / `.tag` -- this object's,
a bare `CompareTag(..)`, or a GameObject variable's -- read the authored
`m_TagString` (`_engine_go_tag`).

**Rigidbody2D.** On a Rigidbody2D field, local or
`GetComponent<Rigidbody2D>()`, as the engine's `Rigidbody2D_*` over the
tables the glue pushes before each step:

| C# | |
|----|--|
| `AddForce(F)`, `AddForce(F, ForceMode2D.Impulse)` | the velocity change Unity's step makes: F·dt/m, F/m; not on a body that is not dynamic |
| `position`, `position = V`, `MovePosition(V)` | the owner's position; a write is a teleport (Unity moves a kinematic body through space) |
| `mass`, `gravityScale`, `drag` / `linearDamping`, `bodyType`, `isKinematic` | get, set, `op=` |
| `velocity` / `linearVelocity` | as before |

The glue now pushes a changed `mass` (the shape's mass data scaled to it,
as at creation) and a changed `bodyType` (`b2Body_SetType`); it pushed
velocity, a moved position, gravity scale and damping already. A
`velocity` assigned any Vector2 expression (`Vector2.zero`, a local) is
set too.

**Rotation.** A Rigidbody2D turns, as in Unity, unless it is static or its
`m_Constraints` freeze rotation (`RigidbodyConstraints2D.FreezeRotation`);
it was locked. Its owner's class keeps a live rotation (so its sprites draw
turned), the plan's `physics2d_rotation` has the glue start each body at
its owner's authored angle and angular velocity and pull both back after
every step (`engine_rb2d_get_rot` / `set_rot`, radians), and a teleport
keeps the rotation. Scripts have, in Unity's degrees:

| C# | |
|----|--|
| `rotation`, `rotation = a`, `MoveRotation(a)` | the owner's angle (a write is pushed as a turn in place) |
| `angularVelocity` | get, set, `op=` |
| `AddTorque(t)`, `AddTorque(t, ForceMode2D.Impulse)` | applied by Box2D (`b2Body_ApplyTorque` / `ApplyAngularImpulse`), which knows the inertia |
| `freezeRotation` | get, set (the motion lock follows) |
| `transform.eulerAngles.z` | of a turning body's own Transform, [0, 360) |

With no turning body in the scene the API reads 0 and writes nothing.
Godot mode keeps its bodies' rotation locked.

**Joints.** `HingeJoint2D`, `DistanceJoint2D`, `SpringJoint2D`,
`FixedJoint2D`, `SliderJoint2D`, `WheelJoint2D`, `FrictionJoint2D`,
`RelativeJoint2D` and `TargetJoint2D` are read from the scene
(`plan["joints2d"]`, the `_Joint2D_*` tables in data.c) and built by
Box2D-Packed as revolute, distance (rigid; a rope with `maxDistanceOnly`),
distance with a spring, weld, prismatic and wheel joints, and the last
three as its motor joint: friction is velocity control to rest capped at
`maxForce` / `maxTorque`; relative is a spring to the linear and angular
offset (`autoConfigureOffset` as Unity has it), capped the same, whose
frequency is `correctionScale`'s -- Box2D v2's motor joint corrected that
fraction of the error a step, a spring of sqrt(scale) / (2 pi dt) hertz;
target is a spring (`frequency`, `dampingRatio`, `maxForce`) pulling the
anchor to a world point (`autoConfigureTarget`: where the anchor starts),
the body free to turn. They are built with the bodies -- a script's `Start`
sees them. The joint links its own body to the
connected one (a wheel joint: the chassis it is on to the wheel), or to a
static ground body at the origin when there is none; Unity's anchor and
connected anchor are the frames' points (`autoConfigureConnectedAnchor`,
`autoConfigureDistance` and a slider's `autoConfigureAngle` as Unity
configures them), and a hinge's limits and angle are relative to its pose
at creation. A GameObject with a joint and no Rigidbody2D gets the one
Unity adds (dynamic, mass 1, gravity 1).

`gameObject.AddComponent<XJoint2D>()` (or on a GameObject variable) adds
one of the joint kinds at run time: the joint tables keep a spare row per
instance of each class that calls it, the row gets Unity's defaults for
that kind, and it goes on the GameObject's Rigidbody2D -- added too, as
Unity adds one, when there is none (with `AddComponent<Rigidbody2D>`'s own
limits: a class planned without a body may not move). Box2D-Packed builds
it before the next step, so the script sets it up first -- `connectedBody`
(or `null`), `anchor`, `connectedAnchor`, `autoConfigureConnectedAnchor`,
`autoConfigureDistance`, `autoConfigureAngle` are settable, and changing
the bodies or anchors of a built joint builds it again.

Scripts reach a joint through a field of a joint type (set by
`GetComponent<XJoint2D>()`, or a serialized reference), a local, or
`GetComponent<XJoint2D>()` itself; `== null` is no joint, or a broken one.
Its members, pushed before each step when they change and read back after
it:

| C# | |
|----|--|
| `enabled`, `useMotor`, `useLimits`, `enableCollision`, `maxDistanceOnly`, `distance`, `frequency`, `dampingRatio`, `breakForce`, `breakTorque` | get, set, `op=` |
| `motor` (`JointMotor2D`), `limits` (`JointAngleLimits2D` / `JointTranslationLimits2D`), `suspension` (`JointSuspension2D`) | get, set; `new JointMotor2D { motorSpeed = .., maxMotorTorque = .. }`; `motor.motorSpeed`, `limits.min` read directly |
| `maxForce`, `maxTorque`, `correctionScale`, `angularOffset`, `autoConfigureOffset`, `autoConfigureTarget`, `breakAction` | get, set |
| `target`, `linearOffset` (Vector2) | get, set (a moved target moves the spring's end) |
| `jointAngle`, `jointSpeed` (degrees), `jointTranslation`, `connectedBody`, `attachedRigidbody`, `reactionForce`, `reactionTorque`, `GetReactionForce(dt)`, `GetReactionTorque(dt)` | get (the reaction is Box2D's constraint force / torque after the last step) |

Hinge and wheel motor speeds and hinge limits are in degrees, as Unity's
(Box2D clamps a hinge's limits to ±178°). **Breaking**: `breakForce` and
`breakTorque` are Box2D's force and torque thresholds; a joint past one
gets its `breakAction` (`m_BreakAction`, `JointBreakAction2D`): `Destroy`
(the default) removes it -- `GetComponent` no longer finds it -- `Disable`
removes it from the world and sets `enabled` false (enabling it again
builds it again), `CallbackOnly` keeps it, and each of them sends
`OnJointBreak2D(Joint2D)` to its GameObject's scripts; `Ignore` never
breaks.

A Rigidbody2D's authored `m_GravityScale: 0` is kept; it was read as the
default, 1.

**Frame order.** A frame runs as Unity's player loop does: the fixed
steps (FixedUpdate, physics), every `Update`, the animation update (its
curves and Animation Events), then every `LateUpdate`. The animation update
ran before `Update`, and `LateUpdate` -- emitted -- was never called (a
camera following in LateUpdate did not move).

**Lifecycle.** Each script instance keeps Unity's lifecycle: awoken,
started, enabled. A frame's first passes, over every class before the next,
send each object active in the hierarchy `Awake` (once) then `OnEnable`,
then `Start` -- every Awake before any Start, every Start before any
Update. `SetActive` that changes an object's activeInHierarchy sends it and
its active descendants `OnEnable` (with `Awake` first, if it never woke) or
`OnDisable`; an object inactive at load wakes when it is first activated
(it woke at load), and an `Awake` that deactivates its own object leaves it
disabled. `Destroy` sends `OnDisable` then `OnDestroy`; `Instantiate`,
`Awake` and `OnEnable` at once. An object that is not enabled runs no
`Update` / `FixedUpdate` / `LateUpdate` (an inactive one updated). Before,
`OnEnable` was never emitted and `OnDisable` / `OnDestroy` never sent. A
GameObject field or local's `SetActive(..)` is lowered too (it was left).

**Animation Events.** A clip's `m_Events` call, as its time crosses them,
the method of that name on the animated GameObject's scripts (Unity's
SendMessage): with no parameter, or the event's float / int / string when
the method takes one (an `AnimationEvent` / `Object` parameter is not
passed: that event is skipped). A looping clip's events fire again each
loop; one at 0 fires on the first frame; played backwards (a negative
speed) they fire as its time falls past them. The handler is kept even
when nothing else calls it.

**uGUI.** CanvasScaler's *Constant Physical Size* is Unity's: the screen
DPI over the unit's (centimetres 2.54, millimetres 25.4, inches 1, points
72, picas 6); a packed player's DPI is not known when it is packed, so it is
the scaler's `m_FallbackScreenDPI` (96), as Unity uses for a screen that
reports none -- it was a scale of 1. **ToggleGroup**: a toggle's `m_Group`
makes it a radio button -- turning one on turns the group's others off,
their `onValueChanged(false)` first, and without `m_AllowSwitchOff` the one
that is on cannot be clicked off. A script's `isOn = v` is Unity's
`Toggle.Set` -- the checkmark, the group and `onValueChanged`, as a click
(it set the value alone: the checkmark stayed); `SetIsOnWithoutNotify(v)`
the same without the callbacks. At start the group is made valid, as
`ToggleGroup.EnsureValidState` does: at most one toggle on, and without
allowSwitchOff exactly one (the first, when none is). **ScrollRect**: the normalized position is Unity's
`SetNormalizedPosition` in the viewport's local units -- the content's min
edge at `-value * hidden`, whatever its pivot and anchors; it assumed a
top-left content and mixed the canvas-scaled screen sizes into
`anchoredPosition`. The mouse wheel over it scrolls it, as `OnScroll` does
(`scrollSensitivity`; Clamped stays in bounds). Dragging is the same: the pointer's delta in the
viewport's units, and the content's bounds whatever its pivot. The
`movementType` is read -- Unrestricted, Elastic (past a bound the drag
stretches by Unity's RubberDelta, and on release SmoothDamps back over
`elasticity`) and Clamped -- and `inertia` keeps a released content moving,
its velocity falling by `decelerationRate` a second. **EventTrigger**: the input module's order -- Down and
InitializePotentialDrag on press; BeginDrag only once the pointer has moved
10 px (it came on press), then Drag each frame it moves; on release Up,
Click, Drop (on the one under the pointer) and EndDrag, the last two only
after a drag began (EndDrag came before Click, on every release).

**Input.** The host reports the mouse wheel (`engine_scroll_x / _y`,
notches since the last frame, y > 0 away; the example GLFW hosts' scroll
callback): `Input.mouseScrollDelta`, `Input.GetAxis("Mouse ScrollWheel")`
(0.1 a notch) and `Mouse.current.scroll` (120 a notch, as Windows reports
it -- Unity's varies by platform). And the first gamepad
(`engine_gamepad_connected / _button[15] / _axis[6]`, GLFW's layout):
`Gamepad.current` -- null when none -- its buttons (Unity's names and
aliases: buttonSouth / aButton / crossButton ..; the triggers press past
0.5) `isPressed` / `wasPressedThisFrame` / `wasReleasedThisFrame`, its sticks,
triggers and dpad `ReadValue()`; `var gp = Gamepad.current; if (gp == null)
..` works (it was refused).

**InputAction** (tools/unity_pack_input.py): an action's bindings are
resolved when the project is packed -- the code's (`new InputAction(binding:
..)`, `AddBinding`, `AddCompositeBinding("2DVector" / "1DAxis").With(..)`),
or, for none, the Inspector's (the scene's `m_SingletonActionBindings`) --
into the engine's tables, evaluated each frame over the keyboard, gamepad
and wheel: `Enable` / `Disable`, `ReadValue<float / Vector2>()` (the most
actuated binding; a 2D composite's normalized digital vector),
`IsPressed()`, `WasPressedThisFrame()`, `WasReleasedThisFrame()`,
`triggered` (0.5, the default press point). `started` / `performed` /
`canceled += handler` (a method taking the CallbackContext, or a lambda;
`-=` too) fire before Update -- a button's as it is pressed / released, a
value's as it becomes actuated / changes / rests -- with the context's
`ReadValue<T>()`, `ReadValueAsButton()` and phase. An action with no binding
the pack can read is reported. Interactions, processors, action assets and
PlayerInput are not read. (A multi-name field declaration, `int a, b;`,
still declares the first name alone.)

**ParticleSystem** (tools/unity_pack_particles.py): the component's main,
emission and shape modules are read -- lifetime, speed, size and color (a
constant, or random between two; a curve's scalar), gravity modifier,
duration, looping, play on awake, simulation space and speed, rate over
time and bursts, a cone along the emitter's +Z (as its rotation turns it) or
a sphere / circle -- and simulated after LateUpdate, the particles drawn as
squares of their color (the draw list's `tex -2`, a white texel in the
example hosts). Scripts: `Play`, `Stop` (stops emitting; the particles live
on), `Pause`, `Clear`, `Emit(n)`, `isPlaying`, `isEmitting`, `isPaused`,
`isStopped`, `particleCount`, on a field (serialized or GetComponent's), a
local or `GetComponent<ParticleSystem>()`. The other modules (over-lifetime
curves, noise, collision, sub-emitters, trails) and the renderer's material
are not read; `AddComponent<ParticleSystem>` stays refused.

**Authored zeros.** A value authored as 0 is kept where 0 is not the
default: a Rigidbody2D's `m_GravityScale`, an Animation / Animator's speed
(a paused one), a Slider's `m_MaxValue` (a -1..0 slider) and a
Scrollbar's `m_Size` were read as missing and given the default.

**Animated rotation and scale.** An AnimationClip's `m_EulerCurves` and
`m_ScaleCurves` drive rotation (Euler degrees, sampled in Euler space as
Unity's Euler curves are, turned into the quaternion in Unity's Z-X-Y
order) and localScale x / y; they were parsed and dropped, only the root's
`m_PositionCurves` animating. Every curve but the root's position is a
*track*, and each player binds its clip's tracks to their targets when it
is packed: its own Transform, or the child the curve's `path` names -- so a
child's position, rotation and scale animate too (a child whose class is
packed static keeps its position, with a warning).

**Curve tangents.** Keys are evaluated as Unity evaluates them: a cubic
Hermite from each key's `outSlope` and the next key's `inSlope`, scaled by
the segment's length (eased motion, overshoot); an infinite slope -- a
"constant" key -- holds the value until the next key. A key without slopes
gets the straight line's, so it is sampled linearly, as before. The owner keeps live
rotation / scale tables, and `transform.eulerAngles.z` and
`transform.localScale.x / y` read them. A clip that is not looping and is
played backwards (a negative speed) now stops at its start.

**Queries.** `Physics2D.Raycast(origin, direction[, distance[,
layerMask]])`, `RaycastAll`, `OverlapCircle(point, radius[, layerMask])`,
`OverlapCircleAll`, `OverlapPoint(point[, layerMask])` and
`OverlapPointAll` are Box2D-Packed's (`engine_box2d_raycast[_all]` /
`_overlap_circle[_all]` / `_overlap_point`, in every glue), and may run
before the first step (a script's
`Start`: the glue builds the world first). `RaycastAll` is nearest first,
as Unity's; the `*All` arrays (`RaycastHit2D[]`, `Collider2D[]`) are lists,
so `foreach`, `hits[i]` and `.Length` work. A
`RaycastHit2D` is the engine's struct -- `collider` (an index, -1 for
none), `point`, `normal`, `distance`, `fraction` -- `if (hit)` is a hit,
and `hit.collider`, `hit.transform` and a `Collider2D` an overlap returns
read their GameObject as a handler's parameter does (`.gameObject`,
`.name`, `.tag`, `CompareTag`, `GetComponent<T>()`); `transform.position`
as the origin is taken by its x and y. Triggers are hit, as Unity's
`queriesHitTriggers` default has it, and a ray ignores a collider it
starts inside (Unity's `queriesStartInColliders` default would hit it). The
`*NonAlloc` forms are not lowered.

**Layers.** Each collider has its GameObject's `m_Layer`
(`_Collider2D_layer`), and a query's layer mask is tested against it in the
glue's callbacks -- Box2D-Packed's filters are 16 bits, Unity has 32
layers, and contacts are left alone (the layer collision matrix is not
read). With no mask a query takes `Physics2D.DefaultRaycastLayers`: every
layer but "Ignore Raycast". At the source level, a `LayerMask` (field,
local, parameter) is an `int` -- a field's scene value is its `m_Bits` --
`mask.value` is the mask, and `LayerMask.GetMask("A", ..)` /
`NameToLayer("A")` are constants from the project's layer names
(`ProjectSettings/TagManager.asset`, Unity's built-in names without one;
an unknown name is no bit / -1). A `RaycastHit2D` is a bool wherever C#
converts it: `if (hit)`, `hit ? a : b`, `hit && ..`.

# UNITY_PACK — packed engine from a Unity (or Godot) subset

### `[MaxInstances(N)]`: the author sets the cap

```csharp
public class MaxInstancesAttribute : System.Attribute {
    public MaxInstancesAttribute(int n) {}
}

[MaxInstances(255)]   public class Player : MonoBehaviour { … }   // uint8_t
[MaxInstances(20000)] public class BulletTypeA : MonoBehaviour { … } // uint16_t
```

The attribute class is the project's own (Unity needs it to compile the
script; the packer reads the name and ignores the class). With it:

* the index into the class is as narrow as N allows — `uint8_t` up to
  255, `uint16_t` up to 65535 — whatever else in the project spawns, and
  every field that holds one is that width (a handle is as wide as its
  *target*, not its owner);
* the instance array and the GameObject pool hold exactly N, and
  `Instantiate` returns null once N are live: the N+1st bullet is not
  fired. Clipping is the behaviour asked for, not an error;
* N counts **live** instances: a destroyed one's slot is reused (it was
  not — `Destroy` never freed anything, so a pool emptied after N spawns
  in all);
* a scene that already places more than N is an error at the attribute;
* the spare slots are zeros C fills in, so `data.c` does not list 20000
  empty rows;
* the tables beside the instance array -- an instance `List`, `T[]`,
  `Dictionary` or `string` field -- are as long as it (they were the
  scene's count, and a clone wrote past the end), and `Instantiate` gives
  the clone's row what Unity does: a serialized field (public, or
  `[SerializeField]`) copied from the original, any other what its
  initializer makes it, and a `Dictionary`, which Unity never serializes,
  empty.

**Awake and Start, per instance.** Every instance, authored or spawned,
gets `Awake` and `Start` once, in Unity's order: a life byte per row
records each. `Instantiate` calls the clone's `Awake` before it returns;
`Start` runs at the next tick, before that object's first `Update` -- an
`Update` loop skips a row until it has started. A destroyed object gets
neither. They used to run once per class at the first tick, for the
instances there then, so a spawned object never started.

**What a clone keeps.** `Instantiate` copies the instance struct, and then
puts back every member Unity does not serialize -- a private field without
`[SerializeField]` -- to what its initializer makes it; a serialized one
keeps the original's value. The struct copy used to hand a clone the
original's private state, a counter or a flag, so it carried on as if it
had already run. `TestSpawnLifecycle` and the fast check's `life` and
`list_cap` cases run both.

`TestMaxInstances` runs a bullet that clones itself every frame (held at
N) and one that fires and is destroyed (firing for all 60 frames). A
script's component is the class named after its file, as in Unity — the
first class in the file used to be taken, so an attribute class declared
above the component became the scene object's class.

### `--gpu-handles`: handles in a GLES 3.1 SSBO

The stored references — a `Bullet`'s `owner`, a `Player`'s `last` — go
to the GPU at their packed width: four byte handles, two 16-bit handles
or one 32-bit handle per `uint`, read in the shader with
`bitfieldExtract`. A handle is as wide as its *target* class's index, so
with `[MaxInstances(10)] Bullet` and `[MaxInstances(1000)] Player` a
Bullet's `owner` is 16 bits and a Player's `last` is 8.

`--gpu-handles` (`pack(gpu_handles=True)`) adds, and changes nothing
else:

* `engine_upload_handles(uint32_t *dst, int max_words)` in the engine —
  every handle field as one stream of its class's capacity, packed from
  bit 0 and word-aligned; a slot past the live count holds the field's
  null;
* `engine_handles.h` — `ENGINE_HANDLE_WORDS`, and per stream
  `<Class>_<field>_OFF` / `_LEN` / `_BITS` / `_NULL`;
* `shaders/handles.glsl` — for inclusion after `#version 310 es`: the
  SSBO at binding 1 and an accessor per stream, returning an index into
  the target class or its `_NULL`:

```glsl
const uint Bullet_owner_NULL = 65535u;
uint Bullet_owner(uint i) { return bitfieldExtract(handles[0u + i / 2u], int((i % 2u) * 16u), 16); }
const uint Player_last_NULL = 255u;
uint Player_last(uint i) { return bitfieldExtract(handles[5u + i / 4u], int((i % 4u) * 8u), 8); }
```

`TestGpuHandles` packs that scene, decodes the words the C side writes
with `bitfieldExtract`'s definition, and — where a headless GL is
available (`moderngl` over Mesa's EGL/llvmpipe) — runs a compute shader
built from `handles.glsl` and compares every slot the GPU reads with the
scene. The default viewer is OpenGL ES 3.1 (see "Display") and binds
these handles at SSBO binding 1 every frame; the GLES2 viewer, kept for
hardware without ES 3.1, has no SSBOs.


## Unity Runtime: 
### `Mathf`, `Random`, `Parse`, `Path`, `Directory`, `File` reads, `StringBuilder`, `JsonUtility`

Common .NET and Unity static APIs are one table,
`tools/unity_pack_runtime.py`: each C# spelling maps to an engine helper,
its C (in the subset cpprust lowers) and its result type, so the typed
concatenation and `Debug.Log` format a result as they format anything else.
Only the helpers a pack uses are emitted, with the ones they call.

| C# | |
|----|--|
| `Mathf.Sqrt`, `Pow`, `Floor`, `Ceil`, `Round`, `FloorToInt`, `CeilToInt`, `RoundToInt`, `Tan`, `Asin`, `Acos`, `Atan`, `Atan2`, `Exp`, `Log` (1 or 2 args), `Log10`, `Clamp01`, `InverseLerp`, `LerpUnclamped`, `MoveTowards`, `Repeat`, `PingPong`, `DeltaAngle`, `SmoothStep`, `Approximately` | as Unity; `Round` halves to even, as .NET does |
| `Mathf.PI`, `Deg2Rad`, `Rad2Deg`, `Epsilon`, `Infinity`, `NegativeInfinity` | constants |
| `Random.Range(a, b)`, `Random.value`, `Random.InitState(seed)` | int `Range` excludes `b`, float includes it; both ints picks the int one |
| `int.Parse`, `float.Parse`, `double.Parse`, `int.TryParse(s, out n)`, `float.TryParse(s, out int f)` | an `out` declaration is hoisted before its statement |
| `Path.Combine` (2+ args), `GetFileName`, `GetExtension`, `GetFileNameWithoutExtension`, `GetDirectoryName` | `/` separators |
| `Directory.Exists`, `Directory.CreateDirectory` | |
| `File.ReadAllText`, `File.ReadAllLines` | a UTF-8 BOM is dropped; lines split on `\n`, a `\r` before it dropped |
| `Time.realtimeSinceStartup`, `Time.unscaledTime` | `Time.time` (below) |
| `s.GetHashCode()` on a string | deterministic FNV-1a |

Where the packed engine differs from .NET: `float.Parse` reads the
invariant culture (`.` decimal point); `Random` is a seeded xorshift32
(Unity seeds from the clock), so a run repeats unless the script calls
`Random.InitState`; `realtimeSinceStartup` is the engine's time, since it
has no time scale and reads no clock; a string's hash is not .NET's
(which is randomized per process anyway), so only equality of hashes
means anything. Input `Parse` rejects, a file `ReadAllText` cannot open,
or a directory `CreateDirectory` cannot make aborts with the .NET
exception's name, as an unhandled exception ends the process. The
helpers keep to what crust's own C front end has -- no `strtod` (the
decimal reader is the runtime's), no `EOF`, and nothing POSIX outside
`#ifndef CRUST_NO_POSIX_MKDIR` (without it, `Directory` falls back to
`fopen`).

**Bools print as C# prints them.** `"alive " + alive` is `alive True`
and `Debug.Log(ok)` prints `False`; the engine printed 1 and 0. A bool
next to a binary `+` can only be in a concatenation, so a bool variable,
field, or helper result there becomes `(b ? "True" : "False")`. A
conditional takes its branches' type in a concatenation, so
`"x" + (ok ? "in" : "out")` is a string, not a float.

**`StringBuilder`.** A `StringBuilder` local is a coost `fastring` it
appends to in place: `Append(x)` formats `x` as a concatenation would (an
int, float, bool or char as C# prints it) and copies only that. `AppendLine`,
`AppendFormat` (through the format lowering), `Clear`, `Replace`,
`ToString`, `Length` and chained statements (`sb.Append(a).Append(b);`, one
statement per call, in braces) are lowered; a builder passed to a method or
kept in a field is not, and the method is reported.

**`JsonUtility`.** `JsonUtility.ToJson(obj)`, `ToJson(obj, pretty)` and
`FromJsonOverwrite(json, obj)` on a packed object -- `this`, or a handle
field, local or parameter of another class -- are functions generated per
class at pack time (`_Player_ToJson(i, pretty)`), over the fields Unity
serializes (public, or `[SerializeField]`), in declaration order: `int`,
`float`, `bool`, `string`, `Vector2`, `Vector3`. The text is Unity's: a
float always has a point (`2.0`) and is the shortest that reads back as
the same float, strings are escaped, pretty output indents four spaces.
`FromJsonOverwrite` sets the fields the JSON has and leaves the rest, skips
keys it does not know (nested values included), reads `\uXXXX` as UTF-8,
and a document that is not an object is .NET's `ArgumentException`. When a
project calls it, integer fields keep their full C# width: the packer
narrows a field to what its authored values need, and a value read from
JSON is not one it can see. A class with a serialized field of another
type (a reference, a collection) is not lowered, and the method is
reported rather than writing JSON without it.

**`new string[n]`**, `new string[] { .. }`, `new[] { .. }` and `{ .. }`
make a `string[]` of that size (C#'s nulls read as ""), then write each
initializer element.

**Clock.** `Stopwatch` (`StartNew`, `new Stopwatch()`, `Start`, `Stop`,
`Reset`, `Restart`, `ElapsedMilliseconds`, `Elapsed.TotalSeconds` /
`TotalMilliseconds`, `IsRunning`) and `DateTime.Now` / `UtcNow` (`Year`,
`Month`, `Day`, `Hour`, `Minute`, `Second`, `Millisecond`, `DayOfYear`,
`ToString()` and `ToString(format)` with .NET's custom tokens `yyyy yy MM M
dd d HH H hh h mm m ss s fff ff f tt`, quoted text and `\x`) read
`clock_gettime` / `localtime_r`: the player is built with gcc. They sit
behind `#ifndef CRUST_NO_POSIX_MKDIR` like the file helpers, so the pack's
validation through crust's own C front end (no `<time.h>`) still passes;
there the clock reads 0. A stopwatch or a date is a local here: kept in a
field, passed to a method, or subtracted (a `TimeSpan`), it is not lowered,
and the method is reported. `DateTime.ToString()` is the invariant
culture's general form, `MM/dd/yyyy HH:mm:ss`, not the machine's culture.

## Static helper classes and extension methods

A `static class` has no instances, so there is nothing to pack it as;
until now any call into one (`Util.Twice(hp)`) left the calling method a
stub. They are rewritten at the source level first
(`tools/unity_pack_extensions.py`), and the analysis and the lowering read
the rewritten text (`SOURCE_OVERLAY` in `unity_pack_common`):

* a C# 14 extension block -- `extension (GameObject go) { .. }` -- becomes
  classic static members, the receiver their first parameter; an extension
  property `P` becomes a method `get_P(this T x)`;
* an extension call `x.M(a)`, `x.M<T>(a)` or `x.P` becomes the static call
  `Cls.M(x, a)`, `Cls.M<T>(x, a)`, `Cls.get_P(x)` (a method of the
  project's own classes with the same name shadows it);
* a static method whose body is one `return expr;` (or `=> expr`) is
  inlined where it is called, as before; any other one, and every generic
  one, is copied into the calling class as a private static method --
  `Util__Twice`, or `UnityExtensions__GetOrAddComponent__Badge` for each
  type argument, `T` substituted -- with its calls to its siblings, its
  class's consts and its own extension calls rewritten the same way, and
  what it calls copied too.

```csharp
public static class UnityExtensions {
    extension (GameObject go) {
        public bool IsActiveInHierarchy => go.activeInHierarchy;
        public T GetOrAddComponent<T>() where T : Component {
            T component = go.GetComponent<T>();
            if (component == null) component = go.AddComponent<T>();
            return component;
        }
    }
}
// in a MonoBehaviour:
Badge b = gameObject.GetOrAddComponent<Badge>();  // packs, adds once
```

Extension methods on Unity value types work the same way: `Vector2` is a
parameter and return type the engine has (its C struct), so

```csharp
public static Vector2 SetZ(this Vector2 v, float z) { return new Vector2(v.x, z); }
public static void Example2(this float f) { }
// a.SetZ(5f), aim.SetZ(-1f) on a packed Vector2 field, speed.Example2()
```

pack and run. The packer's own `SetX` / `SetZ` inside a
`t.SetWorldScale(..)` argument stay its own; everywhere else a project's
`SetX` / `SetY` / `SetZ` are its extension methods. `GetWorldRect`,
`SetWorldScale` and a static array's `Add` / `Remove` are always the
packer's.

Each copy goes on its own line after the class's last one, so the class's
lines -- and the diagnostics pointing at them -- stay where the author
wrote them; only another top-level type later in the same file moves. A
method that reads or writes a non-const static field of its class is not
copied (each class would get its own copy of shared state), and its calls
are left for the stub check; so is a generic call whose type arguments are
inferred rather than written (`x.M()` for `M<T>(this T x)`). An API a
helper uses counts for the classes that call it (`SOURCE_API_HINTS`).

## Methods that return values, and other objects' fields

A MonoBehaviour's methods emitted as `static void`, and one returning a
value was a stub. A method may now return `int` (and the other integer
types), `bool`, `float`, `double`, `string`, `Vector2`, a packed component
or a `GameObject` (their index), and take a `Vector2` parameter (it was
passed as an `int`, a handle); the type has a C value, and anything else --
a coroutine's `IEnumerator`, a `Vector2`, a collection -- keeps the stub.
A returned string is copied to a scratch slot (`_cs_str_ret`), so an owned
local's text is not freed under the caller. Calls are typed where they are
used: `"x" + Score()` formats an integer, `"ok " + Alive()` a bool. Static
methods are forward-declared like instance ones.

A field of a packed object reached through a local or parameter of its
class (`Badge b = ..; b.n = 9;`) reads and writes its slot
(`Badge_AT(b).n`), as a handle field's does. `go.AddComponent<T>()` on a
`GameObject` variable adds to that GameObject (it added to this one).
`gameObject.activeSelf` / `activeInHierarchy` -- this object's or a
variable's -- read the engine's active tables. A comparison or logical
expression in a concatenation prints as a bool (`"ok " + (n >= 0)` is `ok
True`), and `s[k]` on a string is a `char`.

## Collections: Stack, Queue, HashSet, List members, `T[,]`

`Stack<T>`, `Queue<T>` and `HashSet<T>` were refused; they are rewritten at
the source level (`tools/unity_pack_collections.py`, in the same overlay as
the extension methods) into the `List<T>` the packer lowers -- so locals,
instance fields, statics and every element type the list lowering has work
for them too:

| C# | as a List |
|----|-----------|
| `s.Push(x)`, `q.Enqueue(x)` | `Add` |
| `s.Peek()`, `q.Peek()` | `s[s.Count - 1]`, `q[0]` |
| `s.Pop()`, `q.Dequeue()` | hoisted before the statement: an emptiness check (.NET's `InvalidOperationException`), the element into a temporary, `RemoveAt` |
| `foreach` over a `Stack` | top first, as .NET |
| `h.Add(x)` | `if (!h.Contains(x)) h.Add(x)`; as a value, hoisted with its bool |
| `h.UnionWith(o)`, `IntersectWith`, `ExceptWith` | loops |

**`LinkedList<T>`**, a subset without nodes: `AddLast` is `Add`,
`AddFirst` is `Insert(0, ..)`, `RemoveFirst` / `RemoveLast` remove at an
end, `First.Value` / `Last.Value` read one -- each end checked, as .NET's
null `First` would throw -- and `Count`, `Clear`, `Contains`, `Remove(x)`
and `foreach` are the list's. A `LinkedListNode` (`.First` kept as a node,
`.Next`, `AddAfter`, `Find`) is left for the stub check.

A take hoisted from inside a `while` / `for` header would run once, not
each time round, and is left for the stub check; one in an `if` / `switch`
condition is hoisted before it. A `HashSet` keeps insertion order (.NET's
until an element is removed) and its `Contains` is linear; a `Dequeue`
moves the rest down.

The packed `List` had only `Add`, `Clear`, `Count`, indexing and a field's
`foreach`. It now has `RemoveAt` and `Insert` (index-checked, .NET's
`ArgumentOutOfRangeException`), `Contains`, `IndexOf` and `Remove` (a
search helper per element type), and `foreach` over a local (an index
loop). A `List<string>` is a vector of owned coost `fastring`s, as a
`string[]` is -- it could not take a literal before; a `Dictionary`'s string
keys and values are unchanged.

**Multidimensional arrays.** `T[,]` and `T[,,]` of `int`, `float`, `bool` or
`string` are a `List<T>` (row-major, as .NET lays them out) and an `int`
per dimension: `new T[a, b]` stores the dimensions and fills in
`default(T)`, `g[x, y]` is the flat index through a helper that checks each
one (`IndexOutOfRangeException`), and `GetLength(k)`, `Length`, `Rank` and
`foreach` work. A field's initializer is filled at the start of `Awake`
(one is made if the class has none); its dimension fields go after the
class's last line. An array literal (`{ {1, 2}, .. }`) or a `T[,]`
parameter is left for the stub check.

## Coroutines

An `IEnumerator` method of a MonoBehaviour that `yield`s is rewritten at
the source level (`tools/unity_pack_coroutines.py`) into a state machine
of private fields and methods of its class -- things the packer lowers
like any other:

* its parameters and locals become fields (`_co_Blink_k`), so they survive
  a `yield`; the body becomes `bool _co_Blink_step()`, which a `switch`
  enters at the resume point of its last `yield` (a `goto` into the loop
  body holding it);
* `yield return null` -- and `0`, `WaitForEndOfFrame`, `WaitForFixedUpdate`
  -- waits for the next frame; `yield return new WaitForSeconds(t)` until
  `Time.time` has moved on by `t`; `yield break` ends it. The frame is an
  `int` count per object and the deadline integer milliseconds: a class
  that does not move packs its floats as halves, and a stored time read
  back below `Time.time` resumed a null yield in the frame it yielded in;
* `StartCoroutine(Blink(3))`, `StartCoroutine("Blink")` and
  `StartCoroutine(nameof(Blink))` set the parameters and run the body to
  its first `yield` at once, as Unity does; `StopCoroutine(..)` and
  `StopAllCoroutines()` clear the state;
* each frame, after the object's `Update` -- the author's is renamed and
  called from one made to call both, so its early `return` does not skip
  them -- `_co_tick()` resumes each coroutine whose wait is over.

* `yield return StartCoroutine(Child(..))` -- or `yield return
  Child(..)` -- of another coroutine of the same class starts it (to its
  first `yield`) and waits, a field saying which, until it has ended. The
  tick runs the coroutines in declaration order, once per coroutine, so a
  parent resumes in the frame its child ends, however they are ordered.

Each object has its own fields, so each runs its own coroutines; starting
one that is already running restarts it (Unity would run a second). A
coroutine keeps running after its object is disabled. Left for the stub
check: another object's coroutine, `WaitUntil` / `WaitWhile` (a lambda), a
`yield` inside a `foreach`, a `var` whose type the rewrite cannot see, and
a `Coroutine` kept in a variable.

## `byte[]`, `Encoding`, Base64, MD5 / SHA-256

In a file that builds, converts or hashes bytes -- `Encoding`, `Convert`'s
Base64, `MD5` / `SHA256`, `BitConverter`, or a sized `new byte[n]` -- a
`byte[]` is a `List<byte>` (`tools/unity_pack_collections.py`), and the
byte APIs are runtime helpers over it:

| C# | |
|----|--|
| `new byte[n]`, `new byte[] { .. }`, `b.Length`, `b[i]`, `foreach (byte x in b)` | the list's (a `byte` local is an `int`, as the list holds it) |
| `Encoding.UTF8` / `ASCII.GetBytes(s)`, `.GetString(b)` | UTF-8 bytes and back |
| `Convert.ToBase64String(b)`, `Convert.FromBase64String(s)` | coost's `base64_encode` / `_decode`; bad input is .NET's `FormatException` |
| `MD5.Create()` (a local, or in a `using`) then `.ComputeHash(b)`; `MD5.Create().ComputeHash(b)`; `MD5.HashData(b)`; `SHA256` alike | coost's `md5digest_to` / `sha256digest_to` |
| `BitConverter.ToString(b)` | `"AB-CD-.."` |
| `b.ToString("x2")`, `"X2"`, `{0:x2}` | hex |
| `File.ReadAllBytes`, `File.WriteAllBytes` | over the list, in such a file |

coost's hash and Base64 sources are spliced into the engine only when it
calls them. A byte helper's argument that is itself a byte helper's result
is hoisted into a temporary first (a reference parameter needs an
address). A file whose bytes only go to and from `File.ReadAllBytes` /
`WriteAllBytes` keeps the packer's `ByteArray` view, as before.

## `GetType`, `typeof`, `is`, `nameof`

A packed object's class is known when packing -- `this`, or a handle
field, local or parameter of a packed class -- so these are constants:
`GetType().Name` (and `.FullName`, `.ToString()`) and `x.GetType().Name`
are the class's name, `typeof(T).Name` is `"T"`, `GetType() == typeof(T)`
is decided, `x is T` for an `x` declared a `T` is `x != null`, and
`nameof(x)` is `"x"`. A type used any other way -- reflection, a `Type`
kept in a variable -- is left as written, and the method is reported.




## Animation, input, lighting, camera, physics

Opt-in lowering of Input Manager axes, `Time.time` / `Mathf.Sin`,
`RenderSettings.ambientLight`, authored Lights / Cameras /
SpriteRenderers, and `Physics2D.gravity` + `FixedUpdate` on **authored**
scene objects — see [UNITY_PACK_SYSTEMS.md](UNITY_PACK_SYSTEMS.md). The
packer does not invent ParticleSystem pools, Canvas/UI, or InputAction maps.
Authored AnimationClips / AnimatorControllers and Rigidbodies are packed.
Fixture: `examples/unity_pack/SystemsScene`.


https://github.com/brentharts/crust
