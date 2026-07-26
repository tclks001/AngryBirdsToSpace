// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"

/**
 * Object channel for static world obstacles that can be bypassed by the M9
 * developer-walk option. The continuous planet surface intentionally remains
 * WorldStatic, so ignoring this channel never removes ground support.
 */
constexpr ECollisionChannel ABTSDeveloperObstacleChannel = ECC_GameTraceChannel1;
