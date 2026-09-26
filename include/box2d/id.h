// SPDX-FileCopyrightText: 2023 Erin Catto
// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>

// Note: this file should be stand-alone

/**
 * @defgroup id Ids
 * These ids serve as handles to internal Box2D objects.
 * These should be considered opaque data and passed by value.
 * Include this header if you need the id types and not the whole Box2D API.
 * All ids are considered null if initialized to zero.
 *
 * For example in C++:
 *
 * @code{.cxx}
 * b2WorldId worldId = {};
 * @endcode
 *
 * Or in C:
 *
 * @code{.c}
 * b2WorldId worldId = {0};
 * @endcode
 *
 * These are both considered null.
 *
 * @warning Do not use the internals of these ids. They are subject to change. Ids should be treated as opaque objects.
 * @warning You should use ids to access objects in Box2D. Do not access files within the src folder. Such usage is unsupported.
 * @{
 */

/// World id references a world instance. This should be treated as an opaque handle.
typedef struct b2WorldId
{
	uint16_t index1;
	uint16_t generation;
} b2WorldId;

/// Body id references a body instance. This should be treated as an opaque handle.
/// Box2D-Packed: 4 bytes. Box2D-Packed supports a single world, so no world index is stored.
typedef struct b2BodyId
{
	uint16_t index1;
	uint16_t generation;
} b2BodyId;

/// Shape id references a shape instance. This should be treated as an opaque handle.
/// Box2D-Packed: 4 bytes.
typedef struct b2ShapeId
{
	uint16_t index1;
	uint16_t generation;
} b2ShapeId;

/// Chain id references a chain instances. This should be treated as an opaque handle.
/// Box2D-Packed: 4 bytes.
typedef struct b2ChainId
{
	uint16_t index1;
	uint16_t generation;
} b2ChainId;

/// Joint id references a joint instance. This should be treated as an opaque handle.
/// Box2D-Packed: 4 bytes.
typedef struct b2JointId
{
	uint16_t index1;
	uint16_t generation;
} b2JointId;

/// Contact id references a contact instance. This should be treated as an opaque handled.
/// Box2D-Packed: 8 bytes. Contacts keep a 32-bit index and 32-bit generation because contact
/// counts routinely exceed 65535 in large piles and contacts churn quickly, so a 16-bit
/// generation would wrap too fast to reliably detect stale handles.
typedef struct b2ContactId
{
	int32_t index1;
	uint32_t generation;
} b2ContactId;

/// The largest one-based index that fits in a 16-bit handle. Bodies, shapes, chains, and joints
/// are limited to this many live instances per world.
#define B2_MAX_HANDLE_INDEX1 0xFFFF

// Compile-time size checks for the packed handles.
#if defined( __cplusplus )
static_assert( sizeof( b2WorldId ) == 4, "b2WorldId must be 4 bytes" );
static_assert( sizeof( b2BodyId ) == 4, "b2BodyId must be 4 bytes" );
static_assert( sizeof( b2ShapeId ) == 4, "b2ShapeId must be 4 bytes" );
static_assert( sizeof( b2ChainId ) == 4, "b2ChainId must be 4 bytes" );
static_assert( sizeof( b2JointId ) == 4, "b2JointId must be 4 bytes" );
static_assert( sizeof( b2ContactId ) == 8, "b2ContactId must be 8 bytes" );
#elif defined( __STDC_VERSION__ ) && __STDC_VERSION__ >= 201112L
_Static_assert( sizeof( b2WorldId ) == 4, "b2WorldId must be 4 bytes" );
_Static_assert( sizeof( b2BodyId ) == 4, "b2BodyId must be 4 bytes" );
_Static_assert( sizeof( b2ShapeId ) == 4, "b2ShapeId must be 4 bytes" );
_Static_assert( sizeof( b2ChainId ) == 4, "b2ChainId must be 4 bytes" );
_Static_assert( sizeof( b2JointId ) == 4, "b2JointId must be 4 bytes" );
_Static_assert( sizeof( b2ContactId ) == 8, "b2ContactId must be 8 bytes" );
#endif

// clang-format off
#ifdef __cplusplus
/// A null id. Works for any id type.
#define B2_NULL_ID {}
/// Inline function.
#define B2_ID_INLINE inline
#else
/// A null id. Works for any id type.
#define B2_NULL_ID { 0 }
/// Inline function.
#define B2_ID_INLINE static inline
#endif
// clang-format on

/// Use these to make your identifiers null.
/// You may also use zero initialization to get null.
static const b2WorldId b2_nullWorldId = B2_NULL_ID;
static const b2BodyId b2_nullBodyId = B2_NULL_ID;
static const b2ShapeId b2_nullShapeId = B2_NULL_ID;
static const b2ChainId b2_nullChainId = B2_NULL_ID;
static const b2JointId b2_nullJointId = B2_NULL_ID;
static const b2ContactId b2_nullContactId = B2_NULL_ID;

/// Macro to determine if any id is null.
#define B2_IS_NULL( id ) ( ( id ).index1 == 0 )

/// Macro to determine if any id is non-null.
#define B2_IS_NON_NULL( id ) ( ( id ).index1 != 0 )

/// Compare two ids for equality. Works for any id type. Don't mix types.
#define B2_ID_EQUALS( id1, id2 ) ( ( id1 ).index1 == ( id2 ).index1 && ( id1 ).generation == ( id2 ).generation )

/// Store a world id into a uint32_t.
B2_ID_INLINE uint32_t b2StoreWorldId( b2WorldId id )
{
	return ( (uint32_t)id.index1 << 16 ) | (uint32_t)id.generation;
}

/// Load a uint32_t into a world id.
B2_ID_INLINE b2WorldId b2LoadWorldId( uint32_t x )
{
	b2WorldId id = { (uint16_t)( x >> 16 ), (uint16_t)( x ) };
	return id;
}

/// Store a body id into a uint32_t.
B2_ID_INLINE uint32_t b2StoreBodyId( b2BodyId id )
{
	return ( (uint32_t)id.index1 << 16 ) | (uint32_t)id.generation;
}

/// Load a uint32_t into a body id.
B2_ID_INLINE b2BodyId b2LoadBodyId( uint32_t x )
{
	b2BodyId id = { (uint16_t)( x >> 16 ), (uint16_t)( x ) };
	return id;
}

/// Store a shape id into a uint32_t.
B2_ID_INLINE uint32_t b2StoreShapeId( b2ShapeId id )
{
	return ( (uint32_t)id.index1 << 16 ) | (uint32_t)id.generation;
}

/// Load a uint32_t into a shape id.
B2_ID_INLINE b2ShapeId b2LoadShapeId( uint32_t x )
{
	b2ShapeId id = { (uint16_t)( x >> 16 ), (uint16_t)( x ) };
	return id;
}

/// Store a chain id into a uint32_t.
B2_ID_INLINE uint32_t b2StoreChainId( b2ChainId id )
{
	return ( (uint32_t)id.index1 << 16 ) | (uint32_t)id.generation;
}

/// Load a uint32_t into a chain id.
B2_ID_INLINE b2ChainId b2LoadChainId( uint32_t x )
{
	b2ChainId id = { (uint16_t)( x >> 16 ), (uint16_t)( x ) };
	return id;
}

/// Store a joint id into a uint32_t.
B2_ID_INLINE uint32_t b2StoreJointId( b2JointId id )
{
	return ( (uint32_t)id.index1 << 16 ) | (uint32_t)id.generation;
}

/// Load a uint32_t into a joint id.
B2_ID_INLINE b2JointId b2LoadJointId( uint32_t x )
{
	b2JointId id = { (uint16_t)( x >> 16 ), (uint16_t)( x ) };
	return id;
}

/// Store a contact id into a uint64_t.
B2_ID_INLINE uint64_t b2StoreContactId( b2ContactId id )
{
	return ( (uint64_t)(uint32_t)id.index1 << 32 ) | (uint64_t)id.generation;
}

/// Load a uint64_t into a contact id.
B2_ID_INLINE b2ContactId b2LoadContactId( uint64_t x )
{
	b2ContactId id = { (int32_t)( x >> 32 ), (uint32_t)( x ) };
	return id;
}

/**@}*/
