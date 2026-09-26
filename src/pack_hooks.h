// SPDX-FileCopyrightText: 2026 Box2D-Packed contributors
// SPDX-License-Identifier: MIT

#pragma once

// Box2D-Packed injection hooks shared by several source files. The marker comments are inert in
// normal builds. box2d_pack.py replaces them with user code, see INTRUSIVENGINE.md.

#include "shape.h"

// File scope in every engine file that has markers. Declarations only: declare your functions and
// extern variables here. A definition would be duplicated in each file.
//$pack_hooks$GLOBALS

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
