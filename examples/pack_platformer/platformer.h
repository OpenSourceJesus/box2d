// SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
// SPDX-License-Identifier: MIT

// Game types and rules for the platformer example. platformer.c uses these rules from Box2D
// callbacks and event arrays. inject.json includes this header, so the injected build runs the
// same rules inline inside the engine, with no function calls.
//
// Handlers of different kinds run at different points in the step in the injected build. Each
// one writes its own fields or only adds to counters, so both builds get the same results.

#pragma once

#include "box2d/box2d.h"

typedef enum Kind
{
	KIND_RUNNER,
	KIND_ENEMY,
	KIND_PLATFORM,
	KIND_SOLID,
	KIND_COIN,
} Kind;

typedef struct Runner
{
	Kind kind;
	int index;
	int direction;
	int jumpCooldown;
	int lowGravityTimer;
	int bounceRequest;
	int coins;
	int feathers;
	int stomps;
	int hurts;
	int hardLandings;
	int score;
	float appliedGravityScale; // standard build only
	char name[20];
} Runner;

typedef struct Enemy
{
	Kind kind;
	int direction;
	int stomped;
	float minX, maxX;
} Enemy;

typedef struct Coin
{
	Kind kind;
	int isFeather;
	int cooldown;
	int collected;
} Coin;

typedef struct Tile
{
	Kind kind;
} Tile;

enum
{
	LOW_GRAVITY_STEPS = 180,
	COIN_COOLDOWN_STEPS = 120,
};

// One-way platforms: runners and enemies pass up through them. Runs on worker threads in the
// injected build. Reads kinds and writes only this contact's manifold, so it is thread-safe.
static inline void Game_PreSolve( const void* dataA, const void* dataB, b2Manifold* manifold )
{
	const Kind* a = dataA;
	const Kind* b = dataB;
	if ( a == NULL || b == NULL )
	{
		return;
	}

	// The normal points from A to B
	if ( *a == KIND_PLATFORM && manifold->normal.y < 0.5f )
	{
		manifold->pointCount = 0;
	}
	else if ( *b == KIND_PLATFORM && manifold->normal.y > -0.5f )
	{
		manifold->pointCount = 0;
	}
}

// Coins and feathers. Writes the coin and the runner's pickup fields.
static inline void Game_OnSensorBegin( void* sensorData, void* visitorData )
{
	Coin* coin = sensorData;
	Runner* runner = visitorData;
	if ( coin == NULL || runner == NULL || coin->kind != KIND_COIN || runner->kind != KIND_RUNNER || coin->cooldown > 0 )
	{
		return;
	}

	coin->cooldown = COIN_COOLDOWN_STEPS;
	coin->collected += 1;
	if ( coin->isFeather )
	{
		runner->feathers += 1;
		runner->lowGravityTimer = LOW_GRAVITY_STEPS;
	}
	else
	{
		runner->coins += 1;
		runner->score += 1;
	}
}

// Runner meets enemy: stomp from above, otherwise get hurt. The normal points from A to B.
static inline void Game_OnContactBegin( void* dataA, void* dataB, b2Vec2 normal )
{
	Kind* a = dataA;
	Kind* b = dataB;
	if ( a == NULL || b == NULL )
	{
		return;
	}

	Runner* runner;
	Enemy* enemy;
	float up;
	if ( *a == KIND_RUNNER && *b == KIND_ENEMY )
	{
		runner = (Runner*)a;
		enemy = (Enemy*)b;
		up = -normal.y;
	}
	else if ( *b == KIND_RUNNER && *a == KIND_ENEMY )
	{
		runner = (Runner*)b;
		enemy = (Enemy*)a;
		up = normal.y;
	}
	else
	{
		return;
	}

	if ( up > 0.5f )
	{
		enemy->stomped += 1;
		runner->stomps += 1;
		runner->score += 10;
		runner->bounceRequest = 1;
	}
	else
	{
		runner->hurts += 1;
		runner->score -= 5;
	}
}

// Hard landings on the level geometry
static inline void Game_OnHit( void* dataA, void* dataB, float approachSpeed )
{
	Kind* a = dataA;
	Kind* b = dataB;
	if ( a == NULL || b == NULL || approachSpeed < 6.0f )
	{
		return;
	}

	Runner* runner = *a == KIND_RUNNER ? (Runner*)a : ( *b == KIND_RUNNER ? (Runner*)b : NULL );
	Kind* other = *a == KIND_RUNNER ? b : a;
	if ( runner != NULL && ( *other == KIND_SOLID || *other == KIND_PLATFORM ) )
	{
		runner->hardLandings += 1;
	}
}

// Feather low gravity. Runs on worker threads in the injected build and only reads the runner.
static inline float Game_GravityScale( const void* bodyData )
{
	const Runner* runner = bodyData;
	if ( runner != NULL && runner->kind == KIND_RUNNER && runner->lowGravityTimer > 0 )
	{
		return 0.35f;
	}
	return 1.0f;
}
