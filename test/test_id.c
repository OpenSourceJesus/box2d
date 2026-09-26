// SPDX-FileCopyrightText: 2023 Erin Catto
// SPDX-License-Identifier: MIT

#include "test_macros.h"

#include "box2d/id.h"

int IdTest( void )
{
	// Box2D-Packed: world, body, shape, chain, and joint ids are 4 bytes and round trip through uint32_t
	uint32_t a = 0x01234567;

	{
		b2WorldId id = b2LoadWorldId( a );
		uint32_t b = b2StoreWorldId( id );
		ENSURE( b == a );
	}

	{
		b2BodyId id = b2LoadBodyId( a );
		ENSURE( b2StoreBodyId( id ) == a );
	}

	{
		b2ShapeId id = b2LoadShapeId( a );
		ENSURE( b2StoreShapeId( id ) == a );
	}

	{
		b2ChainId id = b2LoadChainId( a );
		ENSURE( b2StoreChainId( id ) == a );
	}

	{
		b2JointId id = b2LoadJointId( a );
		ENSURE( b2StoreJointId( id ) == a );
	}

	// Contact ids are 8 bytes and round trip through uint64_t
	{
		uint64_t x = 0x0123456789ABCDEFull;
		b2ContactId id = b2LoadContactId( x );
		ENSURE( b2StoreContactId( id ) == x );
	}

	// Packed layout
	ENSURE( sizeof( b2BodyId ) == 4 );
	ENSURE( sizeof( b2ShapeId ) == 4 );
	ENSURE( sizeof( b2ChainId ) == 4 );
	ENSURE( sizeof( b2JointId ) == 4 );
	ENSURE( sizeof( b2ContactId ) == 8 );

	// Null ids
	ENSURE( B2_IS_NULL( b2_nullBodyId ) );
	ENSURE( B2_IS_NULL( b2_nullContactId ) );

	return 0;
}
