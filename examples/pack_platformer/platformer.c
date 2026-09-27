// SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
// SPDX-License-Identifier: MIT

// Platformer: 1,500 AI runners race across a level of one-way platforms, collecting coins and
// feathers (low gravity), stomping patrolling enemies, and taking hard landings.
//
// The game rules live in platformer.h. The standard build runs them from a pre-solve callback and
// the sensor and contact event arrays. The injected build runs the same rules inline inside the
// engine. Both builds apply feather gravity with b2Body_SetGravityScale.
//
//     python3 box2d_pack.py examples/pack_platformer/platformer.c
//     python3 box2d_pack.py examples/pack_platformer/platformer.c examples/pack_platformer/inject.json
//
// Both builds print the same checksum. Add -D STEP_COUNT=N to change the run length.

// clock_gettime under -std=c17
#define _POSIX_C_SOURCE 199309L

#include "platformer.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef B2_PACK_INJECTED
#define B2_PACK_INJECTED 0
#endif

#ifndef STEP_COUNT
#define STEP_COUNT 600
#endif

enum
{
	RUNNER_COUNT = 1500,
	ENEMY_COUNT = 200,
	PLATFORM_ROWS = 6,
	PLATFORMS_PER_ROW = 12,
	CATEGORY_RUNNER = 0x0001,
	CATEGORY_OTHER = 0x0002,
};

static const float LEVEL_HALF_WIDTH = 100.0f;

static Runner* g_runners;
static Enemy* g_enemies;
static Coin* g_coins;
static int g_coinCount;
static b2BodyId* g_runnerBodies;
static b2BodyId* g_enemyBodies;
static Tile g_solid = { KIND_SOLID };
static Tile g_platform = { KIND_PLATFORM };
static uint32_t g_seed = 2026u;

static float Rand( float lo, float hi )
{
	g_seed = 1664525u * g_seed + 1013904223u;
	return lo + ( hi - lo ) * (float)( g_seed >> 8 ) * ( 1.0f / 16777216.0f );
}

#if B2_PACK_INJECTED == 0
static void PreSolveCallback( b2ShapeId shapeIdA, b2ShapeId shapeIdB, b2Manifold* manifold, void* context )
{
	(void)context;
	Game_PreSolve( b2Shape_GetUserData( shapeIdA ), b2Shape_GetUserData( shapeIdB ), manifold );
}

// Standard API: walk the event arrays after the step
static void ProcessEvents( b2WorldId worldId )
{
	b2SensorEvents sensorEvents = b2World_GetSensorEvents( worldId );
	for ( int i = 0; i < sensorEvents.beginCount; ++i )
	{
		b2SensorBeginTouchEvent* e = sensorEvents.beginEvents + i;
		Game_OnSensorBegin( b2Shape_GetUserData( e->sensorShapeId ), b2Shape_GetUserData( e->visitorShapeId ) );
	}

	b2ContactEvents contactEvents = b2World_GetContactEvents( worldId );
	for ( int i = 0; i < contactEvents.beginCount; ++i )
	{
		b2ContactBeginTouchEvent* e = contactEvents.beginEvents + i;
		b2ContactData data = b2Contact_GetData( e->contactId );
		Game_OnContactBegin( b2Shape_GetUserData( e->shapeIdA ), b2Shape_GetUserData( e->shapeIdB ),
							 data.manifold.normal );
	}
	for ( int i = 0; i < contactEvents.hitCount; ++i )
	{
		b2ContactHitEvent* e = contactEvents.hitEvents + i;
		Game_OnHit( b2Shape_GetUserData( e->shapeIdA ), b2Shape_GetUserData( e->shapeIdB ), e->approachSpeed );
	}
}

#endif

// Feather gravity through b2Body_SetGravityScale, effective next step. The engine stores the scale in
// its hot body data. This is the recommended path in both builds. inject_gravity_hook.json replaces
// it with the body_gravity marker instead, which measured slower, see the README.
#ifndef GAME_INJECT_GRAVITY
static void SyncGravity( void )
{
	for ( int i = 0; i < RUNNER_COUNT; ++i )
	{
		Runner* runner = g_runners + i;
		float scale = Game_GravityScale( runner );
		if ( scale != runner->appliedGravityScale )
		{
			b2Body_SetGravityScale( g_runnerBodies[i], scale );
			runner->appliedGravityScale = scale;
		}
	}
}
#endif

static void BuildLevel( b2WorldId worldId )
{
	b2BodyDef bodyDef = b2DefaultBodyDef();
	b2BodyId levelId = b2CreateBody( worldId, &bodyDef );

	b2ShapeDef solidDef = b2DefaultShapeDef();
	solidDef.userData = &g_solid;
	solidDef.enableHitEvents = true;
	solidDef.filter.categoryBits = CATEGORY_OTHER;
	b2Polygon ground = b2MakeOffsetBox( LEVEL_HALF_WIDTH + 2.0f, 1.0f, (b2Vec2){ 0.0f, -1.0f }, b2Rot_identity );
	b2CreatePolygonShape( levelId, &solidDef, &ground );
	b2Polygon left = b2MakeOffsetBox( 1.0f, 40.0f, (b2Vec2){ -LEVEL_HALF_WIDTH - 1.0f, 40.0f }, b2Rot_identity );
	b2Polygon right = b2MakeOffsetBox( 1.0f, 40.0f, (b2Vec2){ LEVEL_HALF_WIDTH + 1.0f, 40.0f }, b2Rot_identity );
	b2CreatePolygonShape( levelId, &solidDef, &left );
	b2CreatePolygonShape( levelId, &solidDef, &right );

	b2ShapeDef platformDef = solidDef;
	platformDef.userData = &g_platform;
	platformDef.enablePreSolveEvents = true;

	b2ShapeDef coinDef = b2DefaultShapeDef();
	coinDef.isSensor = true;
	coinDef.enableSensorEvents = true;
	coinDef.filter.categoryBits = CATEGORY_OTHER;

	g_coins = calloc( PLATFORM_ROWS * PLATFORMS_PER_ROW * 3, sizeof( Coin ) );
	float spacing = 2.0f * LEVEL_HALF_WIDTH / PLATFORMS_PER_ROW;
	for ( int row = 0; row < PLATFORM_ROWS; ++row )
	{
		float y = 4.0f + 4.5f * row;
		for ( int k = 0; k < PLATFORMS_PER_ROW; ++k )
		{
			float x = -LEVEL_HALF_WIDTH + spacing * ( k + 0.5f ) + ( ( row & 1 ) ? 0.5f * spacing : 0.0f );
			if ( x > LEVEL_HALF_WIDTH - 5.0f )
			{
				x -= 2.0f * LEVEL_HALF_WIDTH - spacing;
			}
			b2Polygon platform = b2MakeOffsetBox( 5.0f, 0.25f, (b2Vec2){ x, y }, b2Rot_identity );
			b2CreatePolygonShape( levelId, &platformDef, &platform );

			for ( int c = 0; c < 3; ++c )
			{
				Coin* coin = g_coins + g_coinCount++;
				coin->kind = KIND_COIN;
				coin->isFeather = ( g_coinCount % 9 ) == 0;
				coinDef.userData = coin;
				b2Circle circle = { { x - 3.0f + 3.0f * c, y + 1.2f }, 0.5f };
				b2CreateCircleShape( levelId, &coinDef, &circle );
			}
		}
	}
}

static void SpawnActors( b2WorldId worldId )
{
	g_runners = calloc( RUNNER_COUNT, sizeof( Runner ) );
	g_runnerBodies = calloc( RUNNER_COUNT, sizeof( b2BodyId ) );
	g_enemies = calloc( ENEMY_COUNT, sizeof( Enemy ) );
	g_enemyBodies = calloc( ENEMY_COUNT, sizeof( b2BodyId ) );

	b2BodyDef bodyDef = b2DefaultBodyDef();
	bodyDef.type = b2_dynamicBody;
	bodyDef.enableSleep = false;
	bodyDef.motionLocks.angularZ = true;

	// Runners pass through each other, like players in a race
	b2ShapeDef runnerDef = b2DefaultShapeDef();
	runnerDef.enableSensorEvents = true;
	runnerDef.enablePreSolveEvents = true;
	runnerDef.enableHitEvents = true;
	runnerDef.material.friction = 0.2f;
	runnerDef.filter.categoryBits = CATEGORY_RUNNER;
	runnerDef.filter.maskBits = CATEGORY_OTHER;
	b2Capsule capsule = { { 0.0f, -0.3f }, { 0.0f, 0.3f }, 0.3f };

	for ( int i = 0; i < RUNNER_COUNT; ++i )
	{
		Runner* runner = g_runners + i;
		runner->kind = KIND_RUNNER;
		runner->index = i;
		runner->direction = ( i & 1 ) ? 1 : -1;
		runner->appliedGravityScale = 1.0f;
		snprintf( runner->name, sizeof( runner->name ), "runner_%d", i );
		bodyDef.position = (b2Pos){ Rand( -95.0f, 95.0f ), Rand( 1.0f, 30.0f ) };
		bodyDef.userData = runner;
		g_runnerBodies[i] = b2CreateBody( worldId, &bodyDef );
		runnerDef.userData = runner;
		b2CreateCapsuleShape( g_runnerBodies[i], &runnerDef, &capsule );
	}

	b2ShapeDef enemyDef = b2DefaultShapeDef();
	enemyDef.enablePreSolveEvents = true;
	enemyDef.enableContactEvents = true;
	enemyDef.filter.categoryBits = CATEGORY_OTHER;
	b2Polygon box = b2MakeBox( 0.5f, 0.5f );

	for ( int i = 0; i < ENEMY_COUNT; ++i )
	{
		Enemy* enemy = g_enemies + i;
		enemy->kind = KIND_ENEMY;
		enemy->direction = ( i & 1 ) ? 1 : -1;
		float x = Rand( -90.0f, 90.0f );
		enemy->minX = x - 8.0f;
		enemy->maxX = x + 8.0f;
		bodyDef.position = (b2Pos){ x, Rand( 1.0f, 28.0f ) };
		bodyDef.userData = enemy;
		g_enemyBodies[i] = b2CreateBody( worldId, &bodyDef );
		enemyDef.userData = enemy;
		b2CreatePolygonShape( g_enemyBodies[i], &enemyDef, &box );
	}
}

// Game update after the step, the same in both builds
static void UpdateGame( void )
{
	for ( int i = 0; i < g_coinCount; ++i )
	{
		if ( g_coins[i].cooldown > 0 )
		{
			g_coins[i].cooldown -= 1;
		}
	}

	for ( int i = 0; i < RUNNER_COUNT; ++i )
	{
		Runner* runner = g_runners + i;
		b2BodyId bodyId = g_runnerBodies[i];
		b2Pos p = b2Body_GetPosition( bodyId );
		b2Vec2 v = b2Body_GetLinearVelocity( bodyId );

		if ( ( p.x > LEVEL_HALF_WIDTH - 2.0f && runner->direction > 0 ) ||
			 ( p.x < -LEVEL_HALF_WIDTH + 2.0f && runner->direction < 0 ) )
		{
			runner->direction = -runner->direction;
		}

		v.x = 6.0f * (float)runner->direction;
		if ( runner->bounceRequest )
		{
			v.y = 9.0f;
			runner->bounceRequest = 0;
		}
		else if ( runner->jumpCooldown == 0 && v.y > -0.05f && v.y < 0.05f )
		{
			v.y = 11.0f;
			runner->jumpCooldown = 40 + ( i % 23 );
		}
		b2Body_SetLinearVelocity( bodyId, v );

		if ( runner->jumpCooldown > 0 )
		{
			runner->jumpCooldown -= 1;
		}
		if ( runner->lowGravityTimer > 0 )
		{
			runner->lowGravityTimer -= 1;
		}
	}

	for ( int i = 0; i < ENEMY_COUNT; ++i )
	{
		Enemy* enemy = g_enemies + i;
		b2BodyId bodyId = g_enemyBodies[i];
		b2Pos p = b2Body_GetPosition( bodyId );
		if ( ( p.x > enemy->maxX && enemy->direction > 0 ) || ( p.x < enemy->minX && enemy->direction < 0 ) )
		{
			enemy->direction = -enemy->direction;
		}
		b2Vec2 v = b2Body_GetLinearVelocity( bodyId );
		v.x = 2.5f * (float)enemy->direction;
		b2Body_SetLinearVelocity( bodyId, v );
	}
}

static double NowMs( void )
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

int main( void )
{
	b2WorldDef worldDef = b2DefaultWorldDef();
	worldDef.hitEventThreshold = 4.0f;
	b2WorldId worldId = b2CreateWorld( &worldDef );

#if B2_PACK_INJECTED == 0
	b2World_SetPreSolveCallback( worldId, PreSolveCallback, NULL, NULL );
#endif

	BuildLevel( worldId );
	SpawnActors( worldId );

	double stepMs = 0.0, gameMs = 0.0;
	for ( int step = 0; step < STEP_COUNT; ++step )
	{
		double t0 = NowMs();
		b2World_Step( worldId, 1.0f / 60.0f, 4 );
		double t1 = NowMs();

#if B2_PACK_INJECTED == 0
		ProcessEvents( worldId );
#endif
		UpdateGame();
#ifndef GAME_INJECT_GRAVITY
		SyncGravity();
#endif
		gameMs += NowMs() - t1;
		stepMs += t1 - t0;
	}

	long long coins = 0, feathers = 0, stomps = 0, hurts = 0, landings = 0, score = 0;
	uint64_t h = 1469598103934665603ull;
	for ( int i = 0; i < RUNNER_COUNT; ++i )
	{
		Runner* r = g_runners + i;
		coins += r->coins;
		feathers += r->feathers;
		stomps += r->stomps;
		hurts += r->hurts;
		landings += r->hardLandings;
		score += r->score;
		b2Pos p = b2Body_GetPosition( g_runnerBodies[i] );
		int32_t v[4] = { r->score, r->hardLandings, (int32_t)( p.x * 1000.0f ), (int32_t)( p.y * 1000.0f ) };
		for ( int k = 0; k < 4; ++k )
		{
			h = ( h ^ (uint64_t)(uint32_t)v[k] ) * 1099511628211ull;
		}
	}

	printf( "mode: %s\n", B2_PACK_INJECTED ? "injected" : "standard callbacks and events" );
	printf( "coins %lld, feathers %lld, stomps %lld, hurts %lld, hard landings %lld, score %lld\n", coins, feathers,
			stomps, hurts, landings, score );
	printf( "checksum: 0x%016llx\n", (unsigned long long)h );
	printf( "time: step %.1f ms + game %.1f ms = %.1f ms\n", stepMs, gameMs, stepMs + gameMs );

	b2DestroyWorld( worldId );
	free( g_runners );
	free( g_runnerBodies );
	free( g_enemies );
	free( g_enemyBodies );
	free( g_coins );
	return 0;
}
