[CmdletBinding()]
param(
    [string]$IntegrationRoot,
    [string]$ManifestPath,
    [switch]$ListOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($IntegrationRoot)) {
    $rootResult = @(& git rev-parse --show-toplevel 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "Current directory is not an ABTS Git worktree.`n$($rootResult -join [Environment]::NewLine)"
    }
    $IntegrationRoot = "$($rootResult[0])"
}
$IntegrationRoot = (Resolve-Path -LiteralPath $IntegrationRoot).Path
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $IntegrationRoot 'Tools\ABTSV3IntegrationGateManifest.json'
}
$ManifestPath = (Resolve-Path -LiteralPath $ManifestPath).Path
$manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding utf8 | ConvertFrom-Json

$project = Join-Path $IntegrationRoot 'AngryBirdsToSpace.uproject'
$editorCmd = Join-Path $manifest.engineRoot 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $project -PathType Leaf)) {
    throw "Project is missing: $project"
}
if (-not (Test-Path -LiteralPath $editorCmd -PathType Leaf)) {
    throw "Required UE 5.8 UnrealEditor-Cmd is missing: $editorCmd"
}
$gatePhase = 'Prepared'
if ($manifest.status -eq 'IntegrationV3DTOPublishedMapFreezePending') {
    if ($manifest.activationAllowed -or $manifest.productionContractVersion -ne 2) {
        throw 'Prepared gates require activationAllowed=false and productionContractVersion=2.'
    }
}
elseif ($manifest.status -eq 'MapFreezeV3PublishedChaosFreezePending') {
    $gatePhase = 'Published'
    if (-not $manifest.activationAllowed -or $manifest.productionContractVersion -ne 3) {
        throw 'Published Map Freeze gates require activationAllowed=true and productionContractVersion=3.'
    }
    if ([string]::IsNullOrWhiteSpace($manifest.frozenIdentities.mapFreezeV3Commit) -or
        [string]::IsNullOrWhiteSpace($manifest.frozenIdentities.layoutHash)) {
        throw 'Published Map Freeze gates require exact M3 commit and Layout hash identities.'
    }
}
else {
    throw "Unsupported V3 gate-manifest status: $($manifest.status)"
}

$gates = @($manifest.currentRunnableGates)
if ($gates.Count -eq 0) {
    throw 'Manifest contains no currentRunnableGates.'
}
$duplicateFilters = @($gates | Group-Object filter | Where-Object Count -ne 1)
if ($duplicateFilters.Count -gt 0) {
    throw "Manifest contains duplicate filters: $($duplicateFilters.Name -join ', ')"
}
foreach ($gate in $gates) {
    if ([string]::IsNullOrWhiteSpace($gate.filter) -or $gate.expectedTests -le 0) {
        throw "Invalid runnable gate: $($gate | ConvertTo-Json -Compress)"
    }
    Write-Output "GATE Filter=$($gate.filter) Expected=$($gate.expectedTests) Evidence=$($gate.evidenceLayer)"
}
if ($ListOnly) {
    Write-Output "SUMMARY Listed=$($gates.Count) Executed=0"
    exit 0
}

$logDirectory = Join-Path $IntegrationRoot 'Saved\Logs'
if (-not (Test-Path -LiteralPath $logDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $logDirectory | Out-Null
}

$results = [System.Collections.Generic.List[object]]::new()
foreach ($gate in $gates) {
    $runId = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $safeFilter = $gate.filter -replace '[^A-Za-z0-9._-]', '_'
    $log = Join-Path $logDirectory "V3-$gatePhase-$safeFilter-$runId-FreshAutomation.log"
    $arguments = @(
        $project,
        '-unattended',
        '-nop4',
        '-NullRHI',
        '-NoSound',
        '-NoMessaging',
        "-ExecCmds=Automation RunTests $($gate.filter);Quit",
        '-TestExit=Automation Test Queue Empty',
        "-AbsLog=$log"
    )
    Write-Output "RUN Filter=$($gate.filter) Log=$log"
    & $editorCmd @arguments
    $processExitCode = $LASTEXITCODE
    if ($processExitCode -ne 0) {
        throw "Automation process failed: Filter=$($gate.filter) ExitCode=$processExitCode Log=$log"
    }
    if (-not (Test-Path -LiteralPath $log -PathType Leaf)) {
        throw "Automation log is missing: Filter=$($gate.filter) Log=$log"
    }
    $completionCount = @(Select-String -LiteralPath $log -SimpleMatch '**** TEST COMPLETE. EXIT CODE: 0 ****').Count
    $passedCount = @(Select-String -LiteralPath $log -SimpleMatch 'Test Completed. Result={Success}').Count
    $failedCount = @(Select-String -LiteralPath $log -SimpleMatch 'Test Completed. Result={Fail}').Count
    if ($completionCount -ne 1) {
        throw "Automation completion marker mismatch: Filter=$($gate.filter) Count=$completionCount Log=$log"
    }
    if ($passedCount -ne $gate.expectedTests -or $failedCount -ne 0) {
        throw "Automation result mismatch: Filter=$($gate.filter) Expected=$($gate.expectedTests) Passed=$passedCount Failed=$failedCount Log=$log"
    }
    $results.Add([pscustomobject]@{
        Filter = $gate.filter
        Expected = $gate.expectedTests
        Passed = $passedCount
        Log = $log
    })
    Write-Output "PASS Filter=$($gate.filter) Passed=$passedCount Log=$log"
}

Write-Output "SUMMARY Listed=$($gates.Count) Executed=$($results.Count) Passed=$($results.Count)"
exit 0
