#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
# SPDX-License-Identifier: MIT
"""
Integration test: Box2D-Packed's Godot mode, the physics backend of crust's tools/godot_pack.py.

This packs test/godot/Bounce (a Godot 4 project) two ways (Box2D event arrays, and
--physics-inject), links bounce_host.c against each, and checks what Godot would do:
  1. gravity is Godot's 980 px/s^2, down: each ball first touches down when free fall says
  2. PhysicsMaterial bounce combines as clamp(a + b, 0, 1): BallA (0.5 on a 0 floor) rebounds to
     0.25 of its drop, BallB (0.5 on an absorbent 0.3 pad) to 0.2^2 = 0.04 of it. Unity's
     averaging would give 0.0625; ignoring absorbent, 0.64
  3. linear_damp is Godot's v *= 1 - dt * d a step: the Drifter (damp 2, no gravity) keeps
     100 * (1 - 2/60)^60 px/s after one second
  4. the six instanced crates stand, in pixel units (b2SetLengthUnitsPerMeter)
  5. signals, from Box2D contacts and sensor (Area2D) overlaps: BallA's body_entered / exited
     (wired with += in _Ready) count its bounces, one more enter than exit as it comes to rest;
     BallB does not report contacts, so Godot sends it nothing; the Zone (wired by the scene's
     [connection]s) sees BallB enter and leave once, by group, type and name; the Coin frees
     itself on the first Ball, and a freed node sends nothing more though BallA passes through
     it again
  6. the injected build prints exactly what the standard build prints

Usage: python3 test/godot/run_godot_tests.py --crust PATH   (or set CRUST_ROOT)
"""
import argparse
import math
import os
import re
import subprocess
import sys

HERE = os.path.dirname( os.path.abspath( __file__ ) )
BOX2D_ROOT = os.path.dirname( os.path.dirname( HERE ) )
GRAVITY = 980.0
DT = 1.0 / 60.0


def pack_and_run( godot_pack, out, extra ):
    cmd = [sys.executable, godot_pack, os.path.join( HERE, "Bounce" ), "-o", out, "--force",
           "--box2d", BOX2D_ROOT] + extra
    r = subprocess.run( cmd, capture_output=True, text=True )
    if r.returncode != 0:
        raise SystemExit( "godot_pack failed:\n" + r.stderr[-3000:] )
    objs = [os.path.join( out, n ) for n in ( "engine.o", "data.o", "physics_box2d.o" )]
    libs = [os.path.join( out, "box2d", "libbox2d.a" ), "-lpthread"]
    exe = os.path.join( out, "bounce_host" )
    subprocess.run( ["gcc", "-O2", "-o", exe, os.path.join( HERE, "bounce_host.c" )] + objs + libs + ["-lm"],
                    check=True )
    return subprocess.run( [exe], capture_output=True, text=True, check=True ).stdout.strip()


def parse( text ):
    out = {}
    out["drifter"] = float( re.search( r"drifter vx (\S+)", text ).group( 1 ) )
    for ball in ( "A", "B" ):
        m = re.search( r"ball%s land (\d+) rebound (\S+) rest (\S+)" % ball, text )
        out[ball] = ( int( m.group( 1 ) ), float( m.group( 2 ) ), float( m.group( 3 ) ) )
    out["crates"] = [float( v ) for v in re.search( r"crates y: (.*)", text ).group( 1 ).split()]
    out["contacts"] = [( int( a ), int( b ) ) for a, b in re.findall( r"contacts enters=(\d+) exits=(\d+)", text )]
    out["zone"] = re.findall( r"zone entered by (\w+)", text )
    out["zone_counts"] = re.search( r"zone balls=(\d+) left=(\d+)", text ).groups()
    out["coin"] = re.findall( r"coin taken by (\w+)", text )
    return out


def check( name, r ):
    failures = []
    want = 100.0 * ( 1.0 - 2.0 * DT ) ** 60
    if abs( r["drifter"] - want ) > 0.005 * want:
        failures.append( f"{name}: Drifter vx {r['drifter']}, Godot's damping gives {want:.3f}" )
    # ball, start y, rest y, restitution: Godot's clamp(a + b, 0, 1) with absorbent negative
    for ball, y0, rest, e in ( ( "A", 200.0, 370.0, 0.5 + 0.0 ), ( "B", 190.0, 350.0, 0.5 - 0.3 ) ):
        land, rebound, y = r[ball]
        drop = rest - y0
        t = math.sqrt( 2.0 * drop / GRAVITY ) / DT
        if not t - 1.5 <= land <= t + 2.5:
            failures.append( f"{name}: ball{ball} landed at step {land}, free fall at 980 px/s^2 says {t:.1f}" )
        ratio = rebound / drop
        if not 0.8 * e * e <= ratio <= 1.1 * e * e:
            failures.append( f"{name}: ball{ball} rebounded {ratio:.4f} of its drop, restitution {e} gives {e * e:.4f}" )
        if abs( y - rest ) > 0.1:
            failures.append( f"{name}: ball{ball} not at rest on its surface (y {y})" )
    ( a_in, a_out ), ( b_in, b_out ) = r["contacts"]
    if a_in < 2 or a_in != a_out + 1:
        failures.append( f"{name}: BallA body_entered {a_in}, body_exited {a_out}: bounces then rests expected" )
    if ( b_in, b_out ) != ( 0, 0 ):
        failures.append( f"{name}: BallB does not report contacts, but got {b_in} / {b_out}" )
    if r["zone"] != ["BallB"] or r["zone_counts"] != ( "1", "1" ):
        failures.append( f"{name}: Zone saw {r['zone']} counts {r['zone_counts']}, expected BallB once, 1 / 1" )
    if r["coin"] != ["BallA"]:
        failures.append( f"{name}: the Coin was taken {r['coin']}; once, by BallA, expected" )
    crates = r["crates"]
    if abs( crates[0] - 360.0 ) > 1.0 or any( not 38.0 <= a - b <= 42.0 for a, b in zip( crates, crates[1:] ) ):
        failures.append( f"{name}: crate stack collapsed {crates}" )
    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument( "--crust", default=os.environ.get( "CRUST_ROOT" ) )
    parser.add_argument( "--build-dir", default="/tmp/box2d_godot_tests" )
    args = parser.parse_args()
    if not args.crust:
        print( "SKIP: pass --crust PATH or set CRUST_ROOT to a crust checkout" )
        return 0
    godot_pack = os.path.join( args.crust, "tools", "godot_pack.py" )

    results = {}
    failures = []
    for name, extra in ( ( "box2d", [] ), ( "inject", ["--physics-inject"] ) ):
        results[name] = pack_and_run( godot_pack, os.path.join( args.build_dir, name ), extra )
        print( f"--- {name}\n{results[name]}" )
        failures += check( name, parse( results[name] ) )
    if results["inject"] != results["box2d"]:
        failures.append( "injected and standard Box2D builds differ" )

    for f in failures:
        print( "FAIL:", f )
    print( "PASS" if not failures else "FAILED" )
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit( main() )
