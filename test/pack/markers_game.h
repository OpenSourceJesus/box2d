// SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
// SPDX-License-Identifier: MIT

// Game types for the marker equivalence test. markers.json includes this header, so injected
// code can use the types and the inline rules directly.

#pragma once

typedef enum Kind
{
	KIND_BALL,
	KIND_PLATFORM,
	KIND_PICKUP,
	KIND_GROUND,
} Kind;

typedef struct Thing
{
	Kind kind;
	int team;
	int sensorBegins;
	int sensorEnds;
	int hits;
	float hitSpeed;
	int contactBegins;
	int contactEnds;
} Thing;

// Low-gravity team. Standard build: b2BodyDef.gravityScale. Injected build: body_forces marker.
static inline float Test_GravityScale( const Thing* thing )
{
	return ( thing != NULL && thing->kind == KIND_BALL && thing->team == 1 ) ? 0.5f : 1.0f;
}
