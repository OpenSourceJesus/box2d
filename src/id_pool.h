// SPDX-FileCopyrightText: 2023 Erin Catto
// SPDX-License-Identifier: MIT

#pragma once

#include "container.h"

#include "box2d/id.h"

typedef struct b2IdPool
{
	b2Array( int ) freeArray;
	int nextIndex;
} b2IdPool;

b2IdPool b2CreateIdPool( void );
void b2DestroyIdPool( b2IdPool* pool );

int b2AllocId( b2IdPool* pool );
void b2FreeId( b2IdPool* pool, int id );
void b2ValidateFreeId( b2IdPool* pool, int id );
void b2ValidateUsedId( b2IdPool* pool, int id );

static inline int b2GetIdCount( b2IdPool* pool )
{
	return pool->nextIndex - pool->freeArray.count;
}

static inline int b2GetIdCapacity( b2IdPool* pool )
{
	return pool->nextIndex;
}

static inline int b2GetIdBytes( b2IdPool* pool )
{
	return b2Array_ByteCount( pool->freeArray );
}

/// Box2D-Packed: number of ids that can still be allocated before a one-based index would
/// exceed the 16-bit handle range. Use for pools whose ids are exposed as 4-byte handles
/// (bodies, shapes, chains, joints).
static inline int b2GetHandleRoom( b2IdPool* pool )
{
	return pool->freeArray.count + ( B2_MAX_HANDLE_INDEX1 - pool->nextIndex );
}
