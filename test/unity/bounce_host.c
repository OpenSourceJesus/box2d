/* Host for the Bounce fixture: 300 fixed steps, then print Ball messages and crate heights. */
#include <stdio.h>
void engine_tick(void);
extern float Time_deltaTime;
typedef struct { int enters; int stays; int exits; } Ball;
extern Ball _Ball_inst_array[];
extern float _Ball_pos[][2];
extern float _Crate_pos[][2];
int main(void) {
  Time_deltaTime = 0.02f;
  for (int i = 0; i < 300; ++i) engine_tick();
  printf("ball y %.3f enters %d stays %d exits %d | crates y:", _Ball_pos[0][1], _Ball_inst_array[0].enters, _Ball_inst_array[0].stays, _Ball_inst_array[0].exits);
  for (int k = 0; k < 6; ++k) printf(" %.3f", _Crate_pos[k][1]);
  printf("\n");
  return 0;
}
