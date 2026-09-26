#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
# SPDX-License-Identifier: MIT
"""
Equivalence tests for box2d_pack injection markers.

Builds test/pack/markers.c three ways and checks:
  1. standard (callbacks + event arrays) and injected (markers.json) print identical output
  2. a control build without the filter and pre-solve rules prints a different state hash,
     so the test is sensitive to those rules

Usage: python3 test/pack/run_pack_tests.py [--build-dir DIR]
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname( os.path.abspath( __file__ ) )
sys.path.insert( 0, os.path.dirname( os.path.dirname( HERE ) ) )
import box2d_pack  # noqa: E402


def run( exe ):
    return subprocess.run( [exe], capture_output=True, text=True, check=True ).stdout


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument( "--build-dir", default="/tmp/box2d_pack_tests" )
    args = parser.parse_args()

    source = os.path.join( HERE, "markers.c" )
    inject = os.path.join( HERE, "markers.json" )
    out = lambda name: os.path.join( args.build_dir, name )

    standard = box2d_pack.build( source, build_dir=out( "standard" ), out=out( "standard/markers" ) )
    injected = box2d_pack.build( source, inject, build_dir=out( "injected" ), out=out( "injected/markers" ) )
    control = box2d_pack.build( source, build_dir=out( "standard" ), out=out( "standard/control" ),
                                defines={ "TEST_DISABLE_RULES": None } )

    a, b, c = run( standard.exe ), run( injected.exe ), run( control.exe )
    print( "standard:\n" + a + "injected:\n" + b + "control (no filter, no pre-solve):\n" + c )

    ok = True
    if a != b:
        print( "FAIL: standard and injected builds differ" )
        ok = False
    if a.splitlines()[-1] == c.splitlines()[-1]:
        print( "FAIL: control matches, the test does not detect the filter and pre-solve rules" )
        ok = False
    print( "PASS" if ok else "FAILED" )
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit( main() )
