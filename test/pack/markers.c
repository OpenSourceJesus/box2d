// SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
// SPDX-License-Identifier: MIT

// Equivalence test for box2d_pack injection markers. One scene exercises sensor begin/end,
// contact hit events, custom pair filtering, and pre-solve. The standard build uses the Box2D
// callbacks and event arrays. The injected build runs the same logic at the markers, with the
// callbacks and arrays compiled out. Both must print identical output. See run_pack_tests.py.

#include "box2d/box2d.h"

#include <stdint.h>
#include <stdio.h>

#ifndef B2_PACK_INJECTED
#define B2_PACK_INJECTED 0
#endif

#include "markers_game.h"

enum
{
	BALL_COUNT = 400,
	PICKUP_COUNT = 40,
	STEP_COUNT = 240,
};

static Thing g_balls[BALL_COUNT];
static Thing g_pickups[PICKUP_COUNT];
static Thing g_platform = { .kind = KIND_PLATFORM };
static Thing g_ground = { .kind = KIND_GROUND };
static b2BodyId g_ballBodies[BALL_COUNT];

// Shared game logic. Called by callbacks and event loops in the standard build, and from the
// injected markers in the injected build.

void Test_SensorBegin( void* sensorData, void* visitorData );
void Test_SensorEnd( void* sensorData, void* visitorData );
void Test_Hit( void* dataA, void* dataB, float approachSpeed );
void Test_ContactBegin( void* dataA, void* dataB );
void Test_ContactEnd( void* dataA, void* dataB );
int Test_Filter( void* dataA, void* dataB );
int Test_PreSolve( void* dataA, void* dataB, b2Manifold* manifold );

void Test_SensorBegin( void* sensorData, void* visitorData )
{
	Thing* pickup = sensorData;
	Thing* visitor = visitorData;
	pickup->sensorBegins += 1;
	if ( visitor != NULL )
	{
		visitor->sensorBegins += 1;
	}
}

void Test_SensorEnd( void* sensorData, void* visitorData )
{
	Thing* pickup = sensorData;
	Thing* visitor = visitorData;
	pickup->sensorEnds += 1;
	if ( visitor != NULL )
	{
		visitor->sensorEnds += 1;
	}
}

void Test_Hit( void* dataA, void* dataB, float approachSpeed )
{
	Thing* a = dataA;
	Thing* b = dataB;
	a->hits += 1;
	b->hits += 1;
	a->hitSpeed += approachSpeed;
	b->hitSpeed += approachSpeed;
}

// Contact begin and end. End events come from two engine paths: shapes that stop touching, and
// contacts destroyed when bounding boxes separate. Bouncing balls produce both.
void Test_ContactBegin( void* dataA, void* dataB )
{
	( (Thing*)dataA )->contactBegins += 1;
	( (Thing*)dataB )->contactBegins += 1;
}

void Test_ContactEnd( void* dataA, void* dataB )
{
	( (Thing*)dataA )->contactEnds += 1;
	( (Thing*)dataB )->contactEnds += 1;
}

// Balls of team 0 and team 2 pass through each other. Pure function of its inputs, thread-safe.
int Test_Filter( void* dataA, void* dataB )
{
	const Thing* a = dataA;
	const Thing* b = dataB;
	if ( a->kind == KIND_BALL && b->kind == KIND_BALL && a->team + b->team == 2 && a->team != b->team )
	{
		return 0;
	}
	return 1;
}

// One-way platform: balls pass up through it. Only touches the manifold, thread-safe.
int Test_PreSolve( void* dataA, void* dataB, b2Manifold* manifold )
{
	const Thing* a = dataA;
	const Thing* b = dataB;
	float sign = 0.0f;
	if ( a->kind == KIND_PLATFORM && b->kind == KIND_BALL )
	{
		sign = 1.0f;
	}
	else if ( b->kind == KIND_PLATFORM && a->kind == KIND_BALL )
	{
		sign = -1.0f;
	}
	if ( sign != 0.0f && sign * manifold->normal.y < 0.5f )
	{
		manifold->pointCount = 0;
		return 0;
	}
	return 1;
}

#if B2_PACK_INJECTED == 0 && !defined( TEST_DISABLE_RULES )
static bool FilterCallback( b2ShapeId a, b2ShapeId b, void* context )
{
	(void)context;
	bool result = Test_Filter( b2Shape_GetUserData( a ), b2Shape_GetUserData( b ) ) != 0;
	return result;
}

static void PreSolveCallback( b2ShapeId a, b2ShapeId b, b2Manifold* manifold, void* context )
{
	(void)context;
	Test_PreSolve( b2Shape_GetUserData( a ), b2Shape_GetUserData( b ), manifold );
}
#endif

static uint32_t g_seed = 7u;
static float Rand( float lo, float hi )
{
	g_seed = 1664525u * g_seed + 1013904223u;
	return lo + ( hi - lo ) * (float)( g_seed >> 8 ) * ( 1.0f / 16777216.0f );
}

int main( void )
{
	b2WorldDef worldDef = b2DefaultWorldDef();
	worldDef.hitEventThreshold = 1.0f;
	b2WorldId worldId = b2CreateWorld( &worldDef );

#if B2_PACK_INJECTED == 0 && !defined( TEST_DISABLE_RULES )
	// TEST_DISABLE_RULES builds a control without the filter and one-way platform. Its hash must
	// differ, which shows the test is sensitive to those rules.
	b2World_SetCustomFilterCallback( worldId, FilterCallback, NULL );
	b2World_SetPreSolveCallback( worldId, PreSolveCallback, NULL, NULL );
#endif

	b2BodyDef bodyDef = b2DefaultBodyDef();
	b2BodyId groundId = b2CreateBody( worldId, &bodyDef );
	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.userData = &g_ground;
	shapeDef.enableHitEvents = true;
	b2Polygon box = b2MakeOffsetBox( 40.0f, 1.0f, (b2Vec2){ 0.0f, -1.0f }, b2Rot_identity );
	b2CreatePolygonShape( groundId, &shapeDef, &box );
	box = b2MakeOffsetBox( 1.0f, 30.0f, (b2Vec2){ -40.0f, 30.0f }, b2Rot_identity );
	b2CreatePolygonShape( groundId, &shapeDef, &box );
	box = b2MakeOffsetBox( 1.0f, 30.0f, (b2Vec2){ 40.0f, 30.0f }, b2Rot_identity );
	b2CreatePolygonShape( groundId, &shapeDef, &box );

	// One-way platform
	shapeDef.userData = &g_platform;
	shapeDef.enablePreSolveEvents = true;
	box = b2MakeOffsetBox( 30.0f, 0.25f, (b2Vec2){ 0.0f, 12.0f }, b2Rot_identity );
	b2CreatePolygonShape( groundId, &shapeDef, &box );

	// Pickups are static sensors
	b2ShapeDef sensorDef = b2DefaultShapeDef();
	sensorDef.isSensor = true;
	sensorDef.enableSensorEvents = true;
	for ( int i = 0; i < PICKUP_COUNT; ++i )
	{
		g_pickups[i].kind = KIND_PICKUP;
		sensorDef.userData = g_pickups + i;
		b2Circle c = { { -36.0f + 1.8f * i, 4.0f + 3.0f * ( i % 5 ) }, 0.8f };
		b2CreateCircleShape( groundId, &sensorDef, &c );
	}

	// Balls on three teams, with every event kind enabled
	bodyDef.type = b2_dynamicBody;
	b2ShapeDef ballDef = b2DefaultShapeDef();
	ballDef.enableSensorEvents = true;
	ballDef.enableHitEvents = true;
	ballDef.enableContactEvents = true;
	ballDef.enableCustomFiltering = true;
	ballDef.enablePreSolveEvents = true;
	ballDef.material.restitution = 0.4f;
	b2Circle circle = { { 0.0f, 0.0f }, 0.35f };
	for ( int i = 0; i < BALL_COUNT; ++i )
	{
		g_balls[i].kind = KIND_BALL;
		g_balls[i].team = i % 3;
		bodyDef.position = (b2Pos){ Rand( -35.0f, 35.0f ), Rand( 2.0f, 40.0f ) };
		bodyDef.linearVelocity = (b2Vec2){ Rand( -5.0f, 5.0f ), Rand( -5.0f, 12.0f ) };
		bodyDef.userData = g_balls + i;
#if B2_PACK_INJECTED == 0
		bodyDef.gravityScale = Test_GravityScale( g_balls + i );
#endif
		g_ballBodies[i] = b2CreateBody( worldId, &bodyDef );
		ballDef.userData = g_balls + i;
		b2CreateCircleShape( g_ballBodies[i], &ballDef, &circle );
	}

	for ( int step = 0; step < STEP_COUNT; ++step )
	{
		b2World_Step( worldId, 1.0f / 60.0f, 4 );

#if B2_PACK_INJECTED == 0
		b2SensorEvents se = b2World_GetSensorEvents( worldId );
		for ( int i = 0; i < se.beginCount; ++i )
		{
			Test_SensorBegin( b2Shape_GetUserData( se.beginEvents[i].sensorShapeId ),
							  b2Shape_GetUserData( se.beginEvents[i].visitorShapeId ) );
		}
		for ( int i = 0; i < se.endCount; ++i )
		{
			b2ShapeId visitor = se.endEvents[i].visitorShapeId;
			Test_SensorEnd( b2Shape_GetUserData( se.endEvents[i].sensorShapeId ),
							b2Shape_IsValid( visitor ) ? b2Shape_GetUserData( visitor ) : NULL );
		}

		b2ContactEvents ce = b2World_GetContactEvents( worldId );
		for ( int i = 0; i < ce.beginCount; ++i )
		{
			Test_ContactBegin( b2Shape_GetUserData( ce.beginEvents[i].shapeIdA ),
							   b2Shape_GetUserData( ce.beginEvents[i].shapeIdB ) );
		}
		for ( int i = 0; i < ce.endCount; ++i )
		{
			Test_ContactEnd( b2Shape_GetUserData( ce.endEvents[i].shapeIdA ),
							 b2Shape_GetUserData( ce.endEvents[i].shapeIdB ) );
		}
		for ( int i = 0; i < ce.hitCount; ++i )
		{
			b2ContactHitEvent* e = ce.hitEvents + i;
			Test_Hit( b2Shape_GetUserData( e->shapeIdA ), b2Shape_GetUserData( e->shapeIdB ), e->approachSpeed );
		}
#endif
	}

	int sensorBegins = 0, sensorEnds = 0, hits = 0, contactBegins = 0, contactEnds = 0;
	double hitSpeed = 0.0;
	uint64_t h = 1469598103934665603ull;
	for ( int i = 0; i < BALL_COUNT; ++i )
	{
		sensorBegins += g_balls[i].sensorBegins;
		sensorEnds += g_balls[i].sensorEnds;
		hits += g_balls[i].hits;
		contactBegins += g_balls[i].contactBegins;
		contactEnds += g_balls[i].contactEnds;
		hitSpeed += g_balls[i].hitSpeed;

		// Positions show that filtering and pre-solve changed the simulation identically
		b2Pos p = b2Body_GetPosition( g_ballBodies[i] );
		int32_t v[2] = { (int32_t)( p.x * 10000.0f ), (int32_t)( p.y * 10000.0f ) };
		h = ( h ^ (uint32_t)v[0] ) * 1099511628211ull;
		h = ( h ^ (uint32_t)v[1] ) * 1099511628211ull;
	}
	for ( int i = 0; i < PICKUP_COUNT; ++i )
	{
		h = ( h ^ (uint64_t)g_pickups[i].sensorBegins ) * 1099511628211ull;
		h = ( h ^ (uint64_t)g_pickups[i].sensorEnds ) * 1099511628211ull;
	}
	h = ( h ^ (uint64_t)g_platform.hits ) * 1099511628211ull;
	h = ( h ^ (uint64_t)g_ground.hits ) * 1099511628211ull;

	printf( "sensor begins %d, sensor ends %d, hits %d, hit speed %.4f, contact begins %d, contact ends %d\n",
			sensorBegins, sensorEnds, hits, hitSpeed, contactBegins, contactEnds );
	printf( "state hash 0x%016llx\n", (unsigned long long)h );

	b2DestroyWorld( worldId );
	return 0;
}
