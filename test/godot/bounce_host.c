/* Host for the Godot Bounce fixture: 180 fixed steps of 1/60 s (Godot's 60 physics ticks).
   Prints the step each ball first touches down, its first rebound's peak height, the Drifter's
   damped speed, and the crate heights. Godot's y is down. The scripts print their signal counts. */
#include <stdio.h>
void engine_tick( void );
extern float Time_deltaTime;
extern int _Rigidbody2D_count;
extern float _Rigidbody2D_vel_x[];
extern float _Ball_pos[][2]; /* BallA, BallB: one class, Ball.cs */
extern float _Crate_pos[][2];

typedef struct
{
	float rest, last, peak;
	int land, rising, done;
} Track;

static void track( Track* t, float y, int step )
{
	/* first touch down: y stops increasing near the rest height */
	if ( t->land < 0 && y >= t->rest - 1.0f )
		t->land = step;
	if ( t->land >= 0 && !t->done )
	{
		if ( y < t->last )
			t->rising = 1;
		if ( t->rising && y < t->peak )
			t->peak = y;
		if ( t->rising && y > t->last )
			t->done = 1;
	}
	t->last = y;
}

int main( void )
{
	Track a = { 370.0f, 200.0f, 1e9f, -1, 0, 0 };
	Track b = { 350.0f, 190.0f, 1e9f, -1, 0, 0 };
	Time_deltaTime = 1.0f / 60.0f;
	for ( int i = 1; i <= 180; ++i )
	{
		engine_tick();
		track( &a, _Ball_pos[0][1], i );
		track( &b, _Ball_pos[1][1], i );
		if ( i == 60 )
			printf( "drifter vx %.3f\n", _Rigidbody2D_vel_x[_Rigidbody2D_count - 1] );
	}
	printf( "ballA land %d rebound %.2f rest %.2f\n", a.land, a.rest - a.peak, _Ball_pos[0][1] );
	printf( "ballB land %d rebound %.2f rest %.2f\n", b.land, b.rest - b.peak, _Ball_pos[1][1] );
	printf( "crates y:" );
	for ( int k = 0; k < 6; ++k )
		printf( " %.2f", _Crate_pos[k][1] );
	printf( "\n" );
	return 0;
}
