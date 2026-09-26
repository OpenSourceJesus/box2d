// SPDX-FileCopyrightText: 2026 Erin Catto
// SPDX-License-Identifier: MIT

#include "core.h"
#include "determinism.h"
#include "physics_world.h"
#include "snapshot.h"
#include "test_macros.h"

#include "box2d/box2d.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* s_snapPath = "test_snapshot_midstream.b2rec";

// Ids held across a snapshot to prove they keep resolving after an in-place restore
typedef struct SnapshotIds
{
	b2BodyId body;
	b2ShapeId shape;
	b2JointId joint;
	b2ChainId chain;
} SnapshotIds;

// Build a scene that exercises every heap-bearing container:
// - ground (static box)
// - main stack of dynamic boxes (settling stack)
// - several joint types (revolute, prismatic, distance, weld)
// - one chain shape
// - one sensor shape overlapping a moving body
// - isolated second stack let to sleep before the snapshot
//
// Returns the worldId. When outIds is non-NULL it also returns one id of each kind
// (body, shape, joint, chain) so a caller can check they survive an in-place restore.
static b2WorldId BuildScene( int workerCount, SnapshotIds* outIds )
{
	b2JointId heldJoint = b2_nullJointId;
	b2ChainId heldChain = b2_nullChainId;

	b2WorldDef def = b2DefaultWorldDef();
	def.workerCount = workerCount;
	b2WorldId worldId = b2CreateWorld( &def );

	// Ground
	{
		b2BodyDef bd = b2DefaultBodyDef();
		bd.position = (b2Pos){ 0.0f, -1.0f };
		b2BodyId groundId = b2CreateBody( worldId, &bd );

		b2Polygon groundBox = b2MakeBox( 40.0f, 1.0f );
		b2ShapeDef sd = b2DefaultShapeDef();
		b2CreatePolygonShape( groundId, &sd, &groundBox );
	}

	// Main stack: 8 dynamic boxes
	b2BodyId stackTop = b2_nullBodyId;
	{
		b2ShapeDef sd = b2DefaultShapeDef();
		b2Polygon box = b2MakeBox( 0.5f, 0.5f );
		for ( int i = 0; i < 8; ++i )
		{
			b2BodyDef bd = b2DefaultBodyDef();
			bd.type = b2_dynamicBody;
			bd.position = (b2Pos){ 0.0f, 0.5f + (float)i * 1.1f };
			b2BodyId bodyId = b2CreateBody( worldId, &bd );
			b2CreatePolygonShape( bodyId, &sd, &box );
			stackTop = bodyId;
		}
	}

	// Joint bodies: two dynamic bodies, one for each pair of joints
	b2BodyDef jbDef = b2DefaultBodyDef();
	jbDef.type = b2_dynamicBody;
	jbDef.position = (b2Pos){ 5.0f, 2.0f };
	b2BodyId jbA = b2CreateBody( worldId, &jbDef );
	jbDef.position = (b2Pos){ 7.0f, 2.0f };
	b2BodyId jbB = b2CreateBody( worldId, &jbDef );
	jbDef.position = (b2Pos){ 9.0f, 2.0f };
	b2BodyId jbC = b2CreateBody( worldId, &jbDef );
	jbDef.position = (b2Pos){ 11.0f, 2.0f };
	b2BodyId jbD = b2CreateBody( worldId, &jbDef );

	b2Polygon jbox = b2MakeBox( 0.3f, 0.3f );
	b2ShapeDef jsd = b2DefaultShapeDef();
	b2ShapeId heldShape = b2CreatePolygonShape( jbA, &jsd, &jbox );
	b2CreatePolygonShape( jbB, &jsd, &jbox );
	b2CreatePolygonShape( jbC, &jsd, &jbox );
	b2CreatePolygonShape( jbD, &jsd, &jbox );

	// Revolute joint (mirrors determinism.c idiom)
	{
		b2RevoluteJointDef rd = b2DefaultRevoluteJointDef();
		rd.enableLimit = true;
		rd.lowerAngle = -0.1f * B2_PI;
		rd.upperAngle = 0.2f * B2_PI;
		rd.enableSpring = true;
		rd.hertz = 1.0f;
		rd.dampingRatio = 1.0f;
		rd.enableMotor = true;
		rd.maxMotorTorque = 0.5f;
		rd.base.bodyIdA = jbA;
		rd.base.bodyIdB = jbB;
		rd.base.localFrameA.p = (b2Vec2){ 0.3f, 0.0f };
		rd.base.localFrameB.p = (b2Vec2){ -0.3f, 0.0f };
		heldJoint = b2CreateRevoluteJoint( worldId, &rd );
	}

	// Prismatic joint
	{
		b2PrismaticJointDef pd = b2DefaultPrismaticJointDef();
		pd.enableLimit = true;
		pd.lowerTranslation = -0.5f;
		pd.upperTranslation = 0.5f;
		pd.base.bodyIdA = jbB;
		pd.base.bodyIdB = jbC;
		pd.base.localFrameA.p = (b2Vec2){ 0.3f, 0.0f };
		pd.base.localFrameB.p = (b2Vec2){ -0.3f, 0.0f };
		b2CreatePrismaticJoint( worldId, &pd );
	}

	// Distance joint
	{
		b2DistanceJointDef dd = b2DefaultDistanceJointDef();
		dd.length = 2.0f;
		dd.base.bodyIdA = jbC;
		dd.base.bodyIdB = jbD;
		dd.base.localFrameA.p = (b2Vec2){ 0.3f, 0.0f };
		dd.base.localFrameB.p = (b2Vec2){ -0.3f, 0.0f };
		b2CreateDistanceJoint( worldId, &dd );
	}

	// Weld joint
	{
		b2WeldJointDef wd = b2DefaultWeldJointDef();
		wd.linearHertz = 5.0f;
		wd.linearDampingRatio = 0.7f;
		wd.base.bodyIdA = jbD;
		wd.base.bodyIdB = stackTop;
		wd.base.localFrameA.p = (b2Vec2){ 0.3f, 0.0f };
		wd.base.localFrameB.p = (b2Vec2){ 0.0f, 0.0f };
		b2CreateWeldJoint( worldId, &wd );
	}

	// Chain shape on a static body
	{
		b2BodyDef cbd = b2DefaultBodyDef();
		cbd.position = (b2Pos){ -10.0f, 0.0f };
		b2BodyId chainBodyId = b2CreateBody( worldId, &cbd );

		b2Vec2 chainPoints[3] = { { -2.0f, 0.0f }, { 0.0f, 0.0f }, { 2.0f, 0.0f } };
		b2SurfaceMaterial chainMat = b2DefaultSurfaceMaterial();
		chainMat.friction = 0.4f;
		b2ChainDef chainDef = b2DefaultChainDef();
		chainDef.points = chainPoints;
		chainDef.pointCount = 3;
		chainDef.ghost1 = (b2Vec2){ -4.0f, 0.0f };
		chainDef.ghost2 = (b2Vec2){ 4.0f, 2.0f };
		chainDef.materials = &chainMat;
		chainDef.materialCount = 1;
		chainDef.isLoop = false;
		heldChain = b2CreateChain( chainBodyId, &chainDef );
	}

	// Sensor on a static body, overlapping the scene area
	{
		b2BodyDef sbd = b2DefaultBodyDef();
		sbd.position = (b2Pos){ 0.0f, 5.0f };
		b2BodyId sensorBodyId = b2CreateBody( worldId, &sbd );

		b2Polygon sensorBox = b2MakeBox( 3.0f, 3.0f );
		b2ShapeDef sensorDef = b2DefaultShapeDef();
		sensorDef.isSensor = true;
		sensorDef.enableSensorEvents = true;
		b2CreatePolygonShape( sensorBodyId, &sensorDef, &sensorBox );
	}

	// Isolated second stack far from the main scene — will go to sleep independently
	{
		b2ShapeDef sd = b2DefaultShapeDef();
		b2Polygon box = b2MakeBox( 0.5f, 0.5f );
		for ( int i = 0; i < 6; ++i )
		{
			b2BodyDef bd = b2DefaultBodyDef();
			bd.type = b2_dynamicBody;
			bd.position = (b2Pos){ 40.0f, 0.5f + (float)i * 1.1f };
			b2BodyId bodyId = b2CreateBody( worldId, &bd );
			b2CreatePolygonShape( bodyId, &sd, &box );
		}
	}

	// A destroyed static shape leaves a free pair in the static tree. Nothing rebuilds the
	// static tree on its own, so the hole is still there at every snapshot below.
	{
		b2BodyDef bd = b2DefaultBodyDef();
		bd.position = (b2Pos){ -30.0f, 10.0f };
		b2BodyId scrapId = b2CreateBody( worldId, &bd );

		b2Polygon scrapBox = b2MakeBox( 0.5f, 0.5f );
		b2ShapeDef sd = b2DefaultShapeDef();
		b2CreatePolygonShape( scrapId, &sd, &scrapBox );
		b2DestroyBody( scrapId );
	}

	if ( outIds != NULL )
	{
		outIds->body = stackTop;
		outIds->shape = heldShape;
		outIds->joint = heldJoint;
		outIds->chain = heldChain;
	}

	return worldId;
}

// Step until both the main scene and the isolated stack have settled to sleep.
// Returns the step count used. Mirrors the settle-detect idiom from determinism.c.
static int StepUntilSleep( b2WorldId worldId )
{
	float dt = 1.0f / 60.0f;
	int subSteps = 4;
	int maxSteps = 500;

	for ( int step = 0; step < maxSteps; ++step )
	{
		b2World_Step( worldId, dt, subSteps );

		int awake = b2World_GetAwakeBodyCount( worldId );
		if ( awake == 0 )
		{
			return step + 1;
		}
	}
	return maxSteps;
}

// Box2D-Packed is single-world, so a restored world cannot run side by side with its origin.
// Instead the origin's per-step hashes are recorded, the origin is destroyed, and the restored
// world is checked against the recording. This is the same determinism guarantee as a lockstep.
typedef struct HashTrail
{
	uint64_t shallow[120];
	uint64_t deep[120];
	int count;
} HashTrail;

static void RecordTrail( b2WorldId worldId, HashTrail* trail, int steps, bool withDeep )
{
	b2World* world = b2GetWorldFromId( worldId );
	trail->count = steps;
	for ( int step = 0; step < steps; ++step )
	{
		b2World_Step( worldId, 1.0f / 60.0f, 4 );
		trail->shallow[step] = b2HashWorldState( world );
		trail->deep[step] = withDeep ? b2HashWorldStateDeep( world ) : 0;
	}
}

static bool MatchTrail( b2WorldId worldId, const HashTrail* trail, bool withDeep, const char* label )
{
	b2World* world = b2GetWorldFromId( worldId );
	for ( int step = 0; step < trail->count; ++step )
	{
		b2World_Step( worldId, 1.0f / 60.0f, 4 );
		uint64_t s = b2HashWorldState( world );
		if ( s != trail->shallow[step] )
		{
			printf( "%s: shallow hash mismatch at step %d (expected=%llu got=%llu)\n", label, step,
					(unsigned long long)trail->shallow[step], (unsigned long long)s );
			return false;
		}

		if ( withDeep )
		{
			uint64_t d = b2HashWorldStateDeep( world );
			if ( d != trail->deep[step] )
			{
				printf( "%s: deep hash mismatch at step %d (expected=%llu got=%llu)\n", label, step,
						(unsigned long long)trail->deep[step], (unsigned long long)d );
				return false;
			}
		}
	}
	return true;
}

// Free pairs are chained through the parent index of their first node. A broken restore shows up
// here and not in the hashes, which never read tree nodes. Captured so it can be compared after the
// origin world is gone.
#define MAX_FREE_PAIRS 1024
typedef struct TreeFreeImage
{
	int nodeEnd;
	int pairFreeList;
	int pairCount;
	int parents[2 * MAX_FREE_PAIRS];
} TreeFreeImage;

static void CaptureTreeFree( b2World* world, TreeFreeImage images[b2_bodyTypeCount] )
{
	for ( int treeType = 0; treeType < b2_bodyTypeCount; ++treeType )
	{
		const b2DynamicTree* tree = world->broadPhase.trees + treeType;
		TreeFreeImage* img = images + treeType;
		img->nodeEnd = tree->nodeEnd;
		img->pairFreeList = tree->pairFreeList;
		img->pairCount = 0;
		for ( int pair = tree->pairFreeList; pair != B2_NULL_INDEX && img->pairCount < MAX_FREE_PAIRS;
			  pair = tree->parents[pair] )
		{
			img->parents[2 * img->pairCount + 0] = tree->parents[pair];
			img->parents[2 * img->pairCount + 1] = tree->parents[pair + 1];
			img->pairCount += 1;
		}
	}
}

int SnapshotTest( void )
{
	float dt = 1.0f / 60.0f;
	int subSteps = 4;

	// Phase 1: build and settle worldA
	b2WorldId worldAId = BuildScene( 1, NULL );
	StepUntilSleep( worldAId );

	b2World* worldA = b2GetWorldFromId( worldAId );

	// The sleeping-set path (sets beyond the initial 3) must be exercised
	ENSURE( worldA->solverSets.count > 3 );

	// The free pair image only round trips if a tree actually has a hole
	ENSURE( worldA->broadPhase.trees[b2_staticBody].pairFreeList != B2_NULL_INDEX );

	// Serialize worldA
	b2RecBuffer buf = { 0 };
	b2SerializeWorld( worldA, &buf );
	ENSURE( buf.size > 0 );

	// Capture everything worldB will be compared against
	uint64_t hashA0 = b2HashWorldState( worldA );
	uint64_t deepA0 = b2HashWorldStateDeep( worldA );
	static TreeFreeImage treeImagesA[b2_bodyTypeCount];
	CaptureTreeFree( worldA, treeImagesA );

	static HashTrail trailA;
	RecordTrail( worldAId, &trailA, 120, true );

	// Serialize worldA after the trail for phase 4
	b2RecBuffer buf2 = { 0 };
	b2SerializeWorld( worldA, &buf2 );
	ENSURE( buf2.size > 0 );

	b2DestroyWorld( worldAId );
	ENSURE( b2World_IsValid( worldAId ) == false );

	// Phase 2: deserialize into worldB (same worker count)
	b2WorldId worldBId = b2CreateWorldFromSnapshot( buf.data, buf.size, 1 );
	ENSURE( b2World_IsValid( worldBId ) );

	b2World* worldB = b2GetWorldFromId( worldBId );

	// Immediate hash check — B must be identical to A at the snapshot instant
	ENSURE( b2HashWorldState( worldB ) == hashA0 );
	ENSURE( b2HashWorldStateDeep( worldB ) == deepA0 );

	{
		static TreeFreeImage treeImagesB[b2_bodyTypeCount];
		CaptureTreeFree( worldB, treeImagesB );
		for ( int treeType = 0; treeType < b2_bodyTypeCount; ++treeType )
		{
			const TreeFreeImage* a = treeImagesA + treeType;
			const TreeFreeImage* b = treeImagesB + treeType;
			ENSURE( a->nodeEnd == b->nodeEnd );
			ENSURE( a->pairFreeList == b->pairFreeList );
			ENSURE( a->pairCount == b->pairCount );
			ENSURE( memcmp( a->parents, b->parents, sizeof( int ) * 2 * a->pairCount ) == 0 );
		}
	}

	// Phase 3: worldB must reproduce worldA's recorded trail for 120 steps
	ENSURE( MatchTrail( worldBId, &trailA, true, "snapshot vs origin" ) );
	b2DestroyWorld( worldBId );

	// Phase 4: restore is worker-count independent. Rebuild worldA's later state at one worker,
	// record its trail, then rebuild the same bytes at four workers and match the trail.
	b2WorldId worldA1Id = b2CreateWorldFromSnapshot( buf2.data, buf2.size, 1 );
	ENSURE( b2World_IsValid( worldA1Id ) );
	uint64_t hashA1 = b2HashWorldState( b2GetWorldFromId( worldA1Id ) );
	static HashTrail trailA1;
	RecordTrail( worldA1Id, &trailA1, 120, false );
	b2DestroyWorld( worldA1Id );

	b2WorldId worldCId = b2CreateWorldFromSnapshot( buf2.data, buf2.size, 4 );
	ENSURE( b2World_IsValid( worldCId ) );
	ENSURE( b2HashWorldState( b2GetWorldFromId( worldCId ) ) == hashA1 );
	ENSURE( MatchTrail( worldCId, &trailA1, false, "one vs four workers" ) );
	b2DestroyWorld( worldCId );

	b2RecBufFree( &buf2 );

	// Phase 5: in-place restore keeps held ids working and rolls the world back exactly
	SnapshotIds ids;
	b2WorldId rId = BuildScene( 1, &ids );
	StepUntilSleep( rId );
	b2World* rWorld = b2GetWorldFromId( rId );

	// Producer: size query, then fill a caller-owned buffer
	int imageSize = b2World_GetSnapshot( rId, NULL, 0 );
	ENSURE( imageSize > 0 );
	uint8_t* image = b2Alloc( imageSize );
	int written = b2World_GetSnapshot( rId, image, imageSize );
	ENSURE( written == imageSize );

	uint64_t snapHash = b2HashWorldStateDeep( rWorld );

	// Diverge from the snapshot: push a held body and add a body that did not exist at the snapshot
	b2Body_SetLinearVelocity( ids.body, (b2Vec2){ 3.0f, 6.0f } );
	b2Body_SetAwake( ids.body, true );
	b2BodyDef postDef = b2DefaultBodyDef();
	postDef.type = b2_dynamicBody;
	postDef.position = (b2Pos){ 20.0f, 20.0f };
	b2BodyId postBody = b2CreateBody( rId, &postDef );
	for ( int step = 0; step < 30; ++step )
	{
		b2World_Step( rId, dt, subSteps );
	}
	ENSURE( b2HashWorldStateDeep( rWorld ) != snapHash );

	ENSURE( b2World_Restore( rId, image, imageSize ) );

	// Whole-world state rolled back to the snapshot instant
	ENSURE( b2HashWorldStateDeep( rWorld ) == snapHash );

	// The world id and every id held at the snapshot instant resolve again
	ENSURE( b2World_IsValid( rId ) );
	ENSURE( b2Body_IsValid( ids.body ) );
	ENSURE( b2Shape_IsValid( ids.shape ) );
	ENSURE( b2Joint_IsValid( ids.joint ) );
	ENSURE( b2Chain_IsValid( ids.chain ) );

	// An id minted after the snapshot is rejected, not aliased onto a different object
	ENSURE( b2Body_IsValid( postBody ) == false );

	// Phase 6: a rejected image leaves the world untouched
	uint64_t preBadHash = b2HashWorldStateDeep( rWorld );
	ENSURE( b2World_Restore( rId, NULL, 0 ) == false );
	uint8_t* corrupt = b2Alloc( imageSize );
	memcpy( corrupt, image, imageSize );
	corrupt[0] ^= 0xFF; // break the magic
	ENSURE( b2World_Restore( rId, corrupt, imageSize ) == false );
	ENSURE( b2HashWorldStateDeep( rWorld ) == preBadHash );
	b2Free( corrupt, imageSize );

	// The node capacity in the image is only an allocation hint. A huge one must not drive the
	// allocation, the live nodes bound it. Find the static tree record by the fields that survive
	// a restore exactly, the capacity itself may have been clamped.
	const b2DynamicTree* staticTree = rWorld->broadPhase.trees + b2_staticBody;
	int32_t treeHead = (int32_t)staticTree->nodeEnd;
	int32_t treeTail[5] = { (int32_t)staticTree->pairFreeList, (int32_t)staticTree->proxyCount,
							(int32_t)staticTree->proxyCapacity, (int32_t)staticTree->proxyFreeList,
							staticTree->dfsOrdered ? 1 : 0 };
	int treeOffset = -1;
	for ( int i = 0; i + 28 <= imageSize; ++i )
	{
		if ( memcmp( image + i, &treeHead, 4 ) == 0 && memcmp( image + i + 8, treeTail, 20 ) == 0 )
		{
			treeOffset = i;
			break;
		}
	}
	ENSURE( treeOffset >= 0 );

	uint8_t* greedy = b2Alloc( imageSize );
	memcpy( greedy, image, imageSize );
	int32_t hugeCapacity = INT32_MAX / 64;
	memcpy( greedy + treeOffset + 4, &hugeCapacity, 4 );
	ENSURE( b2World_Restore( rId, greedy, imageSize ) );
	ENSURE( b2HashWorldStateDeep( rWorld ) == snapHash );
	ENSURE( staticTree->nodeCapacity <= 2 * staticTree->nodeEnd );
	b2Free( greedy, imageSize );

	// Repeated in-place restore over the chain/sensor/island heap must not leak
	for ( int i = 0; i < 3; ++i )
	{
		ENSURE( b2World_Restore( rId, image, imageSize ) );
	}
	ENSURE( b2HashWorldStateDeep( rWorld ) == snapHash );

	// Done with the in-place world; Box2D-Packed can only hold one world at a time
	b2DestroyWorld( rId );

	// Phase 7: restore is worker-count independent. Load the one-worker image fresh at one
	// worker and record its trail, then restore it in place into a four-worker world and match.
	b2WorldId freshId = b2CreateWorldFromSnapshot( image, imageSize, 1 );
	ENSURE( b2World_IsValid( freshId ) );
	uint64_t freshHash = b2HashWorldState( b2GetWorldFromId( freshId ) );
	static HashTrail trailFresh;
	RecordTrail( freshId, &trailFresh, 120, false );
	b2DestroyWorld( freshId );

	b2WorldDef def4 = b2DefaultWorldDef();
	def4.workerCount = 4;
	b2WorldId sId = b2CreateWorld( &def4 );
	ENSURE( b2World_Restore( sId, image, imageSize ) );
	ENSURE( b2HashWorldState( b2GetWorldFromId( sId ) ) == freshHash );
	ENSURE( MatchTrail( sId, &trailFresh, false, "in-place vs fresh" ) );
	b2DestroyWorld( sId );

	b2Free( image, imageSize );

	// Phase 8: mid-stream recording into a file, then deterministic replay. b2World_StartRecording
	// writes a snapshot of the live world, the file then continues with the hook log. Replay passing
	// proves the deserialized snapshot reproduced the live world for every recorded step.
	{
		b2WorldId wId = BuildScene( 1, NULL );

		// Snapshot mid-motion so the recorded tail exercises moving bodies, not a settled world
		for ( int step = 0; step < 20; ++step )
		{
			b2World_Step( wId, dt, subSteps );
		}

		b2Recording* rec = b2CreateRecording( 0 );
		b2World_StartRecording( wId, rec );
		for ( int step = 0; step < 60; ++step )
		{
			b2World_Step( wId, dt, subSteps );
		}
		b2World_StopRecording( wId );
		b2DestroyWorld( wId );

		const uint8_t* recData = b2Recording_GetData( rec );
		int recSize = b2Recording_GetSize( rec );
		ENSURE( b2ValidateReplay( recData, recSize, 0 ) );
		ENSURE( b2ValidateReplay( recData, recSize, 4 ) );

		// File round-trip: save the buffer, load it back, and replay the loaded copy
		ENSURE( b2SaveRecordingToFile( rec, s_snapPath ) );
		b2Recording* loaded = b2LoadRecordingFromFile( s_snapPath );
		ENSURE( loaded != NULL );
		ENSURE( b2ValidateReplay( b2Recording_GetData( loaded ), b2Recording_GetSize( loaded ), 0 ) );
		b2DestroyRecording( loaded );

		// The player opens the recording and the replay world id is stable across a restart
		b2Replay* player = b2CreateReplay( recData, recSize, 0 );
		ENSURE( player != NULL );
		b2WorldId pid0 = b2Replay_GetWorldId( player );

		int frames = 0;
		while ( b2Replay_StepFrame( player ) )
		{
			frames += 1;
		}
		ENSURE( frames == 60 );
		ENSURE( b2Replay_HasDiverged( player ) == false );

		b2Replay_Restart( player );
		b2WorldId pid1 = b2Replay_GetWorldId( player );
		ENSURE( pid0.index1 == pid1.index1 && pid0.generation == pid1.generation );
		ENSURE( b2Replay_GetFrame( player ) == 0 );

		int frames2 = 0;
		while ( b2Replay_StepFrame( player ) )
		{
			frames2 += 1;
		}
		ENSURE( frames2 == 60 );
		ENSURE( b2Replay_HasDiverged( player ) == false );

		b2DestroyReplay( player );
		b2DestroyRecording( rec );
	}

	// Phase 9: snapshot-equals-real. A world built from a mid-stream snapshot must reproduce the
	// origin world's future step for step, not merely be internally self-consistent.
	{
		b2WorldId wId = BuildScene( 1, NULL );

		// Snapshot mid-motion so the compared tail has real dynamics
		for ( int step = 0; step < 20; ++step )
		{
			b2World_Step( wId, dt, subSteps );
		}

		int snapSize = b2World_GetSnapshot( wId, NULL, 0 );
		uint8_t* snap = b2Alloc( snapSize );
		b2World_GetSnapshot( wId, snap, snapSize );

		b2World* origin = b2GetWorldFromId( wId );

		enum
		{
			tailSteps = 90
		};
		uint64_t realTail[tailSteps];
		for ( int step = 0; step < tailSteps; ++step )
		{
			b2World_Step( wId, dt, subSteps );
			realTail[step] = b2HashWorldState( origin );
		}

		b2DestroyWorld( wId );

		b2WorldId cId = b2CreateWorldFromSnapshot( snap, snapSize, 1 );
		ENSURE( b2World_IsValid( cId ) );
		b2World* clone = b2GetWorldFromId( cId );
		for ( int step = 0; step < tailSteps; ++step )
		{
			b2World_Step( cId, dt, subSteps );
			if ( realTail[step] != b2HashWorldState( clone ) )
			{
				printf( "snapshot-equals-real mismatch at tail step %d\n", step );
				ENSURE( false );
			}
		}

		b2Free( snap, snapSize );
		b2DestroyWorld( cId );
	}

	// Phase 10: a recording started before the first step also restarts in place, with a stable
	// replay world id
	{
		b2WorldDef wd = b2DefaultWorldDef();
		wd.workerCount = 1;
		b2WorldId wId = b2CreateWorld( &wd );

		b2Recording* rec = b2CreateRecording( 0 );
		b2World_StartRecording( wId, rec );

		b2BodyDef bd = b2DefaultBodyDef();
		bd.type = b2_dynamicBody;
		bd.position = (b2Pos){ 0.0f, 8.0f };
		b2BodyId fallingBody = b2CreateBody( wId, &bd );
		b2Polygon box = b2MakeBox( 0.5f, 0.5f );
		b2ShapeDef sdef = b2DefaultShapeDef();
		b2CreatePolygonShape( fallingBody, &sdef, &box );

		for ( int step = 0; step < 30; ++step )
		{
			b2World_Step( wId, dt, subSteps );
		}
		b2World_StopRecording( wId );
		b2DestroyWorld( wId );

		b2Replay* player = b2CreateReplay( b2Recording_GetData( rec ), b2Recording_GetSize( rec ), 0 );
		ENSURE( player != NULL );
		b2WorldId pid0 = b2Replay_GetWorldId( player );
		for ( int i = 0; i < 5; ++i )
		{
			b2Replay_StepFrame( player );
		}
		b2Replay_Restart( player );
		b2WorldId pid1 = b2Replay_GetWorldId( player );
		ENSURE( pid0.index1 == pid1.index1 && pid0.generation == pid1.generation );
		ENSURE( b2Replay_GetFrame( player ) == 0 );
		b2DestroyReplay( player );
		b2DestroyRecording( rec );
	}

	// Phase 11: restore clears the event buffers. End events queued by a between-step mutator
	// must not survive a rollback and surface after the first resimmed step.
	{
		b2WorldDef wd = b2DefaultWorldDef();
		wd.workerCount = 1;
		b2WorldId wId = b2CreateWorld( &wd );
		b2World* w = b2GetWorldFromId( wId );

		b2BodyDef gbd = b2DefaultBodyDef();
		gbd.position = (b2Pos){ 0.0f, -1.0f };
		b2BodyId ground = b2CreateBody( wId, &gbd );
		b2Polygon groundBox = b2MakeBox( 10.0f, 1.0f );
		b2ShapeDef gsd = b2DefaultShapeDef();
		gsd.enableContactEvents = true;
		b2CreatePolygonShape( ground, &gsd, &groundBox );

		b2BodyDef bd = b2DefaultBodyDef();
		bd.type = b2_dynamicBody;
		bd.position = (b2Pos){ 0.0f, 0.5f };
		b2BodyId box = b2CreateBody( wId, &bd );
		b2Polygon boxPoly = b2MakeBox( 0.5f, 0.5f );
		b2ShapeDef bsd = b2DefaultShapeDef();
		bsd.enableContactEvents = true;
		b2CreatePolygonShape( box, &bsd, &boxPoly );

		// Settle so the box rests on the ground with a live touching contact
		for ( int step = 0; step < 60; ++step )
		{
			b2World_Step( wId, dt, subSteps );
		}

		// The public state hash accessor must match the internal deep hash
		ENSURE( b2World_GetStateHash( wId ) == b2HashWorldStateDeep( w ) );

		int snapSize = b2World_GetSnapshot( wId, NULL, 0 );
		uint8_t* snap = b2Alloc( snapSize );
		b2World_GetSnapshot( wId, snap, snapSize );

		// Disabling the resting box destroys its touching contact and queues an end event,
		// exactly the between-step mutation a rollback would discard
		b2Body_Disable( box );
		ENSURE( w->contactEndEvents[w->endEventArrayIndex].count >= 1 );

		ENSURE( b2World_Restore( wId, snap, snapSize ) );

		// Restore must leave no queued end events in either half of the double buffer
		ENSURE( w->contactEndEvents[0].count == 0 );
		ENSURE( w->contactEndEvents[1].count == 0 );

		// The resimmed step keeps the box resting, so no end event should reach the user
		b2World_Step( wId, dt, subSteps );
		b2ContactEvents events = b2World_GetContactEvents( wId );
		ENSURE( events.endCount == 0 );

		b2Free( snap, snapSize );
		b2DestroyWorld( wId );
	}

	remove( s_snapPath );

	// Clean up. Every world was destroyed at the end of its phase.
	b2RecBufFree( &buf );

	return 0;
}
