#include <stdio.h>

#include "box2d/box2d.h"
int made_chains, made_boxes;
b2ChainId __real_b2CreateChain(b2BodyId, const b2ChainDef*);
b2ChainId __wrap_b2CreateChain(b2BodyId b, const b2ChainDef* d) { made_chains++; return __real_b2CreateChain(b, d); }
b2ShapeId __real_b2CreatePolygonShape(b2BodyId, const b2ShapeDef*, const b2Polygon*);
b2ShapeId __wrap_b2CreatePolygonShape(b2BodyId b, const b2ShapeDef* d, const b2Polygon* p) { made_boxes++; return __real_b2CreatePolygonShape(b, d, p); }
#include <string.h>
float Physics2D_gravity_x = 0, Physics2D_gravity_y = -9.81f, Time_fixedDeltaTime = 1.0f/60.0f;
const int _Collider2D_count = 2;
const int _Collider2D_kind[] = {5, 1};
const int _Collider2D_is_trigger[] = {0, 0};
const int _Collider2D_rb2d[] = {-1, 0};
const float _Collider2D_ox[] = {0, 0}, _Collider2D_oy[] = {0, 0};
const float _Collider2D_hw[] = {0.5f, 0.5f}, _Collider2D_hh[] = {0.5f, 0.5f};
const float _Collider2D_cos[] = {1, 1}, _Collider2D_sin[] = {0, 0};
const float _Collider2D_friction[] = {0.4f, 0.4f}, _Collider2D_bounciness[] = {0, 0};
const int _Collider2D_friction_combine[] = {0, 0}, _Collider2D_bounce_combine[] = {0, 0}, _Collider2D_layer[] = {0, 0};
const int _Rigidbody2D_count = 1;
const int _Rigidbody2D_body_type[] = {0};
const float _Rigidbody2D_gravity_scale[] = {1}, _Rigidbody2D_linear_damping[] = {0}, _Rigidbody2D_mass[] = {1};
float _Rigidbody2D_vel_x[] = {0}, _Rigidbody2D_vel_y[] = {0};
static float bx = 0, by = 3;
float bvx_init = 0;
void engine_rb2d_get_pos(int rb, float* x, float* y) { *x = bx; *y = by; }
void engine_rb2d_set_pos(int rb, float x, float y) { bx = x; by = y; }
void engine_col2d_center(int ci, float* x, float* y) { *x = 0; *y = 0; }
static int contacts;
void engine_col2d_contact(int a, int b) { contacts++; }
void engine_box2d_step(void);
void b2u_terrain_set(int ci, const float* boxes, int n);

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static void run(int steps) { for (int i = 0; i < steps; i++) engine_box2d_step(); }
static void put(int ci, const float* pts, const int* st, const int* ct, const int* lp, int n) { b2u_terrain_set_chains(ci, pts, st, ct, lp, n); }

int main(void) {
  /* a slab of ground, x -5..5, y -1..0, as a loop: ground on the left of the way */
  const float slab[] = { -5,-1, 5,-1, 5,0, -5,0 };
  const int st1[] = {0}, ct4[] = {4}, lp1[] = {1}, lp0[] = {0};
  put(0, slab, st1, ct4, lp1, 1);
  run(180);
  CHECK(by > 0.45f && by < 0.56f, "ball rests on a loop chain: y=%.3f", by);
  CHECK(contacts > 100, "chain contacts are reported (events work through a chain): %d", contacts);
  printf("loop chain: y=%.3f, touching reports=%d\n", by, contacts);

  /* the same chain again: it keeps its shapes, so the contact never ends */
  contacts = 0;
  int before = made_chains;
  put(0, slab, st1, ct4, lp1, 1);
  run(30);
  CHECK(made_chains == before, "an unchanged chain is not made again: %d new", made_chains - before);
  CHECK(contacts == 30, "an unchanged chain keeps its contact: %d of 30", contacts);
  /* one of two chains changes: only that one is made */
  const float twoA[] = { -5,-1, 5,-1, 5,0, -5,0,   20,-1, 25,-1, 25,0, 20,0 };
  const float twoB[] = { -5,-1, 5,-1, 5,0, -5,0,   20,-1, 26,-1, 26,0, 20,0 };
  const int stA[] = {0, 4}, ctA[] = {4, 4}, lpA[] = {1, 1};
  put(0, twoA, stA, ctA, lpA, 2);
  before = made_chains;
  put(0, twoB, stA, ctA, lpA, 2);
  CHECK(made_chains == before + 1, "of two chains one changed, %d made", made_chains - before);
  put(0, slab, st1, ct4, lp1, 1);

  /* the ground split in two with a wide gap under the ball: two loops, the ball falls */
  const float two[] = { -5,-1, -1.5f,-1, -1.5f,0, -5,0,   1.5f,-1, 5,-1, 5,0, 1.5f,0 };
  const int st2[] = {0, 4}, ct44[] = {4, 4}, lp11[] = {1, 1};
  put(0, two, st2, ct44, lp11, 2);
  run(120);
  CHECK(by < -5.0f, "the ball falls through the gap: y=%.3f", by);
  printf("gap: y=%.3f\n", by);

  /* boxes replace chains, and chains replace boxes */
  float box[] = { 0, -0.5f, 5, 0.5f };
  bx = 0; by = 3; run(1);
  b2u_terrain_set(0, box, 1);
  by = 3; run(150);
  CHECK(by > 0.45f && by < 0.56f, "boxes after chains: y=%.3f", by);
  put(0, slab, st1, ct4, lp1, 1);
  by = 3; run(150);
  CHECK(by > 0.45f && by < 0.56f, "chains after boxes (the boxes are gone, not doubled): y=%.3f", by);

  /* an open chain: along -x the air is on the right, which is up */
  const float open[] = { 5,0, -5,0 };
  const int ct2[] = {2};
  put(0, open, st1, ct2, lp0, 1);
  by = 3; run(150);
  CHECK(by > 0.45f && by < 0.56f, "ball rests on an open chain: y=%.3f", by);
  printf("open chain: y=%.3f\n", by);

  /* two open chains meeting at x = 0, a ball rolling over the seam keeps its height and its speed */
  const float seam[] = { 5,0, 0,0,   0,0, -5,0 };
  const int st3[] = {0, 2}, ct22[] = {2, 2}, lp00[] = {0, 0};
  put(0, seam, st3, ct22, lp00, 2);
  bx = 4.8f; by = 0.5f; _Rigidbody2D_vel_x[0] = -9; _Rigidbody2D_vel_y[0] = 0;
  float miny = 9, maxy = -9, maxjump = 0, prev = -9, crossed = 0;
  for (int i = 0; i < 70; i++) {
    engine_box2d_step();
    if (bx < 3.0f && bx > -3.0f) {
      if (by < miny) miny = by;
      if (by > maxy) maxy = by;
      float jump = _Rigidbody2D_vel_x[0] - prev; if (jump < 0) jump = -jump;
      if (prev > -9 && jump > maxjump) maxjump = jump;
      if (bx < 0) crossed = 1;
    }
    prev = _Rigidbody2D_vel_x[0];
  }
  printf("seam: y %.4f..%.4f, largest speed change in a step %.3f, crossed=%d\n", miny, maxy, maxjump, (int)crossed);
  CHECK(crossed == 1, "the ball rolled over the seam (x=%.2f)", bx);
  CHECK(maxy - miny < 0.02f, "no bump at the seam: height varies %.4f", maxy - miny);
  CHECK(maxjump < 0.2f, "no catch at the seam: speed changed %.3f in one step", maxjump);

  /* more than the cap are cut, not overrun */
  static float many[2 * 5000]; static int ms[1000], mc[1000], ml[1000];
  for (int i = 0; i < 5000; i++) { many[2*i] = (float)i; many[2*i+1] = (float)(i & 1); }
  for (int k = 0; k < 1000; k++) { ms[k] = 5*k; mc[k] = 5; ml[k] = 0; }
  put(0, many, ms, mc, ml, 1000);
  put(0, many, ms, mc, ml, 0);
  puts(fails ? "chains FAILED" : "chains ok");
  return fails ? 1 : 0;
}
