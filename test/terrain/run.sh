#!/bin/sh
# Builds Box2D-Packed's sources and the glue box2d_unity.py emits for a plan with one terrain chunk and
# one dynamic circle, and runs two programs against it, under ASan / UBSan:
#   main.c         boxes: rests on terrain, an unchanged rebuild keeps the contact, a dug hole lets the
#                  circle fall, boxes past the cap are cut.
#   main_chains.c  chains: loops and open chains, an unchanged chain keeps its contact, a gap, boxes
#                  and chains replacing each other, no bump over a seam, chains past the cap are cut.
# Run from anywhere.
set -e
here=$(cd "$(dirname "$0")" && pwd); root="$here/../.."
out=$(mktemp -d)
for t in main main_chains; do
python3 - "$root" "$out" "$t" <<'PY'
import importlib.util, sys
spec = importlib.util.spec_from_file_location("m", sys.argv[1] + "/box2d_unity.py")
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
plan = {"collider2d": [{"kind": 5}, {"kind": 1}], "rigidbody2d": [{}],
        "physics2d_terrain": True, "terrain2d_max_shapes": 64}
if sys.argv[3] == "main_chains":
    plan.update({"terrain2d_chains": True, "terrain2d_max_chains": 64, "terrain2d_max_points": 256})
m.emit_glue(sys.argv[2], plan)
PY
gcc -O1 -g -fsanitize=address,undefined -I"$root/include" -I"$root/src" -I"$root/extern" \
    "$here/$t.c" "$out/physics_box2d.c" "$root"/src/*.c -lm -Wl,--wrap=b2CreateChain,--wrap=b2CreatePolygonShape -o "$out/$t"
"$out/$t"
done
