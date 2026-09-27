#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
# SPDX-License-Identifier: MIT
"""
box2d_pack: build Box2D-Packed with user code injected into the engine.

The engine sources carry inert marker comments such as

    //$b2Collide$CONTACT_BEGIN

box2d_pack copies the sources to a build directory, replaces markers in the copies with
user C code described by a JSON file, and compiles everything with gcc. User logic then
runs inside the engine loop, where the relevant data is already in cache, instead of going
through event arrays and id lookups after the step.

Command line:

    python3 box2d_pack.py                              # engine only -> /tmp/libbox2d.a
    python3 box2d_pack.py usercode.c                   # engine + main() -> /tmp/box2d
    python3 box2d_pack.py usercode.c userinject.json   # same, with injected code
    python3 box2d_pack.py --list-markers               # show injection points

Python:

    import box2d                                        # the repo directory is a package
    result = box2d.build("usercode.c", "userinject.json")
    print(result.exe)

Injection JSON:

    {
      "defines": { "B2_PACK_NO_CONTACT_BEGIN_ARRAY": 1 },
      "cflags":  [ "-march=native" ],
      "includes": [ "game.h" ],
      "sources":  [ "game_logic.c" ],
      "globals": "void Game_OnBegin( void* a, void* b );",
      "inject": [
        { "event": "contact_begin",
          "code": "Game_OnBegin( shapeA->userData, shapeB->userData );" },
        { "marker": "b2World_Step$FOOTER", "code_file": "post_step.c" },
        { "function": "b2World_Step", "point": "HEADER", "code": [ "line 1", "line 2" ] }
      ]
    }

Each injection names its target with "marker", "event", or "function" + "point". Code comes
from "code" (a string, or a list of lines) or "code_file" (relative to the JSON file).
Function-body markers get the code wrapped in braces. GLOBALS markers get it verbatim at
file scope, which is where to declare user functions and externs.

"includes" lists game headers, relative to the JSON file. They are included at the globals marker,
so injected code can use the game's types and static inline functions directly, with no call and
no need for --lto. "sources" lists extra game C files to compile and link with usercode.c.

Both the engine and the user code are compiled with B2_PACK_INJECTED=1 when an injection
file is given, so user code can tell which mode it was built in.
"""

import argparse
import concurrent.futures
import dataclasses
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

__all__ = [
    "BuildError",
    "BuildResult",
    "EVENTS",
    "apply_injections",
    "build",
    "find_markers",
    "load_injections",
    "main",
]

REPO_ROOT = os.path.dirname( os.path.abspath( __file__ ) )

# Friendly names for common markers
EVENTS = {
    "globals": "pack_hooks$GLOBALS",
    "pre_step": "b2World_Step$HEADER",
    "post_step": "b2World_Step$FOOTER",
    "contact_begin": "b2Collide$CONTACT_BEGIN",
    "contact_end": "b2PackContactEnd$CONTACT_END",
    "contact_hit": "b2Solve$CONTACT_HIT",
    "sensor_begin": "b2PackSensorBegin$SENSOR_BEGIN",
    "sensor_end": "b2PackSensorEnd$SENSOR_END",
    "custom_filter": "b2PackCustomFilter$FILTER",
    "pre_solve": "b2UpdateContact$PRE_SOLVE",
    "body_gravity": "b2PackBodyGravity$BODY_GRAVITY",
}

# //$scope$POINT on a line of its own
MARKER_RE = re.compile( r"^(?P<indent>[ \t]*)//\$(?P<scope>[A-Za-z_][A-Za-z0-9_]*)\$(?P<point>[A-Z][A-Z0-9_]*)\s*$" )


class BuildError( Exception ):
    """Raised for bad injection files, unknown markers, and compiler failures."""


@dataclasses.dataclass
class Marker:
    name: str  # "scope$POINT"
    path: str  # path relative to the source root
    line: int  # 1-based line number
    indent: str
    doc: str  # comment lines directly above the marker

    @property
    def file_scope( self ):
        return self.name.endswith( "$GLOBALS" )

    @property
    def worker_threads( self ):
        return "worker threads" in self.doc.lower()


@dataclasses.dataclass
class Injection:
    marker: str
    code: str
    origin: str  # where the code came from, used for #line
    origin_line: int = 1


@dataclasses.dataclass
class InjectionSpec:
    injections: list
    defines: dict
    cflags: list
    path: str = None
    sources: list = dataclasses.field( default_factory=list )
    include_dirs: list = dataclasses.field( default_factory=list )


@dataclasses.dataclass
class BuildResult:
    lib: str
    exe: str = None
    source_dir: str = None
    objects: list = dataclasses.field( default_factory=list )
    compiled: int = 0
    reused: int = 0


def _engine_sources( repo_root ):
    src = os.path.join( repo_root, "src" )
    inc = os.path.join( repo_root, "include", "box2d" )
    if not os.path.isdir( src ) or not os.path.isdir( inc ):
        raise BuildError( f"not a Box2D-Packed source tree: {repo_root}" )
    files = []
    for name in sorted( os.listdir( src ) ):
        if name.endswith( ( ".c", ".h", ".inl" ) ):
            files.append( os.path.join( "src", name ) )
    for name in sorted( os.listdir( inc ) ):
        if name.endswith( ".h" ):
            files.append( os.path.join( "include", "box2d", name ) )
    return files


def find_markers( repo_root=REPO_ROOT ):
    """Return {marker name: Marker} for every injection point in the engine sources."""
    markers = {}
    for rel in _engine_sources( repo_root ):
        with open( os.path.join( repo_root, rel ), encoding="utf-8" ) as f:
            lines = f.read().split( "\n" )
        for i, text in enumerate( lines ):
            m = MARKER_RE.match( text )
            if m is None:
                continue
            name = f"{m.group( 'scope' )}${m.group( 'point' )}"
            if name in markers:
                other = markers[name]
                raise BuildError( f"duplicate marker {name} in {rel}:{i + 1} and {other.path}:{other.line}" )
            doc = []
            j = i - 1
            while j >= 0 and lines[j].strip().startswith( "//" ) and not MARKER_RE.match( lines[j] ):
                doc.insert( 0, lines[j].strip()[2:].strip() )
                j -= 1
            markers[name] = Marker( name, rel, i + 1, m.group( "indent" ), " ".join( doc ) )
    return markers


def _code_text( entry, base_dir, where ):
    if "code" in entry and "code_file" in entry:
        raise BuildError( f"{where}: use either 'code' or 'code_file', not both" )
    if "code_file" in entry:
        path = os.path.join( base_dir, entry["code_file"] )
        try:
            with open( path, encoding="utf-8" ) as f:
                return f.read(), os.path.abspath( path )
        except OSError as e:
            raise BuildError( f"{where}: cannot read code_file: {e}" )
    if "code" in entry:
        code = entry["code"]
        if isinstance( code, list ):
            if not all( isinstance( line, str ) for line in code ):
                raise BuildError( f"{where}: 'code' list must contain strings" )
            code = "\n".join( code )
        if not isinstance( code, str ):
            raise BuildError( f"{where}: 'code' must be a string or a list of strings" )
        return code, None
    raise BuildError( f"{where}: missing 'code' or 'code_file'" )


def _target_marker( entry, where ):
    keys = [k for k in ( "marker", "event", "function" ) if k in entry]
    if len( keys ) != 1:
        raise BuildError( f"{where}: give exactly one of 'marker', 'event', or 'function' + 'point'" )
    if "marker" in entry:
        return entry["marker"]
    if "event" in entry:
        event = entry["event"]
        if event not in EVENTS:
            raise BuildError( f"{where}: unknown event '{event}', known events: {', '.join( sorted( EVENTS ) )}" )
        return EVENTS[event]
    if "point" not in entry:
        raise BuildError( f"{where}: 'function' needs a 'point', such as HEADER or FOOTER" )
    return f"{entry['function']}${entry['point']}"


def load_injections( path ):
    """Parse an injection JSON file into an InjectionSpec. Markers are checked at apply time."""
    try:
        with open( path, encoding="utf-8" ) as f:
            data = json.load( f )
    except OSError as e:
        raise BuildError( f"cannot read {path}: {e}" )
    except json.JSONDecodeError as e:
        raise BuildError( f"{path}: invalid JSON: {e}" )
    if not isinstance( data, dict ):
        raise BuildError( f"{path}: top level must be an object" )

    known = { "inject", "globals", "includes", "sources", "defines", "cflags", "comment" }
    unknown = set( data ) - known
    if unknown:
        raise BuildError( f"{path}: unknown keys {sorted( unknown )}, expected some of {sorted( known )}" )

    base_dir = os.path.dirname( os.path.abspath( path ) )
    name = os.path.basename( path )
    injections = []

    def path_list( key ):
        items = data.get( key, [] )
        if not isinstance( items, list ) or not all( isinstance( i, str ) for i in items ):
            raise BuildError( f"{path}: '{key}' must be a list of file paths" )
        resolved = []
        for item in items:
            full = os.path.abspath( os.path.join( base_dir, item ) )
            if not os.path.isfile( full ):
                raise BuildError( f"{path}: {key} file not found: {item}" )
            resolved.append( full )
        return resolved

    # Game headers go first in the globals marker, so every injection can use the game's types
    # and static inline functions
    includes = path_list( "includes" )
    if includes:
        code = "\n".join( f'#include "{inc}"' for inc in includes )
        injections.append( Injection( EVENTS["globals"], code, f"{name}:includes" ) )
    sources = path_list( "sources" )

    if "globals" in data:
        code, origin = _code_text( { "code": data["globals"] }, base_dir, f"{name}: globals" )
        injections.append( Injection( EVENTS["globals"], code, origin or f"{name}:globals" ) )

    entries = data.get( "inject", [] )
    if not isinstance( entries, list ):
        raise BuildError( f"{path}: 'inject' must be a list" )
    for index, entry in enumerate( entries ):
        where = f"{name}: inject[{index}]"
        if not isinstance( entry, dict ):
            raise BuildError( f"{where}: must be an object" )
        marker = _target_marker( entry, where )
        code, origin = _code_text( entry, base_dir, where )
        injections.append( Injection( marker, code, origin or f"{name}:inject[{index}]" ) )

    defines = data.get( "defines", {} )
    if not isinstance( defines, dict ):
        raise BuildError( f"{path}: 'defines' must be an object" )
    cflags = data.get( "cflags", [] )
    if not isinstance( cflags, list ) or not all( isinstance( c, str ) for c in cflags ):
        raise BuildError( f"{path}: 'cflags' must be a list of strings" )

    return InjectionSpec( injections, defines, cflags, os.path.abspath( path ), sources, [base_dir] )


def apply_injections( files, markers, injections ):
    """
    Patch {relative path: text} in place. Every injection must name a known marker.
    Returns the number of markers that received code.
    """
    by_marker = {}
    for inj in injections:
        if inj.marker not in markers:
            close = [m for m in markers if m.split( "$" )[0] == inj.marker.split( "$" )[0]]
            hint = f" Markers in that scope: {', '.join( sorted( close ) )}." if close else ""
            raise BuildError(
                f"{inj.origin}: unknown marker '{inj.marker}'.{hint} Run box2d_pack.py --list-markers to see all." )
        by_marker.setdefault( inj.marker, [] ).append( inj )

    for name in sorted( by_marker ):
        if markers[name].worker_threads:
            print( f"box2d_pack: note: {name} runs on worker threads, injected code must be thread-safe",
                   file=sys.stderr )

    by_file = {}
    for name, injs in by_marker.items():
        by_file.setdefault( markers[name].path, [] ).append( ( markers[name], injs ) )

    for rel, items in by_file.items():
        lines = files[rel].split( "\n" )
        # Bottom up so earlier line numbers stay valid
        for marker, injs in sorted( items, key=lambda item: -item[0].line ):
            out = [f"{marker.indent}/* box2d_pack: begin {marker.name} */"]
            for inj in injs:
                # The brace comes before #line so compiler errors in the user code report the
                # right line of the snippet
                if not marker.file_scope:
                    out.append( f"{marker.indent}{{" )
                out.append( f'#line {inj.origin_line} "{inj.origin}"' )
                out.extend( inj.code.split( "\n" ) )
                if not marker.file_scope:
                    out.append( f"{marker.indent}}}" )
            # Restore line numbers for the rest of the engine file
            out.append( f'#line {marker.line + 1} "{rel}"' )
            lines[marker.line - 1: marker.line] = out
        files[rel] = "\n".join( lines )
    return len( by_marker )


def _write_if_changed( path, text ):
    try:
        with open( path, encoding="utf-8" ) as f:
            if f.read() == text:
                return False
    except OSError:
        pass
    os.makedirs( os.path.dirname( path ), exist_ok=True )
    with open( path, "w", encoding="utf-8" ) as f:
        f.write( text )
    return True


def _run( cmd, verbose ):
    if verbose:
        print( " ".join( cmd ) )
    proc = subprocess.run( cmd, capture_output=True, text=True )
    if proc.returncode != 0:
        raise BuildError( f"command failed ({proc.returncode}): {' '.join( cmd )}\n{proc.stderr}{proc.stdout}" )
    if proc.stderr and verbose:
        sys.stderr.write( proc.stderr )
    return proc


def build(
    usercode=None,
    inject=None,
    *,
    build_dir="/tmp",
    out=None,
    lib=None,
    cc="gcc",
    opt="-O3",
    debug=False,
    lto=False,
    defines=None,
    cflags=None,
    ldflags=None,
    jobs=None,
    repo_root=REPO_ROOT,
    verbose=False,
):
    """
    Copy the engine sources to build_dir/box2d_src, apply injections, compile each engine
    source to build_dir/b2_<name>.o, archive them into lib (default build_dir/libbox2d.a),
    and when usercode is given link it into out (default build_dir/box2d).

    inject may be a path to a JSON file or an InjectionSpec. Returns a BuildResult.
    """
    spec = None
    if inject is not None:
        spec = inject if isinstance( inject, InjectionSpec ) else load_injections( inject )

    build_dir = os.path.abspath( build_dir )
    source_dir = os.path.join( build_dir, "box2d_src" )
    lib = os.path.abspath( lib ) if lib else os.path.join( build_dir, "libbox2d.a" )
    os.makedirs( build_dir, exist_ok=True )

    # Read and patch the sources in memory, then write only what changed
    markers = find_markers( repo_root )
    rel_files = _engine_sources( repo_root )
    files = {}
    for rel in rel_files:
        with open( os.path.join( repo_root, rel ), encoding="utf-8" ) as f:
            files[rel] = f.read()
    if spec is not None:
        apply_injections( files, markers, spec.injections )

    # Remove stale copies of files that no longer exist in the repo
    if os.path.isdir( source_dir ):
        wanted = { os.path.normpath( os.path.join( source_dir, rel ) ) for rel in rel_files }
        for root, _, names in os.walk( source_dir ):
            for name in names:
                path = os.path.normpath( os.path.join( root, name ) )
                if path not in wanted:
                    os.remove( path )

    for rel in rel_files:
        _write_if_changed( os.path.join( source_dir, rel ), files[rel] )

    # Compiler flags shared by the engine and the user code
    all_defines = {}
    if not debug:
        all_defines["NDEBUG"] = None
    if spec is not None:
        all_defines["B2_PACK_INJECTED"] = 1
        all_defines.update( spec.defines )
    all_defines.update( defines or {} )

    common = ["-std=c17", "-g0" if not debug else "-g", opt if not debug else "-O0"]
    common += ["-I", os.path.join( source_dir, "include" )]
    if lto:
        common.append( "-flto" )
    for key, value in all_defines.items():
        common.append( f"-D{key}" if value is None else f"-D{key}={value}" )
    common += ( spec.cflags if spec else [] ) + list( cflags or [] )

    engine_flags = common + ["-I", os.path.join( source_dir, "src" ), "-Wall", "-Wno-unused-value"]
    signature = hashlib.sha1( ( cc + "\0" + "\0".join( engine_flags ) ).encode() ).hexdigest()

    c_files = [rel for rel in rel_files if rel.startswith( "src" ) and rel.endswith( ".c" )]
    header_hash = hashlib.sha1(
        "".join( files[rel] for rel in rel_files if not rel.endswith( ".c" ) ).encode() ).hexdigest()

    def compile_one( rel ):
        stem = os.path.splitext( os.path.basename( rel ) )[0]
        obj = os.path.join( build_dir, f"b2_{stem}.o" )
        stamp = obj + ".sig"
        key = hashlib.sha1( ( signature + header_hash + files[rel] ).encode() ).hexdigest()
        if os.path.exists( obj ) and os.path.exists( stamp ):
            with open( stamp ) as f:
                if f.read() == key:
                    return obj, False
        _run( [cc] + engine_flags + ["-c", os.path.join( source_dir, rel ), "-o", obj], verbose )
        with open( stamp, "w" ) as f:
            f.write( key )
        return obj, True

    result = BuildResult( lib=lib, source_dir=source_dir )
    with concurrent.futures.ThreadPoolExecutor( max_workers=jobs or os.cpu_count() or 1 ) as pool:
        for obj, compiled in pool.map( compile_one, c_files ):
            result.objects.append( obj )
            if compiled:
                result.compiled += 1
            else:
                result.reused += 1

    ar = "gcc-ar" if lto and shutil.which( "gcc-ar" ) else "ar"
    if os.path.exists( lib ):
        os.remove( lib )
    _run( [ar, "rcs", lib] + result.objects, verbose )

    if usercode is not None:
        exe = os.path.abspath( out ) if out else os.path.join( build_dir, "box2d" )
        user_flags = common + ["-Wall"]
        extra_sources = []
        if spec is not None:
            for d in spec.include_dirs:
                user_flags += ["-I", d]
            extra_sources = spec.sources
        link = [cc] + user_flags + [os.path.abspath( usercode )] + extra_sources + [lib, "-o", exe, "-lm", "-lpthread"]
        _run( link + list( ldflags or [] ), verbose )
        result.exe = exe

    return result


def _print_markers( markers ):
    aliases = { v: k for k, v in EVENTS.items() }
    for name in sorted( markers, key=lambda n: ( markers[n].path, markers[n].line ) ):
        m = markers[name]
        alias = f"  (event: {aliases[name]})" if name in aliases else ""
        threads = "  [WORKER THREADS]" if m.worker_threads else ""
        print( f"{name}{alias}{threads}\n    {m.path}:{m.line}" )
        if m.doc:
            print( f"    {m.doc}" )


def main( argv=None ):
    parser = argparse.ArgumentParser(
        prog="box2d_pack.py",
        description="Build Box2D-Packed, optionally with user code injected into the engine.",
    )
    parser.add_argument( "usercode", nargs="?", help="C file that defines main(), linked into the executable" )
    parser.add_argument( "inject", nargs="?", help="injection JSON file" )
    parser.add_argument( "--build-dir", default="/tmp", help="where copies, objects, and outputs go (default /tmp)" )
    parser.add_argument( "-o", "--out", help="executable path (default BUILD_DIR/box2d)" )
    parser.add_argument( "--lib", help="static library path (default BUILD_DIR/libbox2d.a)" )
    parser.add_argument( "--cc", default="gcc" )
    parser.add_argument( "--opt", default="-O3", help="optimization flag (default -O3)" )
    parser.add_argument( "--debug", action="store_true", help="-O0 -g with Box2D asserts" )
    parser.add_argument( "--lto", action="store_true", help="link time optimization across engine and user code" )
    parser.add_argument( "-D", dest="defines", action="append", default=[], metavar="NAME[=VALUE]" )
    parser.add_argument( "-j", "--jobs", type=int )
    parser.add_argument( "--list-markers", action="store_true", help="list injection points and exit" )
    parser.add_argument( "--run", action="store_true", help="run the executable after building" )
    parser.add_argument( "-v", "--verbose", action="store_true" )
    args = parser.parse_args( argv )

    try:
        if args.list_markers:
            _print_markers( find_markers() )
            return 0

        if args.inject and not args.usercode:
            parser.error( "an injection file needs a usercode file" )

        defines = {}
        for d in args.defines:
            key, _, value = d.partition( "=" )
            defines[key] = value if value else None

        result = build(
            args.usercode,
            args.inject,
            build_dir=args.build_dir,
            out=args.out,
            lib=args.lib,
            cc=args.cc,
            opt=args.opt,
            debug=args.debug,
            lto=args.lto,
            defines=defines,
            jobs=args.jobs,
            verbose=args.verbose,
        )
    except BuildError as e:
        print( f"box2d_pack: error: {e}", file=sys.stderr )
        return 1

    print( f"box2d_pack: {result.compiled} compiled, {result.reused} up to date -> {result.lib}" )
    if result.exe:
        print( f"box2d_pack: executable -> {result.exe}" )
        if args.run:
            return subprocess.call( [result.exe] )
    return 0


if __name__ == "__main__":
    sys.exit( main() )
