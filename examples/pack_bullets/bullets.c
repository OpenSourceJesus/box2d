// SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
// SPDX-License-Identifier: MIT

// Bullet storm: an event-heavy benchmark for box2d_pack.
//
// 20,000 bullets fly into a field of 4,800 static targets. Every bullet that touches a target
// damages it and is recycled, relaunched from just outside the field after the step. Bullets do not collide
// with each other. The result is thousands of contact begin events per step, so event handling is a
// real share of the frame.
//
// Standard build: walk the begin events after the step and look up user data by shape id.
//     python3 box2d_pack.py examples/pack_bullets/bullets.c
// Injected build: the same handler runs inside the engine's contact loop.
//     python3 box2d_pack.py examples/pack_bullets/bullets.c examples/pack_bullets/inject.json
//
// Both builds print the same checksum.

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
	BULLET_COUNT = 20000,
	TARGET_COLUMNS = 80,
	TARGET_ROWS = 60,
	TARGET_COUNT = TARGET_COLUMNS * TARGET_ROWS,
	CATEGORY_BULLET = 0x0001,
	CATEGORY_TARGET = 0x0002,
};

typedef enum Kind
{
	KIND_BULLET,
	KIND_TARGET,
	KIND_WALL,
} Kind;

typedef struct Target
{
	Kind kind;
	int hp;
	int hitsTaken;
	int destroyedCount;
	char name[24];
	float stats[16];
} Target;

typedef struct Bullet
{
	Kind kind;
	int index;
	int damage;
	int pendingRecycle;
	int hits;
	float stats[11];
} Bullet;

typedef struct Game
{
	Target* targets;
	Bullet* bullets;
	b2BodyId* bulletBodies;
	int* recycleQueue;
	int recycleCount;
	long long hits;
	long long wallHits;
	Kind wallKind;
} Game;

static Game g_game;
static uint32_t g_seed = 99u;

static float Rand( float lo, float hi )
{
	g_seed = 1664525u * g_seed + 1013904223u;
	return lo + ( hi - lo ) * (float)( g_seed >> 8 ) * ( 1.0f / 16777216.0f );
}

// Game logic for a contact begin. Called from the engine in the injected build.
void Game_OnBegin( void* userDataA, void* userDataB );

void Game_OnBegin( void* userDataA, void* userDataB )
{
	Kind* ka = userDataA;
	Kind* kb = userDataB;
	if ( ka == NULL || kb == NULL )
	{
		return;
	}

	Bullet* bullet = NULL;
	Kind* other = NULL;
	if ( *ka == KIND_BULLET )
	{
		bullet = (Bullet*)ka;
		other = kb;
	}
	else if ( *kb == KIND_BULLET )
	{
		bullet = (Bullet*)kb;
		other = ka;
	}
	if ( bullet == NULL || bullet->pendingRecycle )
	{
		return;
	}

	if ( *other == KIND_TARGET )
	{
		Target* target = (Target*)other;
		target->hp -= bullet->damage;
		target->hitsTaken += 1;
		if ( target->hp <= 0 )
		{
			target->destroyedCount += 1;
			target->hp = 100;
		}
		bullet->hits += 1;
		g_game.hits += 1;
	}
	else
	{
		g_game.wallHits += 1;
	}

	bullet->pendingRecycle = 1;
	g_game.recycleQueue[g_game.recycleCount++] = bullet->index;
}

static void Launch( int index )
{
	// From a random point just outside the target field, so bullets spend most of their life
	// hitting targets rather than flying toward them
	float side = Rand( 0.0f, 4.0f );
	b2Pos p;
	if ( side < 1.0f )
		p = (b2Pos){ -61.0f, Rand( -45.0f, 45.0f ) };
	else if ( side < 2.0f )
		p = (b2Pos){ 61.0f, Rand( -45.0f, 45.0f ) };
	else if ( side < 3.0f )
		p = (b2Pos){ Rand( -60.0f, 60.0f ), -46.0f };
	else
		p = (b2Pos){ Rand( -60.0f, 60.0f ), 46.0f };

	b2Vec2 aim = { Rand( -55.0f, 55.0f ) - (float)p.x, Rand( -40.0f, 40.0f ) - (float)p.y };
	float length = b2Length( aim );
	float speed = Rand( 18.0f, 30.0f );
	b2Vec2 v = { speed * aim.x / length, speed * aim.y / length };

	b2BodyId bodyId = g_game.bulletBodies[index];
	b2Body_SetTransform( bodyId, p, b2Rot_identity );
	b2Body_SetLinearVelocity( bodyId, v );
}

static void Recycle( void )
{
	for ( int i = 0; i < g_game.recycleCount; ++i )
	{
		int index = g_game.recycleQueue[i];
		Launch( index );
		g_game.bullets[index].pendingRecycle = 0;
	}
	g_game.recycleCount = 0;
}

static double NowMs( void )
{
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

int main( void )
{
	g_game.targets = calloc( TARGET_COUNT, sizeof( Target ) );
	g_game.bullets = calloc( BULLET_COUNT, sizeof( Bullet ) );
	g_game.bulletBodies = calloc( BULLET_COUNT, sizeof( b2BodyId ) );
	g_game.recycleQueue = calloc( BULLET_COUNT, sizeof( int ) );
	g_game.wallKind = KIND_WALL;

	b2WorldDef worldDef = b2DefaultWorldDef();
	worldDef.gravity = (b2Vec2){ 0.0f, 0.0f };
	b2WorldId worldId = b2CreateWorld( &worldDef );

	// Targets and walls on one static body
	b2BodyDef bodyDef = b2DefaultBodyDef();
	b2BodyId groundId = b2CreateBody( worldId, &bodyDef );

	b2ShapeDef targetDef = b2DefaultShapeDef();
	targetDef.enableContactEvents = true;
	targetDef.filter.categoryBits = CATEGORY_TARGET;
	targetDef.filter.maskBits = CATEGORY_BULLET;

	// Assign target objects in a shuffled order, like heap allocated entities
	int* order = malloc( TARGET_COUNT * sizeof( int ) );
	for ( int i = 0; i < TARGET_COUNT; ++i )
	{
		order[i] = i;
	}
	for ( int i = TARGET_COUNT - 1; i > 0; --i )
	{
		int j = (int)Rand( 0.0f, (float)( i + 1 ) );
		j = j > i ? i : j;
		int t = order[i];
		order[i] = order[j];
		order[j] = t;
	}

	for ( int row = 0; row < TARGET_ROWS; ++row )
	{
		for ( int col = 0; col < TARGET_COLUMNS; ++col )
		{
			Target* target = g_game.targets + order[row * TARGET_COLUMNS + col];
			target->kind = KIND_TARGET;
			target->hp = 100;
			targetDef.userData = target;
			b2Circle c = { { -59.25f + 1.5f * col, -44.25f + 1.5f * row }, 0.35f };
			b2CreateCircleShape( groundId, &targetDef, &c );
		}
	}
	free( order );

	b2ShapeDef wallDef = targetDef;
	wallDef.userData = &g_game.wallKind;
	b2Polygon walls[4] = {
		b2MakeOffsetBox( 80.0f, 1.0f, (b2Vec2){ 0.0f, -62.0f }, b2Rot_identity ),
		b2MakeOffsetBox( 80.0f, 1.0f, (b2Vec2){ 0.0f, 62.0f }, b2Rot_identity ),
		b2MakeOffsetBox( 1.0f, 62.0f, (b2Vec2){ -80.0f, 0.0f }, b2Rot_identity ),
		b2MakeOffsetBox( 1.0f, 62.0f, (b2Vec2){ 80.0f, 0.0f }, b2Rot_identity ),
	};
	for ( int i = 0; i < 4; ++i )
	{
		b2CreatePolygonShape( groundId, &wallDef, walls + i );
	}

	// Bullets
	bodyDef.type = b2_dynamicBody;
	bodyDef.gravityScale = 0.0f;
	bodyDef.enableSleep = false;
	b2ShapeDef bulletDef = b2DefaultShapeDef();
	bulletDef.enableContactEvents = true;
	bulletDef.filter.categoryBits = CATEGORY_BULLET;
	bulletDef.filter.maskBits = CATEGORY_TARGET;
	b2Circle bulletCircle = { { 0.0f, 0.0f }, 0.1f };

	for ( int i = 0; i < BULLET_COUNT; ++i )
	{
		Bullet* bullet = g_game.bullets + i;
		bullet->kind = KIND_BULLET;
		bullet->index = i;
		bullet->damage = 5 + ( i % 7 );
		bodyDef.position = (b2Pos){ Rand( -68.0f, 68.0f ), Rand( -58.0f, 58.0f ) };
		g_game.bulletBodies[i] = b2CreateBody( worldId, &bodyDef );
		bulletDef.userData = bullet;
		b2CreateCircleShape( g_game.bulletBodies[i], &bulletDef, &bulletCircle );
		Launch( i );
	}

	double stepMs = 0.0, gameMs = 0.0;
	long long eventsWalked = 0;

	for ( int step = 0; step < STEP_COUNT; ++step )
	{
		double t0 = NowMs();
		b2World_Step( worldId, 1.0f / 60.0f, 4 );
		double t1 = NowMs();

#if B2_PACK_INJECTED == 0
		b2ContactEvents events = b2World_GetContactEvents( worldId );
		for ( int i = 0; i < events.beginCount; ++i )
		{
			b2ContactBeginTouchEvent* e = events.beginEvents + i;
			Game_OnBegin( b2Shape_GetUserData( e->shapeIdA ), b2Shape_GetUserData( e->shapeIdB ) );
		}
		eventsWalked += events.beginCount;
#endif

		Recycle();
		double t2 = NowMs();
		stepMs += t1 - t0;
		gameMs += t2 - t1;
	}

	uint64_t h = 1469598103934665603ull;
	long long destroyed = 0;
	for ( int i = 0; i < TARGET_COUNT; ++i )
	{
		destroyed += g_game.targets[i].destroyedCount;
		h = ( h ^ (uint64_t)(uint32_t)g_game.targets[i].hp ) * 1099511628211ull;
		h = ( h ^ (uint64_t)(uint32_t)g_game.targets[i].hitsTaken ) * 1099511628211ull;
	}
	for ( int i = 0; i < BULLET_COUNT; i += 7 )
	{
		b2Pos p = b2Body_GetPosition( g_game.bulletBodies[i] );
		h = ( h ^ (uint64_t)(uint32_t)(int32_t)( p.x * 1000.0f ) ) * 1099511628211ull;
		h = ( h ^ (uint64_t)(uint32_t)(int32_t)( p.y * 1000.0f ) ) * 1099511628211ull;
	}

	printf( "mode: %s\n", B2_PACK_INJECTED ? "injected" : "standard events" );
	printf( "target hits %lld (%.0f per step), wall hits %lld, targets destroyed %lld, events walked %lld\n",
			g_game.hits, (double)g_game.hits / STEP_COUNT, g_game.wallHits, destroyed, eventsWalked );
	printf( "checksum: 0x%016llx\n", (unsigned long long)h );
	printf( "time: step %.1f ms + game %.1f ms = %.1f ms\n", stepMs, gameMs, stepMs + gameMs );

	b2DestroyWorld( worldId );
	free( g_game.targets );
	free( g_game.bullets );
	free( g_game.bulletBodies );
	free( g_game.recycleQueue );
	return 0;
}
