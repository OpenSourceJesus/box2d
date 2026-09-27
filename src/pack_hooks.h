// SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
// SPDX-License-Identifier: MIT

#pragma once

// Box2D-Packed injection hooks shared by several source files. The marker comments are inert in
// normal builds. box2d_pack.py replaces them with user code, see INTRUSIVENGINE.md.

#include "body.h"
#include "physics_world.h"
#include "shape.h"

// File scope in every engine file that has markers. Declarations only: declare your functions and
// extern variables here. A definition would be duplicated in each file.
//$pack_hooks$GLOBALS

// Contact end touch, from every place a touching contact ends: the narrow phase when shapes stop
// touching, and contact destruction when bounding boxes separate or when a body or shape is
// destroyed, disabled, or refiltered. Pushes the end event and runs the injection marker.
static inline void b2PackContactEnd( b2World* world, const b2Shape* shapeA, const b2Shape* shapeB,
									 b2ContactId contactFullId )
{
	b2ShapeId shapeIdA = { (uint16_t)( shapeA->id + 1 ), shapeA->generation };
	b2ShapeId shapeIdB = { (uint16_t)( shapeB->id + 1 ), shapeB->generation };

#ifndef B2_PACK_NO_CONTACT_END_ARRAY
	b2ContactEndTouchEvent event = { shapeIdA, shapeIdB, contactFullId };
	b2Array_Push( world->contactEndEvents[world->endEventArrayIndex], event );
#endif

	// Single threaded. Usually inside the step with the world locked, but also inside calls such as
	// b2DestroyBody and b2DestroyShape: do not create or destroy anything here. In scope: world,
	// shapeA, shapeB (const b2Shape*, ->userData), shapeIdA, shapeIdB, contactFullId.
	//$b2PackContactEnd$CONTACT_END

	(void)shapeIdA;
	(void)shapeIdB;
}

// User data of the body that owns a body sim. The b2Body record is cold, so this is a random read.
static inline void* b2PackBodyUserData( const b2World* world, const b2BodySim* sim )
{
	return world->bodies.data[sim->bodyId].userData;
}

// Per-body gravity, applied everywhere the engine uses gravity: velocity integration, every
// substep, and continuous collision, which removes the gravity gained during lost time. Called
// only for dynamic bodies. Runs on worker threads, injected code must be thread-safe.
static inline void b2PackBodyGravity( const b2World* world, const b2BodySim* sim, float* gravityScalePtr,
									  b2Vec2* bodyGravityPtr )
{
	float gravityScale = *gravityScalePtr;
	b2Vec2 bodyGravity = *bodyGravityPtr;

	// WORKER THREADS, must be thread-safe. In scope: world, sim (const b2BodySim*: center,
	// transform, invMass), gravityScale and bodyGravity (both may be changed),
	// b2PackBodyUserData( world, sim ) (a cold read).
	//$b2PackBodyGravity$BODY_GRAVITY

	*gravityScalePtr = gravityScale;
	*bodyGravityPtr = bodyGravity;
	(void)world;
	(void)sim;
}

// Custom pair filter, applied everywhere the engine filters pairs: new contacts in the broad
// phase, sensor overlaps, and continuous collision. Called only when either shape enables custom
// filtering. Runs on worker threads, injected code must be thread-safe.
static inline bool b2PackCustomFilter( const b2Shape* shapeA, const b2Shape* shapeB )
{
	bool shouldCollide = true;

	// WORKER THREADS, must be thread-safe. In scope: shapeA, shapeB (const b2Shape*, ->userData),
	// shouldCollide (set false to reject the pair).
	//$b2PackCustomFilter$FILTER

	(void)shapeA;
	(void)shapeB;
	return shouldCollide;
}
