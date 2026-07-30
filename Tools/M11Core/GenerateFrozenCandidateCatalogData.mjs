// Copyright Epic Games, Inc. All Rights Reserved.
//
// Emits the C++ layout builders used by the Editor-only PIE Candidate catalog.
// Input files are authoritative particle-beam candidate manifests.

import fs from "node:fs";

function number(value) {
  if (!Number.isFinite(value)) {
    throw new Error(`Non-finite numeric value: ${value}`);
  }
  return Number.isInteger(value) ? `${value}.0` : value.toPrecision(17);
}

function integer(value) {
  if (!Number.isInteger(value)) {
    throw new Error(`Expected integer: ${value}`);
  }
  return `${value}`;
}

function boolean(value) {
  return value ? "true" : "false";
}

function vec(value) {
  return `Vec3d(${number(value[0])}, ${number(value[1])}, ${number(value[2])})`;
}

function color(value) {
  return `Color4f{${number(value[0])}f, ${number(value[1])}f, ${number(value[2])}f, ${number(value[3])}f}`;
}

function hex64(value) {
  if (!/^0x[0-9a-f]{16}$/i.test(value)) {
    throw new Error(`Expected uint64 hex identity: ${value}`);
  }
  return `${value.toLowerCase()}ull`;
}

function emitBody(lines, body, index) {
  const prefix = `Layout.Scenario.Bodies[${index}]`;
  lines.push(`\t${prefix}.BodyId = ${integer(body.bodyId)};`);
  lines.push(`\t${prefix}.Role = static_cast<GravityRole>(${integer(body.role)});`);
  lines.push(`\t${prefix}.CenterCM = ${vec(body.centerCM)};`);
  lines.push(`\t${prefix}.GravitationalParameterCM3PerSec2 = ${number(body.gravitationalParameterCM3PerSec2)};`);
  lines.push(`\t${prefix}.MinimumEvaluationRadiusCM = ${number(body.minimumEvaluationRadiusCM)};`);
  lines.push(`\t${prefix}.VisualRadiusCM = ${number(body.visualRadiusCM)};`);
  lines.push(`\t${prefix}.CollisionRadiusCM = ${number(body.collisionRadiusCM)};`);
  lines.push(`\t${prefix}.MaximumSimulationRadiusCM = ${number(body.maximumSimulationRadiusCM)};`);
  lines.push(`\t${prefix}.InfluenceRadiusCM = ${number(body.influenceRadiusCM)};`);
  lines.push(`\t${prefix}.AssistReferenceRadiusCM = ${number(body.assistReferenceRadiusCM)};`);
  lines.push(`\t${prefix}.InfluenceBlendWidthCM = ${number(body.influenceBlendWidthCM)};`);
  lines.push(`\t${prefix}.VirtualOrbitalVelocityCMPerSec = ${vec(body.virtualOrbitalVelocityCMPerSec)};`);
  lines.push(`\t${prefix}.BPlaneReferenceNormal = ${vec(body.bPlaneReferenceNormal)};`);
  lines.push(`\t${prefix}.BPlaneFallbackAxis = ${vec(body.bPlaneFallbackAxis)};`);
  lines.push(`\t${prefix}.BPlaneTargetTCM = ${number(body.bPlaneTargetTCM)};`);
  lines.push(`\t${prefix}.BPlaneTargetRCM = ${number(body.bPlaneTargetRCM)};`);
  lines.push(`\t${prefix}.BPlaneSigmaTCM = ${number(body.bPlaneSigmaTCM)};`);
  lines.push(`\t${prefix}.BPlaneSigmaRCM = ${number(body.bPlaneSigmaRCM)};`);
  lines.push(`\t${prefix}.BPlaneOuterChiSquared = ${number(body.bPlaneOuterChiSquared)};`);
  lines.push(`\t${prefix}.AllowedPassSideValue = static_cast<AllowedPassSide>(${integer(body.allowedPassSide)});`);
  lines.push(`\t${prefix}.MinimumEnergyChangeCM2PerSec2 = ${number(body.minimumEnergyChangeCM2PerSec2)};`);
  lines.push(`\t${prefix}.MaximumEnergyChangeCM2PerSec2 = ${number(body.maximumEnergyChangeCM2PerSec2)};`);
  lines.push(`\t${prefix}.DebugColor = ${color(body.debugColor)};`);
}

function emitLayout(manifest, rank) {
  const candidate = manifest.evaluatedCandidate;
  if (!candidate?.layout) {
    throw new Error("Manifest has no evaluatedCandidate.layout");
  }
  const layout = candidate.layout;
  const launch = layout.launch;
  const scenario = layout.scenario;
  const target = scenario.target;
  const solver = layout.solver;
  const lines = [];
  lines.push(`CandidateLayout MakeFrozenV4LayoutRank${rank}()`);
  lines.push("{");
  lines.push("\tCandidateLayout Layout;");
  lines.push(`\tLayout.LayoutVersion = ${integer(layout.layoutVersion)};`);
  lines.push(`\tLayout.Launch.Version = ${integer(launch.version)};`);
  lines.push(`\tLayout.Launch.PouchLocalPositionCM = ${vec(launch.pouchLocalPositionCM)};`);
  for (const [name, value] of [
    ["MinimumYawDegrees", launch.minimumYawDegrees],
    ["MaximumYawDegrees", launch.maximumYawDegrees],
    ["MinimumPitchDegrees", launch.minimumPitchDegrees],
    ["MaximumPitchDegrees", launch.maximumPitchDegrees],
    ["MinimumPower", launch.minimumPower],
    ["MaximumPower", launch.maximumPower],
    ["MinimumLaunchSpeedCMPerSec", launch.minimumLaunchSpeedCMPerSec],
    ["MaximumLaunchSpeedCMPerSec", launch.maximumLaunchSpeedCMPerSec],
    ["MaximumSimulationTimeSeconds", launch.maximumSimulationTimeSeconds],
  ]) {
    lines.push(`\tLayout.Launch.${name} = ${number(value)};`);
  }
  lines.push(`\tLayout.NominalInput.YawDegrees = ${number(layout.nominalInput.yawDegrees)};`);
  lines.push(`\tLayout.NominalInput.PitchDegrees = ${number(layout.nominalInput.pitchDegrees)};`);
  lines.push(`\tLayout.NominalInput.Power = ${number(layout.nominalInput.power)};`);
  lines.push(`\tLayout.Scenario.LayoutVersion = ${integer(scenario.layoutVersion)};`);
  lines.push(`\tLayout.Scenario.ScenarioHash = ${integer(scenario.scenarioHash)}u;`);
  scenario.bodies.forEach((body, index) => emitBody(lines, body, index));
  lines.push(`\tLayout.Scenario.Target.TargetId = ${integer(target.targetId)};`);
  lines.push(`\tLayout.Scenario.Target.CenterCM = ${vec(target.centerCM)};`);
  lines.push(`\tLayout.Scenario.Target.HitRadiusCM = ${number(target.hitRadiusCM)};`);
  lines.push(`\tLayout.Scenario.Target.GeometricContactRadiusCM = ${number(target.geometricContactRadiusCM)};`);
  lines.push(`\tLayout.Scenario.Target.UseSeparateGeometricContactCenter = ${boolean(target.useSeparateGeometricContactCenter)};`);
  lines.push(`\tLayout.Scenario.Target.GeometricContactCenterCM = ${vec(target.geometricContactCenterCM)};`);
  lines.push(`\tLayout.Scenario.Target.RequiredQualifiedAssistCount = ${integer(target.requiredQualifiedAssistCount)};`);
  lines.push(`\tLayout.Scenario.Target.MinimumQualifyingCorridorQuality = ${number(target.minimumQualifyingCorridorQuality)};`);
  lines.push(`\tLayout.Scenario.Target.MinimumQualifyingEnergyGainCM2PerSec2 = ${number(target.minimumQualifyingEnergyGainCM2PerSec2)};`);
  lines.push(`\tLayout.Scenario.Target.RequireAllowedPassSide = ${boolean(target.requireAllowedPassSide)};`);
  lines.push(`\tLayout.Scenario.Target.PresentationForward = ${vec(target.presentationForward)};`);
  for (const [name, value, kind] of [
    ["SolverVersion", solver.solverVersion, "int"],
    ["HashSchemaVersion", solver.hashSchemaVersion, "int"],
    ["FixedTimeStepSeconds", solver.fixedTimeStepSeconds, "number"],
    ["MaximumSimulationTimeSeconds", solver.maximumSimulationTimeSeconds, "number"],
    ["MaximumStepCount", solver.maximumStepCount, "int"],
    ["MaximumSubdivisionDepth", solver.maximumSubdivisionDepth, "int"],
    ["MaximumCoastStepExpansionDepth", solver.maximumCoastStepExpansionDepth, "int"],
    ["AssistStepRadiusFraction", solver.assistStepRadiusFraction, "number"],
    ["CollisionStepRadiusFraction", solver.collisionStepRadiusFraction, "number"],
    ["GravityTimescaleFraction", solver.gravityTimescaleFraction, "number"],
    ["PositionErrorLimitCM", solver.positionErrorLimitCM, "number"],
    ["RootBisectionIterations", solver.rootBisectionIterations, "int"],
    ["RootAlphaTolerance", solver.rootAlphaTolerance, "number"],
    ["BPlaneBasisMinimumLength", solver.bPlaneBasisMinimumLength, "number"],
    ["MinimumVInfinityCMPerSec", solver.minimumVInfinityCMPerSec, "number"],
    ["MaximumNaturalDeflectionErrorRadians", solver.maximumNaturalDeflectionErrorRadians, "number"],
    ["EnergyQualityPower", solver.energyQualityPower, "number"],
    ["EnergyRootEpsilonCM2PerSec2", solver.energyRootEpsilonCM2PerSec2, "number"],
    ["ExitEnergyResidualToleranceCM2PerSec2", solver.exitEnergyResidualToleranceCM2PerSec2, "number"],
    ["EnergyShootingIterationCount", solver.energyShootingIterationCount, "int"],
    ["NaturalCloneMaximumTimeSeconds", solver.naturalCloneMaximumTimeSeconds, "number"],
    ["NaturalCloneMaximumStepCount", solver.naturalCloneMaximumStepCount, "int"],
  ]) {
    lines.push(`\tLayout.Solver.${name} = ${kind === "int" ? integer(value) : number(value)};`);
  }
  lines.push(`\tLayout.Solver.EnabledAssistMask = ${integer(solver.enabledAssistMask)}u;`);
  lines.push("\treturn Layout;");
  lines.push("}");
  return {
    code: lines.join("\n"),
    identity: {
      rank,
      globalWorkIndex: candidate.globalWorkIndex,
      candidateSourceHash: hex64(candidate.candidateSourceHash),
      nominalRequestHash: hex64(candidate.nominalRequestHash),
      nominalResultHash: hex64(candidate.nominalResultHash),
      scoreHash: hex64(candidate.scoreHash),
    },
  };
}

if (process.argv.length < 4 || process.argv.length % 2 !== 0) {
  throw new Error("Usage: node GenerateFrozenCandidateCatalogData.mjs <rank> <manifest> [...]");
}

const generated = [];
for (let index = 2; index < process.argv.length; index += 2) {
  const rank = Number.parseInt(process.argv[index], 10);
  const manifest = JSON.parse(fs.readFileSync(process.argv[index + 1], "utf8"));
  generated.push(emitLayout(manifest, rank));
}

console.log("// Generated by Tools/M11Core/GenerateFrozenCandidateCatalogData.mjs.");
console.log("// Candidate / NOT CERTIFIED. Editor-only PIE comparison data.");
console.log("");
console.log("using ABTS::M11Core::AllowedPassSide;");
console.log("using ABTS::M11Core::Color4f;");
console.log("using ABTS::M11Core::GravityRole;");
console.log("using ABTS::M11Core::Vec3d;");
console.log("");
for (const entry of generated) {
  console.log(entry.code);
  console.log("");
}
console.log("bool BuildFrozenV4Layout(");
console.log("\tconst int32 CandidateRank,");
console.log("\tCandidateLayout& OutLayout)");
console.log("{");
console.log("\tswitch (CandidateRank)");
console.log("\t{");
for (const entry of generated) {
  console.log(`\tcase ${entry.identity.rank}: OutLayout = MakeFrozenV4LayoutRank${entry.identity.rank}(); return true;`);
}
console.log("\tdefault: return false;");
console.log("\t}");
console.log("}");
console.log("");
console.error(JSON.stringify(generated.map((entry) => entry.identity), null, 2));
