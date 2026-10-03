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
void engine_rb2d_get_pos(int rb, float* x, float* y) { *x = bx; *y = by; }
void engine_rb2d_set_pos(int rb, float x, float y) { bx = x; by = y; }
void engine_col2d_center(int ci, float* x, float* y) { *x = 0; *y = 0; }
static int contacts;
void engine_col2d_contact(int a, int b) { contacts++; }
void engine_box2d_step(void);
void b2u_terrain_set(int ci, const float* boxes, int n);
int main(void) {
  float floor_[10 * 4]; /* ten 1x1 boxes, x = -5..4, top at y = 0 */
  for (int i = 0; i < 10; i++) { floor_[4*i] = -4.5f + i; floor_[4*i+1] = -0.5f; floor_[4*i+2] = 0.5f; floor_[4*i+3] = 0.5f; }
  b2u_terrain_set(0, floor_, 10);
  for (int i = 0; i < 180; i++) engine_box2d_step();
  printf("rests on terrain: y=%.3f (expect ~0.5), touching reports=%d\n", by, contacts);
  /* rebuild with the same boxes: nothing may change */
  contacts = 0;
  int boxes_before = made_boxes;
  b2u_terrain_set(0, floor_, 10);
  if (made_boxes != boxes_before) { printf("FAIL: an unchanged chunk made %d new boxes\n", made_boxes - boxes_before); return 1; }
  for (int i = 0; i < 30; i++) engine_box2d_step();
  printf("same rebuild: y=%.3f, still touching every step=%d (expect 30)\n", by, contacts);
  /* dig a hole under the ball: drop boxes 4 and 5 (x -0.5..0.5 and 0.5..1.5) */
  float hole[8 * 4]; int n = 0;
  for (int i = 0; i < 10; i++) { if (i == 4 || i == 5) continue; memcpy(hole + 4*n, floor_ + 4*i, 16); n++; }
  b2u_terrain_set(0, hole, n);
  for (int i = 0; i < 120; i++) engine_box2d_step();
  printf("after digging: y=%.3f (expect well below -1)\n", by);
  /* more boxes than the cap are cut, not overrun */
  static float many[300 * 4]; for (int i = 0; i < 300; i++) { many[4*i] = 20 + i; many[4*i+1] = 0; many[4*i+2] = .5f; many[4*i+3] = .5f; }
  b2u_terrain_set(0, many, 300); b2u_terrain_set(0, many, 0);
  puts("cap ok");
  return 0;
}
