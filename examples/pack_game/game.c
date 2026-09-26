// SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
// SPDX-License-Identifier: MIT

// Arena brawl: a small game demo and benchmark for box2d_pack.
//
// Two teams of units bounce around a walled arena. Each shape's userData points at a game
// object. When units of opposite teams touch, both take damage and the attacker scores.
// Dead units respawn after the step.
//
// Standard build (no injection): after each step the game walks the contact begin events and
// looks up each shape's userData with b2Shape_GetUserData.
//
//     python3 box2d_pack.py examples/pack_game/game.c
//
// Injected build: Game_OnContactBegin runs inside the engine's contact loop, where the shapes
// are already in cache, and the begin event array is not filled at all.
//
//     python3 box2d_pack.py examples/pack_game/game.c examples/pack_game/inject.json
//
// Both builds print the same checksum, which shows the game logic ran identically.

// clock_gettime under -std=c17
#define _POSIX_C_SOURCE 199309L

#include "box2d/box2d.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef B2_PACK_INJECTED
#define B2_PACK_INJECTED 0
#endif

enum
{
	UNIT_COUNT = 3000,
	STEP_COUNT = 600,
	MAX_HP = 100,
	DAMAGE = 7,
};

typedef enum ObjectKind
{
	KIND_UNIT,
	KIND_WALL,
} ObjectKind;

// A typical game entity, larger than a cache line, with more state than the physics needs
typedef struct GameObject
{
	ObjectKind kind;
	int team;
	int hp;
	int kills;
	int deaths;
	int wallHits;
	int lastHitStep;
	int pendingRespawn;
	b2BodyId bodyId;
	char name[32];
	float stats[32];
} GameObject;

typedef struct Game
{
	GameObject* units; // shuffled in memory, like heap allocated entities
	GameObject wall;
	int step;
	int hits;
	int respawnQueue[UNIT_COUNT];
	int respawnCount;
} Game;

static Game g_game;
static uint32_t g_seed = 12345u;

static float RandomFloat( float lo, float hi )
{
	g_seed = 1664525u * g_seed + 1013904223u;
	return lo + ( hi - lo ) * (float)( g_seed >> 8 ) * ( 1.0f / 16777216.0f );
}

// Game logic for a begin touch. Called from the engine in the injected build, and from the
// event loop in the standard build. Order of calls is the same in both.
void Game_OnContactBegin( void* userDataA, void* userDataB );

void Game_OnContactBegin( void* userDataA, void* userDataB )
{
	GameObject* a = userDataA;
	GameObject* b = userDataB;
	if ( a == NULL || b == NULL )
	{
		return;
	}

	if ( a->kind == KIND_WALL || b->kind == KIND_WALL )
	{
		GameObject* unit = a->kind == KIND_UNIT ? a : b;
		if ( unit->kind == KIND_UNIT )
		{
			unit->wallHits += 1;
		}
		return;
	}

	if ( a->team == b->team || a->hp <= 0 || b->hp <= 0 )
	{
		return;
	}

	g_game.hits += 1;
	a->hp -= DAMAGE;
	b->hp -= DAMAGE;
	a->lastHitStep = g_game.step;
	b->lastHitStep = g_game.step;

	GameObject* pair[2] = { a, b };
	for ( int i = 0; i < 2; ++i )
	{
		GameObject* victim = pair[i];
		if ( victim->hp <= 0 && victim->pendingRespawn == 0 )
		{
			pair[1 - i]->kills += 1;
			victim->deaths += 1;
			victim->pendingRespawn = 1;
			g_game.respawnQueue[g_game.respawnCount++] = (int)( victim - g_game.units );
		}
	}
}

static void CreateArena( b2WorldId worldId )
{
	g_game.wall.kind = KIND_WALL;

	b2BodyDef bodyDef = b2DefaultBodyDef();
	b2BodyId groundId = b2CreateBody( worldId, &bodyDef );

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.userData = &g_game.wall;
	shapeDef.enableContactEvents = true;
	shapeDef.material.friction = 0.0f;
	shapeDef.material.restitution = 1.0f;

	float h = 60.0f;
	b2Polygon walls[4] = {
		b2MakeOffsetBox( h, 1.0f, (b2Vec2){ 0.0f, -h }, b2Rot_identity ),
		b2MakeOffsetBox( h, 1.0f, (b2Vec2){ 0.0f, h }, b2Rot_identity ),
		b2MakeOffsetBox( 1.0f, h, (b2Vec2){ -h, 0.0f }, b2Rot_identity ),
		b2MakeOffsetBox( 1.0f, h, (b2Vec2){ h, 0.0f }, b2Rot_identity ),
	};
	for ( int i = 0; i < 4; ++i )
	{
		b2CreatePolygonShape( groundId, &shapeDef, walls + i );
	}
}

static void CreateUnits( b2WorldId worldId )
{
	g_game.units = calloc( UNIT_COUNT, sizeof( GameObject ) );

	// Shuffle which object each body gets, so user data pointers are scattered like heap entities
	int order[UNIT_COUNT];
	for ( int i = 0; i < UNIT_COUNT; ++i )
	{
		order[i] = i;
	}
	for ( int i = UNIT_COUNT - 1; i > 0; --i )
	{
		int j = (int)RandomFloat( 0.0f, (float)( i + 1 ) );
		j = j > i ? i : j;
		int t = order[i];
		order[i] = order[j];
		order[j] = t;
	}

	b2BodyDef bodyDef = b2DefaultBodyDef();
	bodyDef.type = b2_dynamicBody;
	bodyDef.gravityScale = 0.0f;
	bodyDef.enableSleep = false;

	b2ShapeDef shapeDef = b2DefaultShapeDef();
	shapeDef.enableContactEvents = true;
	shapeDef.material.friction = 0.0f;
	shapeDef.material.restitution = 1.0f;

	b2Circle circle = { { 0.0f, 0.0f }, 0.4f };

	for ( int i = 0; i < UNIT_COUNT; ++i )
	{
		GameObject* unit = g_game.units + order[i];
		unit->kind = KIND_UNIT;
		unit->team = i & 1;
		unit->hp = MAX_HP;
		snprintf( unit->name, sizeof( unit->name ), "unit_%d", i );

		bodyDef.position = (b2Pos){ RandomFloat( -55.0f, 55.0f ), RandomFloat( -55.0f, 55.0f ) };
		bodyDef.linearVelocity = (b2Vec2){ RandomFloat( -12.0f, 12.0f ), RandomFloat( -12.0f, 12.0f ) };
		unit->bodyId = b2CreateBody( worldId, &bodyDef );

		shapeDef.userData = unit;
		b2CreateCircleShape( unit->bodyId, &shapeDef, &circle );
	}
}

// Respawn dead units at their team's side. Runs after the step in both builds.
static void RespawnUnits( void )
{
	for ( int i = 0; i < g_game.respawnCount; ++i )
	{
		GameObject* unit = g_game.units + g_game.respawnQueue[i];
		float x = unit->team == 0 ? -50.0f : 50.0f;
		float y = -50.0f + 100.0f * (float)( g_game.respawnQueue[i] % 97 ) / 97.0f;
		b2Body_SetTransform( unit->bodyId, (b2Pos){ x, y }, b2Rot_identity );
		b2Body_SetLinearVelocity( unit->bodyId, (b2Vec2){ unit->team == 0 ? 10.0f : -10.0f, 0.0f } );
		unit->hp = MAX_HP;
		unit->pendingRespawn = 0;
	}
	g_game.respawnCount = 0;
}

static uint64_t Checksum( void )
{
	uint64_t h = 14695981039346656037ull;
	for ( int i = 0; i < UNIT_COUNT; ++i )
	{
		GameObject* u = g_game.units + i;
		b2Pos p = b2Body_GetPosition( u->bodyId );
		int32_t v[6] = { u->hp, u->kills, u->deaths, u->wallHits, (int32_t)( p.x * 1000.0f ), (int32_t)( p.y * 1000.0f ) };
		const uint8_t* bytes = (const uint8_t*)v;
		for ( size_t k = 0; k < sizeof( v ); ++k )
		{
			h = ( h ^ bytes[k] ) * 1099511628211ull;
		}
	}
	return h;
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
	worldDef.gravity = (b2Vec2){ 0.0f, 0.0f };
	b2WorldId worldId = b2CreateWorld( &worldDef );

	CreateArena( worldId );
	CreateUnits( worldId );

	double stepMs = 0.0;
	double gameMs = 0.0;
	long long events = 0;

	for ( g_game.step = 0; g_game.step < STEP_COUNT; ++g_game.step )
	{
		double t0 = NowMs();
		b2World_Step( worldId, 1.0f / 60.0f, 4 );
		double t1 = NowMs();

#if B2_PACK_INJECTED == 0
		// Standard API: walk the begin events and look up each shape's user data
		b2ContactEvents contactEvents = b2World_GetContactEvents( worldId );
		for ( int i = 0; i < contactEvents.beginCount; ++i )
		{
			b2ContactBeginTouchEvent* e = contactEvents.beginEvents + i;
			Game_OnContactBegin( b2Shape_GetUserData( e->shapeIdA ), b2Shape_GetUserData( e->shapeIdB ) );
		}
		events += contactEvents.beginCount;
#endif

		RespawnUnits();
		double t2 = NowMs();
		stepMs += t1 - t0;
		gameMs += t2 - t1;
	}

	int kills = 0;
	for ( int i = 0; i < UNIT_COUNT; ++i )
	{
		kills += g_game.units[i].kills;
	}

	printf( "mode: %s\n", B2_PACK_INJECTED ? "injected" : "standard events" );
	printf( "hits: %d, kills: %d, events walked after step: %lld\n", g_game.hits, kills, events );
	printf( "checksum: 0x%016llx\n", (unsigned long long)Checksum() );
	printf( "time: step %.1f ms + game %.1f ms = %.1f ms\n", stepMs, gameMs, stepMs + gameMs );

	b2DestroyWorld( worldId );
	free( g_game.units );
	return 0;
}
