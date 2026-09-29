#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
# SPDX-License-Identifier: MIT
"""
box2d_unity: Box2D-Packed as the 2D physics backend for crust's tools/unity_pack.py.

unity_pack packs a Unity-shaped project into engine.c / data.c. Its data tables describe every
Rigidbody2D (_Rigidbody2D_*) and Collider2D (_Collider2D_*), and its engine sends
OnCollisionEnter2D / Stay2D / Exit2D by comparing each fixed step's touching pairs with the
previous step's. This module is unity_pack's 2D physics: it generates a Box2D world for those
tables and keeps everything else:

    engine_physics_fixed()                 (engine.c, unity_pack)
        engine_box2d_step()                (physics_box2d.c, generated here)
            push script changes: velocity, teleports, gravity scale, damping, world gravity
            b2World_Step
            pull positions and velocities into the packed tables
            report touching pairs with engine_col2d_contact()
        engine_physics_collide2d_messages() (unity_pack: Enter / Stay / Exit, after the step)

Touching pairs come from Box2D contact begin and end events. With `--physics-inject`, they come
from box2d_pack injection markers instead: the injected code only records the pair, so scripts
still run after the step, as in Unity.

engine.c exports what the glue needs, in unity_pack's C subset:

    void engine_rb2d_get_pos( int rb, float* x, float* y );   Rigidbody2D owner position
    void engine_rb2d_set_pos( int rb, float x, float y );
    void engine_col2d_center( int ci, float* x, float* y );  collider world center
    void engine_col2d_contact( int a, int b );               report a touching pair

With plan["physics2d_live"] (unity_pack sets it when some GameObjects can leave the simulation,
e.g. scenes that are not loaded) it also exports

    int engine_rb2d_live( int rb );                          owner in the simulation
    int engine_col2d_live( int ci );                         static collider in the simulation

and bodies whose owner is not live are disabled (b2Body_Disable) until it is again.

With plan["physics2d_queries"] it exports Physics2D.Raycast / OverlapCircle / OverlapPoint for the
engine (a collider index, -1 for none); with plan["physics2d_rotation"] bodies turn (see
_with_rotation) instead of having their rotation locked.

With plan["physics2d_contacts"] (scripts read Collision2D contacts) each touching pair's manifold
is reported just before the pair, from b2Shape_GetContactData:

    void engine_col2d_manifold( int a, int b, float nx, float ny, int n,
                                float p0x, float p0y, float p1x, float p1y );
                                normal from a to b, n (0-2) world contact points

Mapping:
    Rigidbody2D Dynamic / Kinematic / Static  ->  b2_dynamicBody / kinematic / static; rotation
                                                  locked unless plan["physics2d_rotation"],
                                                  and then locked by FreezeRotation only
    BoxCollider2D / CircleCollider2D          ->  offset box / circle, rotated by the collider
    CapsuleCollider2D                         ->  capsule along m_Direction (circle when short)
    Collider2D without a Rigidbody2D          ->  static body at the collider center
    m_IsTrigger                               ->  sensor, no collision messages; with
                                                  plan["physics2d_triggers"] its overlaps are
                                                  reported with engine_col2d_trigger( a, b )
                                                  for OnTrigger*2D (standard API only)
    Rigidbody2D mass                          ->  body mass (and again when a script changes it)
    Rigidbody2D bodyType / isKinematic        ->  body type, and b2Body_SetType when a script
                                                  changes it
    friction / bounciness + combine modes     ->  world friction / restitution callbacks that
                                                  apply Unity's PhysicsMaterialCombine rules

Godot mode (`emit_glue( ..., mode="godot" )`, for crust's tools/godot_pack.py, which packs a
Godot 4 project through the same tables). The tables and the step are the same; what differs is
what Godot means by them:

    units                     ->  Godot's pixels, y down, as the scene and scripts have them: the
                                  world runs in them, with b2SetLengthUnitsPerMeter scaling Box2D's
                                  tolerances (length_units_per_meter, default 64)
    PhysicsMaterial           ->  Godot's rules: friction |min(a, b)|, bounce clamp(a + b, 0, 1),
                                  a rough material's friction and an absorbent material's bounce
                                  counted negative (rough / absorbent in the combine columns)
    linear_damp               ->  Godot damps once a step, v *= max(0, 1 - dt * d); the table holds
                                  that d, and the glue sets the Box2D damping whose substeps
                                  compound to the same factor
    fixed step                ->  1/60 s when Time_fixedDeltaTime is unset (physics ticks 60)
    Area2D (a sensor)         ->  overlaps: every shape takes sensor events, and a sensor's begin
                                  and end touch report the pair with engine_col2d_contact, as a
                                  contact does -- godot_pack sends body_entered / area_entered from
                                  them. Unity mode's triggers stay silent, as before
"""

import json
import os
import sys

__all__ = ["emit_glue", "build_library", "GLUE_FILE", "INJECT_FILE", "MODES"]

GLUE_FILE = "physics_box2d.c"
INJECT_FILE = "box2d_inject.json"

_HERE = os.path.dirname( os.path.abspath( __file__ ) )

#: The engines whose tables the glue can take. unity is the default.
MODES = ( "unity", "godot" )


def _counts( plan ):
    """Rigidbody2D capacity (authored + AddComponent budget) and collider count."""
    budget = plan.get( "addcomponent_budget" ) or {}
    rb = len( plan.get( "rigidbody2d" ) or [] ) + int( budget.get( "Rigidbody2D" ) or 0 )
    col = len( plan.get( "collider2d" ) or [] )
    return rb, col


def emit_glue( outdir, plan, inject=False, sub_steps=4, mode="unity", length_units_per_meter=64.0 ):
    """
    Write physics_box2d.c (and box2d_inject.json when inject) into outdir. Returns the paths.
    mode is "unity" (unity_pack) or "godot" (godot_pack); length_units_per_meter is Godot's
    pixels per Box2D metre, and is ignored in unity mode, whose units are metres.
    """
    if mode not in MODES:
        raise ValueError( "box2d_unity: unknown mode %r (one of %s)" % ( mode, ", ".join( MODES ) ) )
    if mode == "godot" and not float( length_units_per_meter ) > 0.0:
        raise ValueError( "box2d_unity: length_units_per_meter must be positive" )
    rb_count, col_count = _counts( plan )
    n_rb = max( 1, rb_count )
    n_col = max( 1, col_count )
    max_pairs = max( 1, col_count * ( col_count - 1 ) // 2 )

    if col_count > 0:
        collider_decls = COLLIDER_EXTERNS
    else:
        # unity_pack emits no collider tables for a scene without colliders
        collider_decls = COLLIDER_EXTERNS.replace( "extern const", "static const" ).replace(
            "_Collider2D_count;", "_Collider2D_count = 0;" ).replace( "[];", "[1];" )
    glue = GLUE_TEMPLATE.format(
        COLLIDER_DECLS=collider_decls,
        N_RB=n_rb,
        N_COL=n_col,
        MAX_PAIRS=max_pairs,
        SUB_STEPS=int( sub_steps ),
        **_mode_parts( mode, length_units_per_meter ),
    )
    if plan.get( "physics2d_live" ):
        glue = _with_live_gate( glue )
    if plan.get( "physics2d_contacts" ):
        glue = _with_contact_manifolds( glue )
    if plan.get( "physics2d_triggers" ) and mode == "unity":
        glue = _with_unity_triggers( glue )
    if plan.get( "physics2d_queries" ):
        glue = glue + QUERY_FUNCTIONS
    if plan.get( "physics2d_rotation" ) and mode == "unity":
        glue = _with_rotation( glue )
    paths = []
    glue_path = os.path.join( outdir, GLUE_FILE )
    _write_if_different( glue_path, glue )
    paths.append( glue_path )

    inject_path = os.path.join( outdir, INJECT_FILE )
    if inject:
        spec = {
            "comment": "Generated by box2d_unity.py for unity_pack --physics-inject. "
                       "Record touching pairs at the contact markers; scripts run after the step.",
            "defines": {
                "B2_PACK_NO_CONTACT_BEGIN_ARRAY": 1,
                "B2_PACK_NO_CONTACT_END_ARRAY": 1,
            },
            "globals": [
                "void b2u_on_begin( int colliderA, int colliderB );",
                "void b2u_on_end( int colliderA, int colliderB );",
            ],
            "inject": [
                {
                    "event": "contact_begin",
                    "code": "b2u_on_begin( (int)(intptr_t)shapeA->userData - 1, (int)(intptr_t)shapeB->userData - 1 );",
                },
                {
                    "event": "contact_end",
                    "code": "b2u_on_end( (int)(intptr_t)shapeA->userData - 1, (int)(intptr_t)shapeB->userData - 1 );",
                },
            ],
        }
        if mode == "godot":
            spec["defines"]["B2_PACK_NO_SENSOR_BEGIN_ARRAY"] = 1
            spec["defines"]["B2_PACK_NO_SENSOR_END_ARRAY"] = 1
            spec["inject"] += [
                {
                    "event": "sensor_begin",
                    "code": "b2u_on_begin( (int)(intptr_t)sensorShape->userData - 1, (int)(intptr_t)visitorShape->userData - 1 );",
                },
                {
                    "event": "sensor_end",
                    "code": "if ( visitorShape != NULL ) b2u_on_end( (int)(intptr_t)sensorShape->userData - 1, (int)(intptr_t)visitorShape->userData - 1 );",
                },
            ]
        _write_if_different( inject_path, json.dumps( spec, indent=2 ) + "\n" )
        paths.append( inject_path )
    elif os.path.exists( inject_path ):
        os.remove( inject_path )
    return paths


def build_library( outdir, inject=False, lto=False, box2d_root=None, verbose=False ):
    """
    Build Box2D-Packed for this pack with box2d_pack, into outdir/box2d.
    Returns (library path, include directory, extra compiler defines for the glue).
    """
    root = os.path.abspath( box2d_root or _HERE )
    if root not in sys.path:
        sys.path.insert( 0, root )
    import box2d_pack  # noqa: E402

    inject_path = os.path.join( outdir, INJECT_FILE ) if inject else None
    result = box2d_pack.build(
        None,
        inject_path,
        build_dir=os.path.join( outdir, "box2d" ),
        lto=lto,
        repo_root=root,
        verbose=verbose,
    )
    include_dir = os.path.join( result.source_dir, "include" )
    defines = ["-DB2_PACK_INJECTED=1"] if inject else []
    return result.lib, include_dir, defines


def _with_live_gate( glue ):
    """Glue that disables the bodies of GameObjects engine.c reports as not live."""
    edits = (
        ( "void engine_col2d_contact( int a, int b );\n",
          "void engine_col2d_contact( int a, int b );\n" + LIVE_EXPORTS ),
        ( "static int b2u_pair_n;\n", "static int b2u_pair_n;\n" + LIVE_STATE ),
        ( "\t\tb2BodyId bodyId = b2CreateBody( b2u_world, &def );\n"
          "\t\tb2u_add_shape( bodyId, ci, b2Vec2_zero );\n",
          "\t\tb2BodyId bodyId = b2CreateBody( b2u_world, &def );\n"
          "\t\tb2u_add_shape( bodyId, ci, b2Vec2_zero );\n"
          "\t\tb2u_col_on[ci] = 1;\n" ),
        ( "\t\tb2u_create_body( b2u_rb_created );\n",
          "\t\tb2u_create_body( b2u_rb_created );\n"
          "\t\tb2u_rb_on[b2u_rb_created] = 1;\n" ),
        ( "\t/* Push what scripts may have changed since the last step */\n",
          LIVE_SYNC + "\n\t/* Push what scripts may have changed since the last step */\n" ),
        ( "\t\tb2BodyId bodyId = b2u_rb_body[rb];\n\t\tfloat x, y;\n",
          "\t\tif ( b2u_rb_on[rb] == 0 )\n\t\t\tcontinue;\n"
          "\t\tb2BodyId bodyId = b2u_rb_body[rb];\n\t\tfloat x, y;\n" ),
        ( "\t\tb2BodyId bodyId = b2u_rb_body[rb];\n\t\tb2Pos p = b2Body_GetPosition( bodyId );\n",
          "\t\tif ( b2u_rb_on[rb] == 0 )\n\t\t\tcontinue;\n"
          "\t\tb2BodyId bodyId = b2u_rb_body[rb];\n\t\tb2Pos p = b2Body_GetPosition( bodyId );\n" ),
    )
    for old, new in edits:
        if glue.count( old ) != 1:
            raise ValueError( "box2d_unity: live gate anchor not found: %r" % old[:60] )
        glue = glue.replace( old, new )
    return glue


def _with_contact_manifolds( glue ):
    """Glue that reports each touching pair's manifold (normal, points) before the pair."""
    edits = (
        ( "void engine_col2d_contact( int a, int b );\n",
          "void engine_col2d_contact( int a, int b );\n" + CONTACT_EXPORTS ),
        ( "static int b2u_pair_n;\n", "static int b2u_pair_n;\n" + CONTACT_STATE ),
        ( "\t\tb2CreateCircleShape( bodyId, &def, &circle );\n",
          "\t\tb2u_col_shape[ci] = b2CreateCircleShape( bodyId, &def, &circle );\n"
          "\t\tb2u_col_has_shape[ci] = 1;\n" ),
        ( "\t\tb2CreateCapsuleShape( bodyId, &def, &capsule );\n",
          "\t\tb2u_col_shape[ci] = b2CreateCapsuleShape( bodyId, &def, &capsule );\n"
          "\t\tb2u_col_has_shape[ci] = 1;\n" ),
        ( "\t\tb2CreatePolygonShape( bodyId, &def, &box );\n",
          "\t\tb2u_col_shape[ci] = b2CreatePolygonShape( bodyId, &def, &box );\n"
          "\t\tb2u_col_has_shape[ci] = 1;\n" ),
        ( "\t\tengine_col2d_contact( b2u_pair_a[i], b2u_pair_b[i] );\n",
          "\t\tb2u_report_manifold( b2u_pair_a[i], b2u_pair_b[i] );\n"
          "\t\tengine_col2d_contact( b2u_pair_a[i], b2u_pair_b[i] );\n" ),
    )
    for old, new in edits:
        if glue.count( old ) != 1:
            raise ValueError( "box2d_unity: contact anchor not found: %r" % old[:60] )
        glue = glue.replace( old, new )
    return glue


#: Physics2D.Raycast / OverlapCircle / OverlapPoint (plan["physics2d_queries"]). Each returns the
#: collider index hit (-1 for none). Triggers are hit, as Unity's queriesHitTriggers default has
#: it; a ray ignores a collider it starts inside (b2World_CastRayClosest), where Unity's
#: queriesStartInColliders default would hit it.
QUERY_FUNCTIONS = """
/* Physics2D queries for unity_pack (plan["physics2d_queries"]) */
#include <math.h>
int engine_box2d_raycast( float ox, float oy, float dx, float dy, float distance, float* out );
int engine_box2d_overlap_circle( float x, float y, float radius );
int engine_box2d_overlap_point( float x, float y );

/* out: point x, y, normal x, y, fraction, distance */
int engine_box2d_raycast( float ox, float oy, float dx, float dy, float distance, float* out )
{
	b2u_ensure();
	float len = sqrtf( dx * dx + dy * dy );
	if ( len <= 0.0f )
		return -1;
	if ( !( distance < 1.0e6f ) )
		distance = 1.0e6f;
	b2Vec2 translation = { dx / len * distance, dy / len * distance };
	b2RayResult r = b2World_CastRayClosest( b2u_world, (b2Pos){ ox, oy }, translation, b2DefaultQueryFilter() );
	if ( r.hit == false )
		return -1;
	out[0] = (float)r.point.x;
	out[1] = (float)r.point.y;
	out[2] = r.normal.x;
	out[3] = r.normal.y;
	out[4] = r.fraction;
	out[5] = r.fraction * distance;
	return (int)(intptr_t)b2Shape_GetUserData( r.shapeId ) - 1;
}

static bool b2u_overlap_first( b2ShapeId shapeId, void* context )
{
	*(int*)context = (int)(intptr_t)b2Shape_GetUserData( shapeId ) - 1;
	return false;
}

int engine_box2d_overlap_circle( float x, float y, float radius )
{
	b2u_ensure();
	b2Vec2 center = { 0.0f, 0.0f };
	b2ShapeProxy proxy = b2MakeProxy( &center, 1, radius > 0.0f ? radius : 0.0f );
	int found = -1;
	b2World_OverlapShape( b2u_world, (b2Pos){ x, y }, &proxy, b2DefaultQueryFilter(), b2u_overlap_first, &found );
	return found;
}

int engine_box2d_overlap_point( float x, float y )
{
	return engine_box2d_overlap_circle( x, y, 0.0f );
}
"""


def _with_rotation( glue ):
    """
    Rigidbody2D rotation (plan["physics2d_rotation"], unity mode). A body turns unless its
    Rigidbody2D freezes rotation (RigidbodyConstraints2D.FreezeRotation); it starts at its owner's
    authored angle and angular velocity. Each step pushes what scripts changed -- an angle written
    (rotation / MoveRotation), freezeRotation, angularVelocity, and the torque and angular impulse
    AddTorque accumulated (applied, then cleared) -- and a teleport keeps the rotation; after the
    step the angle and angular velocity are pulled back. engine.c exports

        float engine_rb2d_get_rot( int rb );          owner's rotation about z, radians
        void engine_rb2d_set_rot( int rb, float a );

    Without the flag every body's rotation stays locked, as before.
    """
    def sub( old, new ):
        nonlocal glue
        assert old in glue, "box2d_unity: rotation marker not found: %r" % old[:60]
        glue = glue.replace( old, new, 1 )

    sub( "void engine_box2d_step( void );\n",
         "void engine_box2d_step( void );\n"
         "float engine_rb2d_get_rot( int rb );\n"
         "void engine_rb2d_set_rot( int rb, float a );\n"
         "extern int _Rigidbody2D_freeze_rot[];\n"
         "extern float _Rigidbody2D_ang_vel[];\n"
         "extern float _Rigidbody2D_torque[];\n"
         "extern float _Rigidbody2D_ang_imp[];\n" )
    sub( "static int b2u_last_type[B2U_MAX_RB];\n",
         "static int b2u_last_type[B2U_MAX_RB];\n"
         "/* the angle as last pulled, and freezeRotation as last pushed */\n"
         "static float b2u_last_a[B2U_MAX_RB];\n"
         "static int b2u_last_freeze[B2U_MAX_RB];\n" )
    sub( "\tdef.motionLocks.angularZ = true;\n",
         "\tdef.motionLocks.angularZ = _Rigidbody2D_freeze_rot[rb] != 0;\n"
         "\tdef.rotation = b2MakeRot( engine_rb2d_get_rot( rb ) );\n"
         "\tdef.angularVelocity = _Rigidbody2D_ang_vel[rb];\n" )
    sub( "\tb2u_last_type[rb] = _Rigidbody2D_body_type[rb];\n}\n",
         "\tb2u_last_type[rb] = _Rigidbody2D_body_type[rb];\n"
         "\tb2u_last_a[rb] = engine_rb2d_get_rot( rb );\n"
         "\tb2u_last_freeze[rb] = _Rigidbody2D_freeze_rot[rb];\n}\n" )
    sub( "b2Body_SetTransform( bodyId, (b2Pos){ x, y }, b2Rot_identity );",
         "b2Body_SetTransform( bodyId, (b2Pos){ x, y }, b2Body_GetRotation( bodyId ) );" )
    sub( "\t\tif ( _Rigidbody2D_body_type[rb] != b2u_last_type[rb] )\n",
         "\t\t{\n"
         "\t\t\t/* rotation / MoveRotation written by a script */\n"
         "\t\t\tfloat a = engine_rb2d_get_rot( rb );\n"
         "\t\t\tif ( a != b2u_last_a[rb] )\n"
         "\t\t\t\tb2Body_SetTransform( bodyId, b2Body_GetPosition( bodyId ), b2MakeRot( a ) );\n"
         "\t\t}\n"
         "\t\tif ( _Rigidbody2D_freeze_rot[rb] != b2u_last_freeze[rb] )\n"
         "\t\t{\n"
         "\t\t\tb2MotionLocks locks = { 0 };\n"
         "\t\t\tlocks.angularZ = _Rigidbody2D_freeze_rot[rb] != 0;\n"
         "\t\t\tb2Body_SetMotionLocks( bodyId, locks );\n"
         "\t\t\tb2u_last_freeze[rb] = _Rigidbody2D_freeze_rot[rb];\n"
         "\t\t}\n"
         "\t\tif ( _Rigidbody2D_body_type[rb] != b2u_last_type[rb] )\n" )
    sub( "\t\t\tb2Body_SetLinearVelocity( bodyId, (b2Vec2){ _Rigidbody2D_vel_x[rb], _Rigidbody2D_vel_y[rb] } );\n",
         "\t\t\tb2Body_SetLinearVelocity( bodyId, (b2Vec2){ _Rigidbody2D_vel_x[rb], _Rigidbody2D_vel_y[rb] } );\n"
         "\t\t\tb2Body_SetAngularVelocity( bodyId, _Rigidbody2D_ang_vel[rb] );\n"
         "\t\t\tif ( _Rigidbody2D_torque[rb] != 0.0f )\n"
         "\t\t\t\tb2Body_ApplyTorque( bodyId, _Rigidbody2D_torque[rb], true );\n"
         "\t\t\tif ( _Rigidbody2D_ang_imp[rb] != 0.0f )\n"
         "\t\t\t\tb2Body_ApplyAngularImpulse( bodyId, _Rigidbody2D_ang_imp[rb], true );\n"
         "\t\t\t_Rigidbody2D_torque[rb] = 0.0f;\n"
         "\t\t\t_Rigidbody2D_ang_imp[rb] = 0.0f;\n" )
    sub( "\t\t_Rigidbody2D_vel_y[rb] = v.y;\n",
         "\t\t_Rigidbody2D_vel_y[rb] = v.y;\n"
         "\t\tengine_rb2d_set_rot( rb, b2Rot_GetAngle( b2Body_GetRotation( bodyId ) ) );\n"
         "\t\tb2u_last_a[rb] = engine_rb2d_get_rot( rb );\n"
         "\t\t_Rigidbody2D_ang_vel[rb] = b2Body_GetAngularVelocity( bodyId );\n" )
    return glue


def _with_unity_triggers( glue ):
    """
    OnTriggerEnter2D / Stay2D / Exit2D (plan["physics2d_triggers"], unity mode). Every shape takes
    sensor events -- a trigger collider is a sensor already -- and the overlapping sensor pairs are
    kept from Box2D's sensor begin and end events, apart from the touching pairs, and reported after
    the step with

        void engine_col2d_trigger( int a, int b );

    unity_pack sends the messages by comparing them with the step before, as it does collisions.
    Standard API only: with --physics-inject the triggers stay silent.
    """
    glue = glue.replace(
        "\tdef.enableContactEvents = _Collider2D_is_trigger[ci] == 0;\n",
        "\tdef.enableContactEvents = _Collider2D_is_trigger[ci] == 0;\n"
        "\t/* OnTrigger*2D: every shape takes sensor events */\n"
        "\tdef.enableSensorEvents = true;\n", 1 )
    glue = glue.replace(
        "void b2u_on_begin( int colliderA, int colliderB );\n",
        "void b2u_on_begin( int colliderA, int colliderB );\n"
        "void engine_col2d_trigger( int a, int b );\n", 1 )
    storage = (
        "/* Overlapping sensor pairs, lo < hi, for OnTrigger*2D */\n"
        "static int b2u_trig_a[B2U_MAX_PAIRS];\n"
        "static int b2u_trig_b[B2U_MAX_PAIRS];\n"
        "static int b2u_trig_n;\n\n"
        "static void b2u_trig_begin( int a, int b )\n"
        "{\n"
        "\tint lo = a < b ? a : b;\n"
        "\tint hi = a < b ? b : a;\n"
        "\tif ( lo < 0 || lo == hi )\n"
        "\t\treturn;\n"
        "\tfor ( int i = 0; i < b2u_trig_n; ++i )\n"
        "\t\tif ( b2u_trig_a[i] == lo && b2u_trig_b[i] == hi )\n"
        "\t\t\treturn;\n"
        "\tif ( b2u_trig_n < B2U_MAX_PAIRS )\n"
        "\t{\n"
        "\t\tb2u_trig_a[b2u_trig_n] = lo;\n"
        "\t\tb2u_trig_b[b2u_trig_n] = hi;\n"
        "\t\tb2u_trig_n += 1;\n"
        "\t}\n"
        "}\n\n"
        "static void b2u_trig_end( int a, int b )\n"
        "{\n"
        "\tint lo = a < b ? a : b;\n"
        "\tint hi = a < b ? b : a;\n"
        "\tfor ( int i = 0; i < b2u_trig_n; ++i )\n"
        "\t{\n"
        "\t\tif ( b2u_trig_a[i] == lo && b2u_trig_b[i] == hi )\n"
        "\t\t{\n"
        "\t\t\tfor ( int k = i + 1; k < b2u_trig_n; ++k )\n"
        "\t\t\t{\n"
        "\t\t\t\tb2u_trig_a[k - 1] = b2u_trig_a[k];\n"
        "\t\t\t\tb2u_trig_b[k - 1] = b2u_trig_b[k];\n"
        "\t\t\t}\n"
        "\t\t\tb2u_trig_n -= 1;\n"
        "\t\t\treturn;\n"
        "\t\t}\n"
        "\t}\n"
        "}\n\n" )
    glue = glue.replace( "void b2u_on_begin( int colliderA, int colliderB )\n{",
                         storage + "void b2u_on_begin( int colliderA, int colliderB )\n{", 1 )
    events = (
        "\t/* OnTrigger*2D: sensor overlaps, apart from the touching pairs */\n"
        "\tb2SensorEvents sensors = b2World_GetSensorEvents( b2u_world );\n"
        "\tfor ( int i = 0; i < sensors.beginCount; ++i )\n"
        "\t{\n"
        "\t\tb2SensorBeginTouchEvent* e = sensors.beginEvents + i;\n"
        "\t\tb2u_trig_begin( (int)(intptr_t)b2Shape_GetUserData( e->sensorShapeId ) - 1,\n"
        "\t\t\t\t\t\t(int)(intptr_t)b2Shape_GetUserData( e->visitorShapeId ) - 1 );\n"
        "\t}\n"
        "\tfor ( int i = 0; i < sensors.endCount; ++i )\n"
        "\t{\n"
        "\t\tb2SensorEndTouchEvent* e = sensors.endEvents + i;\n"
        "\t\tif ( b2Shape_IsValid( e->sensorShapeId ) && b2Shape_IsValid( e->visitorShapeId ) )\n"
        "\t\t{\n"
        "\t\t\tb2u_trig_end( (int)(intptr_t)b2Shape_GetUserData( e->sensorShapeId ) - 1,\n"
        "\t\t\t\t\t\t  (int)(intptr_t)b2Shape_GetUserData( e->visitorShapeId ) - 1 );\n"
        "\t\t}\n"
        "\t}\n" )
    marker = "#endif\n\n\t/* Pull positions and velocities into the packed tables */"
    assert marker in glue, "box2d_unity: trigger events marker not found"
    glue = glue.replace( marker, events + marker, 1 )
    report_marker = "\t/* unity_pack sends Enter / Stay / Exit by comparing with the previous step */\n"
    assert report_marker in glue, "box2d_unity: trigger report marker not found"
    glue = glue.replace(
        report_marker,
        "\tfor ( int i = 0; i < b2u_trig_n; ++i )\n"
        "\t{\n"
        "\t\tengine_col2d_trigger( b2u_trig_a[i], b2u_trig_b[i] );\n"
        "\t}\n" + report_marker, 1 )
    return glue


def _write_if_different( path, text ):
    try:
        with open( path, encoding="utf-8" ) as f:
            if f.read() == text:
                return
    except OSError:
        pass
    with open( path, "w", encoding="utf-8" ) as f:
        f.write( text )


COLLIDER_EXTERNS = """extern const int _Collider2D_count;
extern const int _Collider2D_kind[];
extern const int _Collider2D_is_trigger[];
extern const int _Collider2D_rb2d[];
extern const float _Collider2D_ox[];
extern const float _Collider2D_oy[];
extern const float _Collider2D_hw[];
extern const float _Collider2D_hh[];
extern const float _Collider2D_cos[];
extern const float _Collider2D_sin[];
extern const float _Collider2D_friction[];
extern const float _Collider2D_bounciness[];
extern const int _Collider2D_friction_combine[];
extern const int _Collider2D_bounce_combine[];
"""

CONTACT_EXPORTS = """void engine_col2d_manifold( int a, int b, float nx, float ny, int n, float p0x, float p0y,
							float p1x, float p1y );
"""

CONTACT_STATE = """
/* Each collider's shape, to read a touching pair's manifold after the step */
static b2ShapeId b2u_col_shape[B2U_MAX_COL];
static int b2u_col_has_shape[B2U_MAX_COL];

/* Report the manifold of pair (a, b): normal from a to b, world points */
static void b2u_report_manifold( int a, int b )
{
	b2ContactData data[32];
	if ( a < 0 || b < 0 || b2u_col_has_shape[a] == 0 || b2u_col_has_shape[b] == 0 )
		return;
	int n = b2Shape_GetContactData( b2u_col_shape[a], data, 32 );
	for ( int k = 0; k < n; ++k )
	{
		int flip;
		if ( B2_ID_EQUALS( data[k].shapeIdA, b2u_col_shape[a] ) && B2_ID_EQUALS( data[k].shapeIdB, b2u_col_shape[b] ) )
			flip = 0;
		else if ( B2_ID_EQUALS( data[k].shapeIdA, b2u_col_shape[b] ) && B2_ID_EQUALS( data[k].shapeIdB, b2u_col_shape[a] ) )
			flip = 1;
		else
			continue;
		b2Manifold* m = &data[k].manifold;
		b2Pos center = b2Body_GetWorldCenter( b2Shape_GetBody( data[k].shapeIdA ) );
		float px[2] = { 0.0f, 0.0f }, py[2] = { 0.0f, 0.0f };
		int count = m->pointCount < 2 ? m->pointCount : 2;
		for ( int q = 0; q < count; ++q )
		{
			b2Pos w = b2OffsetPos( center, m->points[q].anchorA );
			px[q] = (float)w.x;
			py[q] = (float)w.y;
		}
		float nx = flip ? -m->normal.x : m->normal.x;
		float ny = flip ? -m->normal.y : m->normal.y;
		engine_col2d_manifold( a, b, nx, ny, count, px[0], py[0], px[1], py[1] );
		return;
	}
}
"""

LIVE_EXPORTS = """int engine_rb2d_live( int rb );
int engine_col2d_live( int ci );
"""

LIVE_STATE = """
/* Which bodies are in the simulation (engine_rb2d_live / engine_col2d_live) */
static int b2u_rb_on[B2U_MAX_RB];
static int b2u_col_on[B2U_MAX_COL];

/* A disabled body's contacts are gone: forget its touching pairs */
static void b2u_drop_pairs( int rb, int ci )
{
	int k = 0;
	for ( int i = 0; i < b2u_pair_n; ++i )
	{
		int a = b2u_pair_a[i], b = b2u_pair_b[i];
		int hit = a == ci || b == ci;
		if ( rb >= 0 )
		{
			hit = hit || ( a < _Collider2D_count && _Collider2D_rb2d[a] == rb ) ||
				  ( b < _Collider2D_count && _Collider2D_rb2d[b] == rb );
		}
		if ( hit )
			continue;
		b2u_pair_a[k] = a;
		b2u_pair_b[k] = b;
		k += 1;
	}
	b2u_pair_n = k;
}
"""

LIVE_SYNC = """\t/* Bodies of GameObjects that left or rejoined the simulation */
\tfor ( int rb = 0; rb < b2u_rb_created; ++rb )
\t{
\t\tint on = engine_rb2d_live( rb ) != 0;
\t\tif ( on == b2u_rb_on[rb] )
\t\t\tcontinue;
\t\tb2u_rb_on[rb] = on;
\t\tif ( on )
\t\t{
\t\t\tb2Body_Enable( b2u_rb_body[rb] );
\t\t}
\t\telse
\t\t{
\t\t\tb2Body_Disable( b2u_rb_body[rb] );
\t\t\tb2u_drop_pairs( rb, -1 );
\t\t}
\t}
\tfor ( int ci = 0; ci < _Collider2D_count && ci < B2U_MAX_COL; ++ci )
\t{
\t\tif ( b2u_col_has_body[ci] == 0 )
\t\t\tcontinue;
\t\tint on = engine_col2d_live( ci ) != 0;
\t\tif ( on == b2u_col_on[ci] )
\t\t\tcontinue;
\t\tb2u_col_on[ci] = on;
\t\tif ( on )
\t\t{
\t\t\tb2Body_Enable( b2u_col_body[ci] );
\t\t}
\t\telse
\t\t{
\t\t\tb2Body_Disable( b2u_col_body[ci] );
\t\t\tb2u_drop_pairs( -1, ci );
\t\t}
\t}
"""

GLUE_TEMPLATE = r"""/* Generated by box2d_unity.py for {PACKER}. Do not edit. */

#include "box2d/box2d.h"

#include <stdint.h>
{EXTRA_INCLUDES}
#ifndef B2_PACK_INJECTED
#define B2_PACK_INJECTED 0
#endif

/* unity_pack tables (data.c) */
extern float Time_fixedDeltaTime;
extern float Physics2D_gravity_x;
extern float Physics2D_gravity_y;
extern int _Rigidbody2D_count;
extern float _Rigidbody2D_vel_x[];
extern float _Rigidbody2D_vel_y[];
extern float _Rigidbody2D_gravity_scale[];
extern float _Rigidbody2D_linear_damping[];
extern float _Rigidbody2D_mass[];
extern int _Rigidbody2D_body_type[];
{COLLIDER_DECLS}
/* unity_pack exports (engine.c) */
void engine_rb2d_get_pos( int rb, float* x, float* y );
void engine_rb2d_set_pos( int rb, float x, float y );
void engine_col2d_center( int ci, float* x, float* y );
void engine_col2d_contact( int a, int b );

void engine_box2d_step( void );
void b2u_on_begin( int colliderA, int colliderB );
void b2u_on_end( int colliderA, int colliderB );

enum
{{
	B2U_MAX_RB = {N_RB},
	B2U_MAX_COL = {N_COL},
	B2U_MAX_PAIRS = {MAX_PAIRS},
	B2U_SUB_STEPS = {SUB_STEPS},
}};

static int b2u_ready;
static int b2u_rb_created;
static b2WorldId b2u_world;
static b2BodyId b2u_rb_body[B2U_MAX_RB];
static float b2u_last_x[B2U_MAX_RB];
static float b2u_last_y[B2U_MAX_RB];
/* Rigidbody2D.mass / bodyType as last pushed: a script may change them */
static float b2u_last_mass[B2U_MAX_RB];
static int b2u_last_type[B2U_MAX_RB];

/* Static bodies of the colliders without a Rigidbody2D */
static b2BodyId b2u_col_body[B2U_MAX_COL];
static int b2u_col_has_body[B2U_MAX_COL];

/* Touching collider pairs, lo < hi, maintained from contact begin and end */
static int b2u_pair_a[B2U_MAX_PAIRS];
static int b2u_pair_b[B2U_MAX_PAIRS];
static int b2u_pair_n;

void b2u_on_begin( int colliderA, int colliderB )
{{
	int lo = colliderA < colliderB ? colliderA : colliderB;
	int hi = colliderA < colliderB ? colliderB : colliderA;
	if ( lo < 0 || lo == hi )
	{{
		return;
	}}
	for ( int i = 0; i < b2u_pair_n; ++i )
	{{
		if ( b2u_pair_a[i] == lo && b2u_pair_b[i] == hi )
		{{
			return;
		}}
	}}
	if ( b2u_pair_n < B2U_MAX_PAIRS )
	{{
		b2u_pair_a[b2u_pair_n] = lo;
		b2u_pair_b[b2u_pair_n] = hi;
		b2u_pair_n += 1;
	}}
}}

void b2u_on_end( int colliderA, int colliderB )
{{
	int lo = colliderA < colliderB ? colliderA : colliderB;
	int hi = colliderA < colliderB ? colliderB : colliderA;
	for ( int i = 0; i < b2u_pair_n; ++i )
	{{
		if ( b2u_pair_a[i] == lo && b2u_pair_b[i] == hi )
		{{
			/* Keep order stable, messages are sent in pair order */
			for ( int k = i + 1; k < b2u_pair_n; ++k )
			{{
				b2u_pair_a[k - 1] = b2u_pair_a[k];
				b2u_pair_b[k - 1] = b2u_pair_b[k];
			}}
			b2u_pair_n -= 1;
			return;
		}}
	}}
}}

{MATERIALS}static b2BodyType b2u_body_type( int unityType )
{{
	/* Rigidbody2D.bodyType: Dynamic=0 Kinematic=1 Static=2 */
	if ( unityType == 1 )
		return b2_kinematicBody;
	if ( unityType == 2 )
		return b2_staticBody;
	return b2_dynamicBody;
}}

static void b2u_add_shape( b2BodyId bodyId, int ci, b2Vec2 offset )
{{
	b2ShapeDef def = b2DefaultShapeDef();
	def.userData = (void*)(intptr_t)( ci + 1 );
	def.material.friction = _Collider2D_friction[ci];
	def.material.restitution = _Collider2D_bounciness[ci];
	def.material.userMaterialId =
		(uint64_t)( _Collider2D_friction_combine[ci] & 0xff ) | ( (uint64_t)( _Collider2D_bounce_combine[ci] & 0xff ) << 8 );
	def.isSensor = _Collider2D_is_trigger[ci] != 0;
	def.enableContactEvents = _Collider2D_is_trigger[ci] == 0;
{SHAPE_EXTRA}
	b2Rot rotation = {{ _Collider2D_cos[ci], _Collider2D_sin[ci] }};
	/* CapsuleCollider2D: kind 2 vertical, 3 horizontal. One no longer than it
	 * is wide is a circle, as in Unity (Box2D refuses a zero-length capsule). */
	int cap = _Collider2D_kind[ci] >= 2, vert = _Collider2D_kind[ci] == 2;
	float r = !cap || vert ? _Collider2D_hw[ci] : _Collider2D_hh[ci];
	float h = cap ? ( vert ? _Collider2D_hh[ci] : _Collider2D_hw[ci] ) - r : 0.0f;
	if ( _Collider2D_kind[ci] == 1 || ( cap && h <= 0.005f ) )
	{{
		b2Circle circle = {{ offset, r }};
		b2CreateCircleShape( bodyId, &def, &circle );
	}}
	else if ( cap )
	{{
		b2Vec2 d = b2RotateVector( rotation, vert ? (b2Vec2){{ 0.0f, h }} : (b2Vec2){{ h, 0.0f }} );
		b2Capsule capsule = {{ b2Sub( offset, d ), b2Add( offset, d ), r }};
		b2CreateCapsuleShape( bodyId, &def, &capsule );
	}}
	else
	{{
		b2Polygon box = b2MakeOffsetBox( _Collider2D_hw[ci], _Collider2D_hh[ci], offset, rotation );
		b2CreatePolygonShape( bodyId, &def, &box );
	}}
}}

/* Body for Rigidbody2D rb, with the colliders the scene attached to it */
static void b2u_create_body( int rb )
{{
	float x, y;
	engine_rb2d_get_pos( rb, &x, &y );
	b2BodyDef def = b2DefaultBodyDef();
	def.type = b2u_body_type( _Rigidbody2D_body_type[rb] );
	def.position = (b2Pos){{ x, y }};
	def.linearVelocity = (b2Vec2){{ _Rigidbody2D_vel_x[rb], _Rigidbody2D_vel_y[rb] }};
	def.gravityScale = _Rigidbody2D_gravity_scale[rb];
	def.linearDamping = {LINEAR_DAMPING};
	def.motionLocks.angularZ = true;
	b2BodyId bodyId = b2CreateBody( b2u_world, &def );
	b2u_rb_body[rb] = bodyId;
	b2u_last_x[rb] = x;
	b2u_last_y[rb] = y;

	for ( int ci = 0; ci < _Collider2D_count && ci < B2U_MAX_COL; ++ci )
	{{
		if ( _Collider2D_rb2d[ci] != rb )
			continue;
		/* Offset relative to the body origin, rotated like the collider */
		float c = _Collider2D_cos[ci], s = _Collider2D_sin[ci];
		float ox = _Collider2D_ox[ci], oy = _Collider2D_oy[ci];
		b2u_add_shape( bodyId, ci, (b2Vec2){{ c * ox - s * oy, s * ox + c * oy }} );
	}}

	/* Rigidbody2D.mass: scale the shape-derived mass data to the authored mass */
	if ( _Rigidbody2D_body_type[rb] == 0 && _Rigidbody2D_mass[rb] > 0.0f )
	{{
		b2MassData md = b2Body_GetMassData( bodyId );
		float mass = _Rigidbody2D_mass[rb];
		if ( md.mass > 0.0f )
		{{
			md.rotationalInertia *= mass / md.mass;
		}}
		md.mass = mass;
		b2Body_SetMassData( bodyId, md );
	}}
	b2u_last_mass[rb] = _Rigidbody2D_mass[rb];
	b2u_last_type[rb] = _Rigidbody2D_body_type[rb];
}}

static void b2u_create( void )
{{
{WORLD_PRELUDE}	b2WorldDef worldDef = b2DefaultWorldDef();
	worldDef.gravity = (b2Vec2){{ Physics2D_gravity_x, Physics2D_gravity_y }};
	worldDef.frictionCallback = b2u_friction;
	worldDef.restitutionCallback = b2u_restitution;
	b2u_world = b2CreateWorld( &worldDef );

	/* Colliders without a Rigidbody2D are static bodies at the collider center */
	for ( int ci = 0; ci < _Collider2D_count && ci < B2U_MAX_COL; ++ci )
	{{
		int rb = _Collider2D_rb2d[ci];
		if ( rb >= 0 && rb < B2U_MAX_RB )
			continue;
		float x, y;
		engine_col2d_center( ci, &x, &y );
		b2BodyDef def = b2DefaultBodyDef();
		def.position = (b2Pos){{ x, y }};
		b2BodyId bodyId = b2CreateBody( b2u_world, &def );
		b2u_add_shape( bodyId, ci, b2Vec2_zero );
		b2u_col_body[ci] = bodyId;
		b2u_col_has_body[ci] = 1;
	}}

	b2u_rb_created = 0;
	b2u_pair_n = 0;
	b2u_ready = 1;
}}

/* The world, and every body so far: on the first step, or a query before it
 * (a script's Start), and AddComponent<Rigidbody2D> bodies when they appear */
static void b2u_ensure( void )
{{
	if ( b2u_ready == 0 )
	{{
		b2u_create();
	}}
	while ( b2u_rb_created < _Rigidbody2D_count && b2u_rb_created < B2U_MAX_RB )
	{{
		b2u_create_body( b2u_rb_created );
		b2u_rb_created += 1;
	}}
}}

void engine_box2d_step( void )
{{
	b2u_ensure();

	/* A static collider whose Transform moved (a parent, a script) is teleported, as in Unity */
	for ( int ci = 0; ci < _Collider2D_count && ci < B2U_MAX_COL; ++ci )
	{{
		if ( b2u_col_has_body[ci] == 0 )
			continue;
		float x, y;
		engine_col2d_center( ci, &x, &y );
		b2BodyId bodyId = b2u_col_body[ci];
		b2Pos p = b2Body_GetPosition( bodyId );
		if ( p.x != x || p.y != y )
			b2Body_SetTransform( bodyId, (b2Pos){{ x, y }}, b2Body_GetRotation( bodyId ) );
	}}

	/* Push what scripts may have changed since the last step */
	b2World_SetGravity( b2u_world, (b2Vec2){{ Physics2D_gravity_x, Physics2D_gravity_y }} );
	for ( int rb = 0; rb < b2u_rb_created; ++rb )
	{{
		b2BodyId bodyId = b2u_rb_body[rb];
		float x, y;
		engine_rb2d_get_pos( rb, &x, &y );
		if ( x != b2u_last_x[rb] || y != b2u_last_y[rb] )
		{{
			/* transform.position written by a script */
			b2Body_SetTransform( bodyId, (b2Pos){{ x, y }}, b2Rot_identity );
		}}
		if ( _Rigidbody2D_body_type[rb] != b2u_last_type[rb] )
		{{
			/* Rigidbody2D.bodyType / isKinematic written by a script */
			b2Body_SetType( bodyId, b2u_body_type( _Rigidbody2D_body_type[rb] ) );
			b2u_last_type[rb] = _Rigidbody2D_body_type[rb];
			b2u_last_mass[rb] = -1.0f;
		}}
		if ( _Rigidbody2D_body_type[rb] == 0 && _Rigidbody2D_mass[rb] > 0.0f &&
			 _Rigidbody2D_mass[rb] != b2u_last_mass[rb] )
		{{
			/* Rigidbody2D.mass written by a script: the mass data scaled to it */
			b2MassData md = b2Body_GetMassData( bodyId );
			float mass = _Rigidbody2D_mass[rb];
			if ( md.mass > 0.0f )
			{{
				md.rotationalInertia *= mass / md.mass;
			}}
			md.mass = mass;
			b2Body_SetMassData( bodyId, md );
			b2u_last_mass[rb] = mass;
		}}
		if ( _Rigidbody2D_body_type[rb] != 2 )
		{{
			b2Body_SetLinearVelocity( bodyId, (b2Vec2){{ _Rigidbody2D_vel_x[rb], _Rigidbody2D_vel_y[rb] }} );
		}}
		if ( _Rigidbody2D_body_type[rb] == 0 )
		{{
			b2Body_SetGravityScale( bodyId, _Rigidbody2D_gravity_scale[rb] );
			b2Body_SetLinearDamping( bodyId, {LINEAR_DAMPING} );
		}}
	}}

	float dt = Time_fixedDeltaTime > 1e-8f ? Time_fixedDeltaTime : {DEFAULT_DT};
	b2World_Step( b2u_world, dt, B2U_SUB_STEPS );

#if B2_PACK_INJECTED == 0
	/* Standard API: touching pairs from the contact event arrays */
	b2ContactEvents events = b2World_GetContactEvents( b2u_world );
	for ( int i = 0; i < events.beginCount; ++i )
	{{
		b2ContactBeginTouchEvent* e = events.beginEvents + i;
		b2u_on_begin( (int)(intptr_t)b2Shape_GetUserData( e->shapeIdA ) - 1,
					  (int)(intptr_t)b2Shape_GetUserData( e->shapeIdB ) - 1 );
	}}
	for ( int i = 0; i < events.endCount; ++i )
	{{
		b2ContactEndTouchEvent* e = events.endEvents + i;
		if ( b2Shape_IsValid( e->shapeIdA ) && b2Shape_IsValid( e->shapeIdB ) )
		{{
			b2u_on_end( (int)(intptr_t)b2Shape_GetUserData( e->shapeIdA ) - 1,
						(int)(intptr_t)b2Shape_GetUserData( e->shapeIdB ) - 1 );
		}}
	}}
{SENSOR_EVENTS}#endif

	/* Pull positions and velocities into the packed tables */
	for ( int rb = 0; rb < b2u_rb_created; ++rb )
	{{
		b2BodyId bodyId = b2u_rb_body[rb];
		b2Pos p = b2Body_GetPosition( bodyId );
		b2Vec2 v = b2Body_GetLinearVelocity( bodyId );
		float x = (float)p.x, y = (float)p.y;
		engine_rb2d_set_pos( rb, x, y );
		b2u_last_x[rb] = x;
		b2u_last_y[rb] = y;
		_Rigidbody2D_vel_x[rb] = v.x;
		_Rigidbody2D_vel_y[rb] = v.y;
	}}

	/* unity_pack sends Enter / Stay / Exit by comparing with the previous step */
	for ( int i = 0; i < b2u_pair_n; ++i )
	{{
		engine_col2d_contact( b2u_pair_a[i], b2u_pair_b[i] );
	}}
}}
"""


UNITY_MATERIALS = r"""/* PhysicsMaterialCombine: Average=0 Multiply=1 Minimum=2 Maximum=3, the higher mode wins */
static float b2u_combine( float a, float b, int ca, int cb )
{{
	int mode = ca > cb ? ca : cb;
	if ( mode > 3 )
		mode = 0;
	if ( mode == 1 )
		return a * b;
	if ( mode == 2 )
		return a < b ? a : b;
	if ( mode == 3 )
		return a > b ? a : b;
	return 0.5f * ( a + b );
}}

/* userMaterialId carries the combine modes: friction in bits 0-7, bounce in bits 8-15 */
static float b2u_friction( float a, uint64_t ma, float b, uint64_t mb )
{{
	return b2u_combine( a, b, (int)( ma & 0xff ), (int)( mb & 0xff ) );
}}

static float b2u_restitution( float a, uint64_t ma, float b, uint64_t mb )
{{
	return b2u_combine( a, b, (int)( ( ma >> 8 ) & 0xff ), (int)( ( mb >> 8 ) & 0xff ) );
}}

"""

GODOT_MATERIALS = r"""/* Godot's PhysicsMaterial: friction |min(a, b)| and bounce clamp(a + b, 0, 1), where a rough
   material's friction and an absorbent material's bounce count negative (so rough wins the min
   and absorbent subtracts). userMaterialId: rough in bits 0-7, absorbent in bits 8-15 */
static float b2u_friction( float a, uint64_t ma, float b, uint64_t mb )
{{
	float fa = ( ma & 0xff ) != 0 ? -a : a;
	float fb = ( mb & 0xff ) != 0 ? -b : b;
	float f = fa < fb ? fa : fb;
	return f < 0.0f ? -f : f;
}}

static float b2u_restitution( float a, uint64_t ma, float b, uint64_t mb )
{{
	float ba = ( ( ma >> 8 ) & 0xff ) != 0 ? -a : a;
	float bb = ( ( mb >> 8 ) & 0xff ) != 0 ? -b : b;
	float r = ba + bb;
	if ( r < 0.0f )
		return 0.0f;
	if ( r > 1.0f )
		return 1.0f;
	return r;
}}

/* Godot damps once a step, v *= max(0, 1 - dt * d); Box2D once a substep, v *= 1 / (1 + h * c).
   The c whose B2U_SUB_STEPS substeps compound to Godot's factor for the step: */
static float b2g_linear_damping( float d )
{{
	float dt = Time_fixedDeltaTime > 1e-8f ? Time_fixedDeltaTime : ( 1.0f / 60.0f );
	float h = dt / (float)B2U_SUB_STEPS;
	float f = 1.0f - dt * d;
	if ( d <= 0.0f )
		return 0.0f;
	if ( f <= 1e-6f )
		return 1e6f / h; /* stopped within the step */
	return ( powf( f, -1.0f / (float)B2U_SUB_STEPS ) - 1.0f ) / h;
}}

"""


GODOT_SENSOR_EVENTS = """\t/* Godot: Area2D overlaps, reported as touching pairs like contacts */
\tb2SensorEvents sensors = b2World_GetSensorEvents( b2u_world );
\tfor ( int i = 0; i < sensors.beginCount; ++i )
\t{
\t\tb2SensorBeginTouchEvent* e = sensors.beginEvents + i;
\t\tb2u_on_begin( (int)(intptr_t)b2Shape_GetUserData( e->sensorShapeId ) - 1,
\t\t\t\t\t  (int)(intptr_t)b2Shape_GetUserData( e->visitorShapeId ) - 1 );
\t}
\tfor ( int i = 0; i < sensors.endCount; ++i )
\t{
\t\tb2SensorEndTouchEvent* e = sensors.endEvents + i;
\t\tif ( b2Shape_IsValid( e->sensorShapeId ) && b2Shape_IsValid( e->visitorShapeId ) )
\t\t{
\t\t\tb2u_on_end( (int)(intptr_t)b2Shape_GetUserData( e->sensorShapeId ) - 1,
\t\t\t\t\t\t(int)(intptr_t)b2Shape_GetUserData( e->visitorShapeId ) - 1 );
\t\t}
\t}
"""


def _mode_parts( mode, length_units_per_meter ):
    """The pieces of GLUE_TEMPLATE that differ by engine. unity's reproduce the glue as it was."""
    if mode == "unity":
        return {
            "PACKER": "unity_pack",
            "EXTRA_INCLUDES": "",
            "MATERIALS": UNITY_MATERIALS.replace( "{{", "{" ).replace( "}}", "}" ),
            "LINEAR_DAMPING": "_Rigidbody2D_linear_damping[rb]",
            "WORLD_PRELUDE": "",
            "DEFAULT_DT": "0.02f",
            "SHAPE_EXTRA": "",
            "SENSOR_EVENTS": "",
        }
    return {
        "PACKER": "godot_pack",
        "EXTRA_INCLUDES": "#include <math.h>\n",
        "MATERIALS": GODOT_MATERIALS.replace( "{{", "{" ).replace( "}}", "}" ),
        "LINEAR_DAMPING": "b2g_linear_damping( _Rigidbody2D_linear_damping[rb] )",
        "WORLD_PRELUDE": (
            "\t/* Godot's units are pixels: Box2D's tolerances and default speeds scale to them.\n"
            "\t   Set before any b2Default*Def, which read it */\n"
            "\tb2SetLengthUnitsPerMeter( %sf );\n" % repr( float( length_units_per_meter ) ) ),
        "DEFAULT_DT": "( 1.0f / 60.0f )",
        "SHAPE_EXTRA": (
            "\t/* Godot: areas see every body and area; both shapes take sensor events */\n"
            "\tdef.enableSensorEvents = true;\n" ),
        "SENSOR_EVENTS": GODOT_SENSOR_EVENTS,
    }
