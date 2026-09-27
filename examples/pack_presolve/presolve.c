// SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
// SPDX-License-Identifier: MIT

// Platformer crowd: a pre-solve benchmark for box2d_pack.
//
// 12,000 circles fall through a stack of one-way platforms. Every contact has pre-solve enabled,
// and the handler reads both shapes' user data: ghosts pass through everything, and platforms let
// bodies pass up through them.
//
// Stress configuration: sleep and contact recycling are off. Box2D skips pre-solve for recycled
// contacts, so with recycling on, a settled crowd would barely call it. Circles keep the narrow
// phase cheap, so the per-call cost of the callback is a meaningful share.
//
// Standard build: b2World_SetPreSolveCallback, looking up user data by shape id.
//     python3 box2d_pack.py examples/pack_presolve/presolve.c
// Injected build: the handler runs at the pre-solve marker, reading userData directly.
//     python3 box2d_pack.py examples/pack_presolve/presolve.c examples/pack_presolve/inject.json
// Add --lto to either build to let gcc inline across the engine and the game.
//
// All builds print the same checksum.

// clock_gettime under -std=c17
#define _POSIX_C_SOURCE 199309L

#include "box2d/box2d.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef B2_PACK_INJECTED
#define B2_PACK_INJECTED 0
#endif

// Override with -D STEP_COUNT=N, for example to shorten Cachegrind runs
#ifndef STEP_COUNT
#define STEP_COUNT 300
#endif

enum
{
	BODY_COUNT = 12000,
	PLATFORM_COUNT = 8,
};

typedef enum Kind
{
	KIND_BODY,
	KIND_PLATFORM,
	KIND_WALL,
} Kind;

typedef struct Thing
{
	Kind kind;
	int ghost;
	int team;
	char name[20];
	float stats[8];
} Thing;

static Thing* g_things;
static Thing g_platform = { .kind = KIND_PLATFORM };
static Thing g_wall = { .kind = KIND_WALL };
static b2BodyId* g_bodies;
static uint32_t g_seed = 4242u;

static float Rand( float lo, float hi )
{
	g_seed = 1664525u * g_seed + 1013904223u;
	return lo + ( hi - lo ) * (float)( g_seed >> 8 ) * ( 1.0f / 16777216.0f );
}

// Pre-solve logic. Runs on worker threads: it reads user data and writes only this contact's
// manifold, so it is thread-safe.
void Game_PreSolve( const void* userDataA, const void* userDataB, b2Manifold* manifold );

void Game_PreSolve( const void* userDataA, const void* userDataB, b2Manifold* manifold )
{
	const Thing* a = userDataA;
	const Thing* b = userDataB;

	// Ghosts pass through other bodies and platforms, but not walls
	if ( ( a->ghost && b->kind != KIND_WALL ) || ( b->ghost && a->kind != KIND_WALL ) )
	{
		manifold->pointCount = 0;
		return;
	}

	// One-way platforms: the normal points from A to B, bodies pass up through the platform
	if ( a->kind == KIND_PLATFORM && manifold->normal.y < 0.5f )
	{
		manifold->pointCount = 0;
	}
	else if ( b->kind == KIND_PLATFORM && manifold->normal.y > -0.5f )
	{
		manifold->pointCount = 0;
	}
}

#if B2_PACK_INJECTED == 0
static void PreSolveCallback( b2ShapeId shapeIdA, b2ShapeId shapeIdB, b2Manifold* manifold, void* context )
{
	(void)context;
	Game_PreSolve( b2Shape_GetUserData( shapeIdA ), b2Shape_GetUserData( shapeIdB ), manifold );
}
#endif

static double NowMs( void )
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

int main( void )
{
	g_things = calloc( BODY_COUNT, sizeof( Thing ) );
	g_bodies = calloc( BODY_COUNT, sizeof( b2BodyId ) );

	b2WorldDef worldDef = b2DefaultWorldDef();
	worldDef.enableSleep = false;
	b2WorldId worldId = b2CreateWorld( &worldDef );

#if B2_PACK_INJECTED == 0
	b2World_SetPreSolveCallback( worldId, PreSolveCallback, NULL, NULL );
#endif

	b2BodyDef bodyDef = b2DefaultBodyDef();
	b2BodyId groundId = b2CreateBody( worldId, &bodyDef );

	b2ShapeDef wallDef = b2DefaultShapeDef();
	wallDef.userData = &g_wall;
	wallDef.enablePreSolveEvents = true;
	b2Polygon walls[3] = {
		b2MakeOffsetBox( 42.0f, 1.0f, (b2Vec2){ 0.0f, -1.0f }, b2Rot_identity ),
		b2MakeOffsetBox( 1.0f, 80.0f, (b2Vec2){ -42.0f, 80.0f }, b2Rot_identity ),
		b2MakeOffsetBox( 1.0f, 80.0f, (b2Vec2){ 42.0f, 80.0f }, b2Rot_identity ),
	};
	for ( int i = 0; i < 3; ++i )
	{
		b2CreatePolygonShape( groundId, &wallDef, walls + i );
	}

	// Staggered one-way platforms with gaps, so the crowd spreads over all of them
	b2ShapeDef platformDef = wallDef;
	platformDef.userData = &g_platform;
	for ( int i = 0; i < PLATFORM_COUNT; ++i )
	{
		float y = 6.0f + 7.0f * i;
		float x = ( i & 1 ) ? 8.0f : -8.0f;
		b2Polygon platform = b2MakeOffsetBox( 30.0f, 0.3f, (b2Vec2){ x, y }, b2Rot_identity );
		b2CreatePolygonShape( groundId, &platformDef, &platform );
	}

	// Crowd, assigned to shuffled game objects
	int* order = malloc( BODY_COUNT * sizeof( int ) );
	for ( int i = 0; i < BODY_COUNT; ++i )
	{
		order[i] = i;
	}
	for ( int i = BODY_COUNT - 1; i > 0; --i )
	{
		int j = (int)Rand( 0.0f, (float)( i + 1 ) );
		j = j > i ? i : j;
		int t = order[i];
		order[i] = order[j];
		order[j] = t;
	}

	bodyDef.type = b2_dynamicBody;
	bodyDef.enableContactRecycling = false;
	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.enablePreSolveEvents = true;
	b2Circle circle = { { 0.0f, 0.0f }, 0.3f };

	for ( int i = 0; i < BODY_COUNT; ++i )
	{
		Thing* thing = g_things + order[i];
		thing->kind = KIND_BODY;
		thing->ghost = ( i % 10 ) == 0;
		thing->team = i & 3;
		bodyDef.position = (b2Pos){ Rand( -40.0f, 40.0f ), Rand( 2.0f, 150.0f ) };
		bodyDef.linearVelocity = (b2Vec2){ Rand( -3.0f, 3.0f ), Rand( -2.0f, 8.0f ) };
		g_bodies[order[i]] = b2CreateBody( worldId, &bodyDef );
		shapeDef.userData = thing;
		b2CreateCircleShape( g_bodies[order[i]], &shapeDef, &circle );
	}
	free( order );

	double stepMs = 0.0;
	for ( int step = 0; step < STEP_COUNT; ++step )
	{
		double t0 = NowMs();
		b2World_Step( worldId, 1.0f / 60.0f, 4 );
		stepMs += NowMs() - t0;
	}

	b2Counters counters = b2World_GetCounters( worldId );

	uint64_t h = 1469598103934665603ull;
	for ( int i = 0; i < BODY_COUNT; ++i )
	{
		b2Pos p = b2Body_GetPosition( g_bodies[i] );
		h = ( h ^ (uint64_t)(uint32_t)(int32_t)( p.x * 10000.0f ) ) * 1099511628211ull;
		h = ( h ^ (uint64_t)(uint32_t)(int32_t)( p.y * 10000.0f ) ) * 1099511628211ull;
	}

	printf( "mode: %s\n", B2_PACK_INJECTED ? "injected" : "standard callback" );
	printf( "contacts at end: %d\n", counters.contactCount );
	printf( "checksum: 0x%016llx\n", (unsigned long long)h );
	printf( "time: %.1f ms for %d steps\n", stepMs, STEP_COUNT );

	b2DestroyWorld( worldId );
	free( g_things );
	free( g_bodies );
	return 0;
}
