// Copyright Epic Games, Inc. All Rights Reserved.

#include "Building/ABTSM73DAGBuildingPipeline.h"

#include "Building/ABTSM73DAGGrammarExpander.h"
#include "Building/ABTSM73DAGLayoutSolver.h"
#include "Building/ABTSM73DAGModuleCompiler.h"
#include "Building/ABTSM73DAGTypes.h"
#include "Building/ABTSM73StructureData.h"

bool FABTSM73DAGBuildingPipeline::Build(
	const FABTSM73DAGGenerationSettings& DAGSettings,
	const FABTSM73DAGLayoutSettings& LayoutSettings,
	const FABTSM73GenerationSettings& BuildingSettings,
	FABTSM73StructureData& OutData,
	FString& OutError) const
{
	FABTSM73DAGGrammarExpander Expander;
	FABTSM73DAGGenerationResult Graph;
	if (!Expander.Generate(DAGSettings, Graph, OutError)) return false;
	FABTSM73DAGLayoutSolver LayoutSolver;
	FABTSM73DAGSpatialLayout Layout;
	if (!LayoutSolver.Solve(Graph, LayoutSettings, Layout, OutError)) return false;
	FABTSM73DAGModuleCompiler Compiler;
	return Compiler.Compile(BuildingSettings, Graph, LayoutSettings, Layout, OutData, OutError);
}
