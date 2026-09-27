#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
# SPDX-License-Identifier: MIT
"""
Integration test: Box2D-Packed as the physics backend of crust's tools/unity_pack.py.

Packs test/unity/Bounce three ways (built-in physics, --physics box2d, and --physics box2d
--physics-inject), links bounce_host.c against each, and checks:
  1. the Ball script's OnCollisionEnter2D / Exit2D counts follow Unity semantics (4 and 3) with
     Box2D, as with the built-in physics
  2. the injected build prints exactly what the standard Box2D build prints
  3. the ball comes to rest on the ground and the 6-crate stack stands

Usage: python3 test/unity/run_unity_tests.py --crust PATH   (or set CRUST_ROOT)
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname( os.path.abspath( __file__ ) )
BOX2D_ROOT = os.path.dirname( os.path.dirname( HERE ) )


def pack_and_run( unity_pack, out, extra ):
    cmd = [sys.executable, unity_pack, os.path.join( HERE, "Bounce" ), "-o", out, "--force",
           "--box2d", BOX2D_ROOT] + extra
    r = subprocess.run( cmd, capture_output=True, text=True )
    if r.returncode != 0:
        raise SystemExit( "unity_pack failed:\n" + r.stderr[-3000:] )
    objs = [os.path.join( out, "engine.o" ), os.path.join( out, "data.o" )]
    libs = []
    if os.path.isfile( os.path.join( out, "physics_box2d.o" ) ):
        objs.append( os.path.join( out, "physics_box2d.o" ) )
        libs = [os.path.join( out, "box2d", "libbox2d.a" ), "-lpthread"]
    exe = os.path.join( out, "bounce_host" )
    subprocess.run( ["gcc", "-O2", "-o", exe, os.path.join( HERE, "bounce_host.c" )] + objs + libs + ["-lm"],
                    check=True )
    return subprocess.run( [exe], capture_output=True, text=True, check=True ).stdout.strip()


def parse( line ):
    m = re.match( r"ball y (\S+) enters (\d+) stays (\d+) exits (\d+) \| crates y: (.*)", line )
    ys = [float( v ) for v in m.group( 5 ).split()]
    return float( m.group( 1 ) ), int( m.group( 2 ) ), int( m.group( 3 ) ), int( m.group( 4 ) ), ys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument( "--crust", default=os.environ.get( "CRUST_ROOT" ) )
    parser.add_argument( "--build-dir", default="/tmp/box2d_unity_tests" )
    args = parser.parse_args()
    if not args.crust:
        print( "SKIP: pass --crust PATH or set CRUST_ROOT to a crust checkout" )
        return 0
    unity_pack = os.path.join( args.crust, "tools", "unity_pack.py" )

    results = {}
    for name, extra in ( ( "builtin", ["--physics", "builtin"] ), ( "box2d", ["--physics", "box2d"] ),
                         ( "inject", ["--physics", "box2d", "--physics-inject"] ) ):
        results[name] = pack_and_run( unity_pack, os.path.join( args.build_dir, name ), extra )
        print( f"{name:8s} {results[name]}" )

    failures = []
    for name in ( "builtin", "box2d", "inject" ):
        ball_y, enters, stays, exits, crates = parse( results[name] )
        if ( enters, exits ) != ( 4, 3 ):
            failures.append( f"{name}: expected 4 enters and 3 exits, got {enters} and {exits}" )
        if abs( ball_y + 1.5 ) > 0.02:
            failures.append( f"{name}: ball not at rest on the ground (y {ball_y})" )
        if any( b - a < 0.9 for a, b in zip( crates, crates[1:] ) ) or crates[-1] < 3.4:
            failures.append( f"{name}: crate stack collapsed {crates}" )
    if results["inject"] != results["box2d"]:
        failures.append( "injected and standard Box2D builds differ" )

    for f in failures:
        print( "FAIL:", f )
    print( "PASS" if not failures else "FAILED" )
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit( main() )
