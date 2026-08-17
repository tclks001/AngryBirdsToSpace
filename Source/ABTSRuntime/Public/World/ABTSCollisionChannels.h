// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"

/**
 * Object channel for static world obstacles that can be bypassed by the M9
 * developer-walk option. The continuous planet surface intentionally remains
 * WorldStatic, so ignoring this channel never removes ground support.
 */
constexpr ECollisionChannel ABTSDeveloperObstacleChannel = ECC_GameTraceChannel1;

/**
 * Walking-only world barriers such as the generated river air wall. These are
 * deliberately separate from buildings: launched birds ignore this channel
 * while continuing to block ABTSDeveloperObstacle and damage M7 modules.
 */
constexpr ECollisionChannel ABTSWalkBarrierChannel = ECC_GameTraceChannel2;
