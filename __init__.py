# SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
# SPDX-License-Identifier: MIT
"""
Box2D-Packed as a Python package. From the directory that contains the repo:

    import box2d
    result = box2d.build( "usercode.c", "userinject.json" )
    print( result.exe )

See box2d_pack.py for the injection format.
"""

from .box2d_pack import (
    EVENTS,
    BuildError,
    BuildResult,
    apply_injections,
    build,
    find_markers,
    load_injections,
    main,
)

__all__ = [
    "EVENTS",
    "BuildError",
    "BuildResult",
    "apply_injections",
    "build",
    "find_markers",
    "load_injections",
    "main",
]
