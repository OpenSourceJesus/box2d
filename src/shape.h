// SPDX-FileCopyrightText: 2023 Erin Catto
// SPDX-License-Identifier: MIT

#pragma once

#include <stddef.h>

#include "container.h"

#include "box2d/types.h"

typedef struct b2BroadPhase b2BroadPhase;
typedef struct b2World b2World;

// Box2D-Packed: fields are ordered so everything the broad-phase pair filter reads
// (bodyId, sensorIndex, type, filter, custom filtering flag, generation) sits in the first
// 64-byte cache line, and aabb starts cache line 1.
//
// The size is deliberately 264 bytes, not 256. With a power-of-two stride, the same field of
// every shape maps to one quarter of the cache sets, and loops that sweep all shapes (such as
// b2FinalizeBodies computing fat AABBs) thrash those sets. Cachegrind showed a 256-byte layout
// raised large_pyramid last-level data misses by 27%. At 264 bytes each shape shifts by 8 bytes
// and fields spread across all sets. The flags stay plain bools: packing them into bit-fields
// is what shrank the struct to 256.
typedef struct b2Shape
{
	// Cache line 0: pair filtering and identity
	int id;
	int bodyId;
	int sensorIndex;
	b2ShapeType type;
	b2Filter filter;
	uint16_t generation;
	bool enableSensorEvents;
	bool enableContactEvents;
	bool enableCustomFiltering;
	bool enableHitEvents;
	bool enablePreSolveEvents;
	int prevShapeId;
	int nextShapeId;
	int proxyKey;
	float density;
	float aabbMargin;
	void* userData;

	// Cache line 1: bounds and material. aabb starts at offset 64.
	b2AABB aabb;
	b2Vec2 localCentroid;
	b2SurfaceMaterial material;

	// Cache lines 1-4: geometry
	union
	{
		b2Capsule capsule;
		b2Circle circle;
		b2Polygon polygon;
		b2Segment segment;
		b2ChainSegment chainSegment;
	};
} b2Shape;

_Static_assert( sizeof( b2Shape ) == 264, "b2Shape size, must not be a power of two, see above" );
_Static_assert( offsetof( b2Shape, aabb ) == 64, "aabb should start cache line 1" );
_Static_assert( offsetof( b2Shape, userData ) + sizeof( void* ) <= 64, "pair filter fields must fit in cache line 0" );

typedef struct b2ChainShape
{
	int id;
	int bodyId;
	int nextChainId;
	int segmentCount;
	uint16_t generation;
	int* shapeIndices;
} b2ChainShape;

typedef struct b2ShapeExtent
{
	float minExtent;
	float maxExtent;
} b2ShapeExtent;

// Sensors are shapes that live in the broad-phase but never have contacts.
// At the end of the time step all sensors are queried for overlap with any other shapes.
// Sensors ignore body type and sleeping.
// Sensors generate events when there is a new overlap or and overlap disappears.
// The sensor overlaps don't get cleared until the next time step regardless of the overlapped
// shapes being destroyed.
// When a sensor is destroyed.
typedef struct
{
	b2Array( int ) overlaps;
} b2SensorOverlaps;

void b2CreateShapeProxy( b2World* world, b2Shape* shape, b2BodyType type, b2WorldTransform transform, bool forcePairCreation );
void b2DestroyShapeProxy( b2Shape* shape, b2BroadPhase* bp );

void b2FreeChainData( b2ChainShape* chain );

b2MassData b2ComputeShapeMass( const b2Shape* shape );
b2ShapeExtent b2ComputeShapeExtent( const b2Shape* shape, b2Vec2 localCenter );
b2AABB b2ComputeShapeAABB( const b2Shape* shape, b2WorldTransform transform );

// Conservative world AABB for a shape, inflated by extra margin. In large world mode this is
// computed in double and rounded outward so the inflation is not lost far from the origin.
b2AABB b2ComputeFatShapeAABB( const b2Shape* shape, b2WorldTransform transform, float extra );
b2Vec2 b2GetShapeCentroid( const b2Shape* shape );
float b2GetShapePerimeter( const b2Shape* shape );
float b2GetShapeProjectedPerimeter( const b2Shape* shape, b2Vec2 line );

b2ShapeProxy b2MakeShapeDistanceProxy( const b2Shape* shape );

b2CastOutput b2RayCastShape( const b2RayCastInput* input, const b2Shape* shape, b2Transform transform );
b2CastOutput b2ShapeCastShape( const b2ShapeCastInput* input, const b2Shape* shape, b2Transform transform );

b2PlaneResult b2CollideMoverAndCircle( const b2Capsule* mover, const b2Circle* shape );
b2PlaneResult b2CollideMoverAndCapsule( const b2Capsule* mover, const b2Capsule* shape );
b2PlaneResult b2CollideMoverAndPolygon( const b2Capsule* mover, const b2Polygon* shape );
b2PlaneResult b2CollideMoverAndSegment( const b2Capsule* mover, const b2Segment* shape );
b2PlaneResult b2CollideMover( const b2Capsule* mover, const b2Shape* shape, b2Transform transform );

static inline float b2GetShapeRadius( const b2Shape* shape )
{
	switch ( shape->type )
	{
		case b2_capsuleShape:
			return shape->capsule.radius;
		case b2_circleShape:
			return shape->circle.radius;
		case b2_polygonShape:
			return shape->polygon.radius;
		default:
			return 0.0f;
	}
}

static inline bool b2ShouldShapesCollide( b2Filter filterA, b2Filter filterB )
{
	if ( filterA.groupIndex == filterB.groupIndex && filterA.groupIndex != 0 )
	{
		return filterA.groupIndex > 0;
	}

	return ( filterA.maskBits & filterB.categoryBits ) != 0 && ( filterA.categoryBits & filterB.maskBits ) != 0;
}

static inline bool b2ShouldQueryCollide( b2Filter shapeFilter, b2QueryFilter queryFilter )
{
	return ( shapeFilter.categoryBits & queryFilter.maskBits ) != 0 && ( shapeFilter.maskBits & queryFilter.categoryBits ) != 0;
}

b2DeclareArray( b2Shape );
b2DeclareArray( b2ChainShape );
