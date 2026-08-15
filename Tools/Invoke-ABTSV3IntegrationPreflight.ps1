[CmdletBinding()]
param(
    [string]$IntegrationRoot,
    [string]$ManifestPath,
    [string]$M3Commit,
    [string]$M7Commit,
    [string]$M11Commit,
    [switch]$FailOnWarning
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-CheckedGit {
    param(
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [switch]$AllowFailure
    )

    $output = @(& git -C $Root @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0 -and -not $AllowFailure) {
        throw "git $($Arguments -join ' ') failed with exit code $exitCode`n$($output -join [Environment]::NewLine)"
    }
    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = @($output | ForEach-Object { "$_" })
    }
}

function Get-RequiredBranchOwner {
    param([Parameter(Mandatory = $true)][string]$Path)

    $normalized = $Path.Replace('\', '/')
    if ($normalized -eq 'Source/ABTSRuntime/Private/Terrain/ABTSM3WorldContractAdapter.cpp' -or
        $normalized -match '^Source/ABTSRuntime/(Public|Private)/Contracts/' -or
        $normalized -match '^Source/ABTSRuntime/(Public|Private)/World/ABTSM110' -or
        $normalized -match '^Source/ABTSRuntime/(Public|Private)/Slingshot/ABTSM6' -or
        $normalized -match '^Source/ABTSRuntime/Private/World/ABTSM51' -or
        $normalized -match '^Source/ABTSRuntime/(Public|Private)/World/ABTSM8' -or
        $normalized -match '^Source/ABTSRuntime/Private/Game/ABTSM51GameMode\.cpp$' -or
        $normalized -match '^Source/ABTSRuntime/(Public|Private)/(Crafting|Inventory)/' -or
        $normalized -match '^Config/' -or
        $normalized -match '\.(Build|Target)\.cs$' -or
        $normalized -match '\.uproject$' -or
        $normalized -eq 'AGENTS.md' -or
        $normalized -eq 'Docs/ABTSMultiWorktreeDevelopmentGuide.md' -or
        $normalized -eq 'Docs/ABTSProjectWorkflow.md' -or
        $normalized -eq 'Docs/DevelopmentTroubleshooting.md' -or
        $normalized -eq 'Content/Maps/Test.umap') {
        return 'Integration'
    }
    if ($normalized -match '^Source/ABTSRuntime/(Public|Private)/(PCG|Terrain)/ABTSM3' -or
        $normalized -match '^Source/ABTSRuntime/(Public|Private)/Game/ABTSM3GameMode\.(h|cpp)$' -or
        $normalized -match '^Docs/(ABTSTaskGraphPCGDesign|M3PCGMapImprovementPlan|M3TaskGraphTerrainPresentationDesign|M3WorktreeTroubleshootingLog|M3JuryDemoFixedSixIntegrationPlan)\.md$' -or
        $normalized -match '^Content/M3/' -or
        $normalized -match '^Content/(Blueprints/BP_ABTSM3Planet\.uasset|Maps/L_ABTS_M3\.umap|Materials/M_ABTS_M3_SDFTerrain\.uasset)$') {
        return 'M3'
    }
    if ($normalized -match '^Source/ABTSRuntime/(Public|Private)/Building/ABTSM7' -or
        $normalized -match '^Source/ABTSRuntime/(Public|Private)/Building/ABTSM73' -or
        $normalized -match '^Source/ABTSRuntime/(Public|Private)/Game/ABTSM7' -or
        $normalized -match '^Source/ABTSRuntime/(Public|Private)/TestStage/ABTSM71' -or
        $normalized -match '^Docs/M7' -or
        $normalized -match '^Docs/M71' -or
        $normalized -match '^Docs/M73' -or
        $normalized -match '^Content/M7/' -or
        $normalized -match '^Content/StaticMesh/BrickMaterials/' -or
        $normalized -match '^Content/(Blueprints/BP_ABTSM7|Blueprints/BP_ABTSM71|Maps/L_ABTS_M7\.umap|Maps/PlanarPhysicsTestMap\.umap)') {
        return 'M7'
    }
    if (($normalized -match '^Source/ABTSRuntime/(Public|Private)/(Game|World)/ABTSM11' -and
            $normalized -notmatch '/ABTSM110') -or
        $normalized -match '^Source/ABTSRuntime/(Public|Private)/(UI|Player|Slingshot)/ABTSM11' -or
        ($normalized -match '^Docs/M11' -and $normalized -notmatch '^Docs/M110') -or
        $normalized -match '^Content/M11/' -or
        $normalized -match '^Content/StaticMesh/UFO/' -or
        $normalized -match '^Content/Destruction/GeometryCollections/(BP_UFOPresentation|GC_UFO_Broken)\.uasset$' -or
        $normalized -match '^Content/(Blueprints/BP_ABTSM11GameMode\.uasset|Maps/L_ABTS_M11\.umap)$') {
        return 'M11'
    }
    return 'Unclassified'
}

function Get-WorktreeRecords {
    param([Parameter(Mandatory = $true)][string]$Root)

    $lines = (Invoke-CheckedGit -Root $Root -Arguments @('worktree', 'list', '--porcelain')).Output
    $records = [System.Collections.Generic.List[object]]::new()
    $current = $null
    foreach ($line in @($lines) + '') {
        if ([string]::IsNullOrWhiteSpace($line)) {
            if ($null -ne $current) {
                $records.Add([pscustomobject]$current)
                $current = $null
            }
            continue
        }
        if ($line.StartsWith('worktree ')) {
            $current = [ordered]@{
                Path = $line.Substring(9)
                Head = ''
                Branch = ''
            }
        }
        elseif ($line.StartsWith('HEAD ')) {
            $current.Head = $line.Substring(5)
        }
        elseif ($line.StartsWith('branch refs/heads/')) {
            $current.Branch = $line.Substring(18)
        }
    }
    return @($records)
}

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

$errors = [System.Collections.Generic.List[string]]::new()
$warnings = [System.Collections.Generic.List[string]]::new()
$notes = [System.Collections.Generic.List[string]]::new()

$topLevel = ((Invoke-CheckedGit -Root $IntegrationRoot -Arguments @('rev-parse', '--show-toplevel')).Output -join '').Trim()
$branch = ((Invoke-CheckedGit -Root $IntegrationRoot -Arguments @('branch', '--show-current')).Output -join '').Trim()
$status = (Invoke-CheckedGit -Root $IntegrationRoot -Arguments @('status', '--short')).Output
$head = ((Invoke-CheckedGit -Root $IntegrationRoot -Arguments @('rev-parse', 'HEAD')).Output -join '').Trim()
if ($topLevel.Replace('\', '/') -ne $IntegrationRoot.Replace('\', '/')) {
    $errors.Add("Integration root mismatch: requested=$IntegrationRoot resolved=$topLevel")
}
if ($branch -ne 'master' -and $branch -notmatch '^integration/candidate-') {
    $errors.Add("Integration worktree must be on master or integration/candidate-*: actual=$branch")
}
if ($status.Count -gt 0) {
    $warnings.Add("Integration worktree is dirty: $($status -join '; ')")
}
if (-not (Test-Path -LiteralPath $manifest.engineRoot -PathType Container)) {
    $errors.Add("Required UE 5.8 root is missing: $($manifest.engineRoot)")
}
if (-not (Test-Path -LiteralPath (Join-Path $manifest.engineRoot 'Engine\Build\BatchFiles\Build.bat') -PathType Leaf)) {
    $errors.Add("Required UE 5.8 Build.bat is missing under: $($manifest.engineRoot)")
}
$baselineCheck = Invoke-CheckedGit -Root $IntegrationRoot -Arguments @(
    'merge-base', '--is-ancestor', "$($manifest.crystalBaselineCommit)", 'master') -AllowFailure
if ($baselineCheck.ExitCode -ne 0) {
    $errors.Add("Crystal baseline is not an ancestor of master: $($manifest.crystalBaselineCommit)")
}
if ($manifest.activationAllowed -or $manifest.productionContractVersion -ne 2) {
    $errors.Add('Prepared manifest must keep activationAllowed=false and productionContractVersion=2.')
}

$expectedBranches = [ordered]@{
    M3 = 'feature/m3-pcg-map'
    M7 = 'feature/m7-buildings'
    M11 = 'feature/m11-finale'
}
$worktrees = Get-WorktreeRecords -Root $IntegrationRoot
foreach ($owner in $expectedBranches.Keys) {
    $featureBranch = $expectedBranches[$owner]
    $record = @($worktrees | Where-Object { $_.Branch -eq $featureBranch })
    if ($record.Count -ne 1) {
        $errors.Add("Expected exactly one worktree for $featureBranch; found=$($record.Count)")
        continue
    }
    $featureStatus = (Invoke-CheckedGit -Root $record[0].Path -Arguments @('status', '--short')).Output
    if ($featureStatus.Count -gt 0) {
        $warnings.Add("$owner worktree is dirty: $($featureStatus -join '; ')")
    }
    $ahead = ((Invoke-CheckedGit -Root $IntegrationRoot -Arguments @(
        'rev-list', '--count', "master..$featureBranch")).Output -join '').Trim()
    $behind = ((Invoke-CheckedGit -Root $IntegrationRoot -Arguments @(
        'rev-list', '--count', "$featureBranch..master")).Output -join '').Trim()
    $changedPaths = (Invoke-CheckedGit -Root $IntegrationRoot -Arguments @(
        'diff', '--name-only', "master...$featureBranch")).Output
    $violations = @($changedPaths | Where-Object {
        (Get-RequiredBranchOwner -Path $_) -notin @($owner, 'Unclassified')
    })
    $unclassified = @($changedPaths | Where-Object {
        (Get-RequiredBranchOwner -Path $_) -eq 'Unclassified'
    })
    if ($violations.Count -gt 0) {
        $errors.Add("$owner branch changes paths owned elsewhere: $($violations -join ', ')")
    }
    if ($unclassified.Count -gt 0) {
        $warnings.Add("$owner branch has unclassified paths requiring manual ownership review: $($unclassified -join ', ')")
    }
    $binaryPaths = @($changedPaths | Where-Object { $_ -match '\.(uasset|umap)$' })
    if ($binaryPaths.Count -gt 0) {
        $warnings.Add("$owner branch changes binary assets requiring unique-writer review: $($binaryPaths -join ', ')")
    }
    $notes.Add("$owner branch=$featureBranch head=$($record[0].Head) ahead=$ahead behind=$behind changed=$($changedPaths.Count)")
}

$handoffs = [ordered]@{
    M3 = $M3Commit
    M7 = $M7Commit
    M11 = $M11Commit
}
foreach ($owner in $handoffs.Keys) {
    $commit = $handoffs[$owner]
    if ([string]::IsNullOrWhiteSpace($commit)) {
        continue
    }
    $verify = Invoke-CheckedGit -Root $IntegrationRoot -Arguments @(
        'rev-parse', '--verify', "$commit^{commit}") -AllowFailure
    if ($verify.ExitCode -ne 0) {
        $errors.Add("$owner handoff commit does not resolve: $commit")
        continue
    }
    $featureBranch = $expectedBranches[$owner]
    $ancestor = Invoke-CheckedGit -Root $IntegrationRoot -Arguments @(
        'merge-base', '--is-ancestor', $commit, $featureBranch) -AllowFailure
    if ($ancestor.ExitCode -ne 0) {
        $errors.Add("$owner handoff commit is not on ${featureBranch}: $commit")
    }
    $mergeBase = ((Invoke-CheckedGit -Root $IntegrationRoot -Arguments @(
        'merge-base', 'master', $commit)).Output -join '').Trim()
    $paths = (Invoke-CheckedGit -Root $IntegrationRoot -Arguments @(
        'diff', '--name-only', "$mergeBase..$commit")).Output
    $badPaths = @($paths | Where-Object {
        (Get-RequiredBranchOwner -Path $_) -notin @($owner, 'Unclassified')
    })
    $unknownPaths = @($paths | Where-Object {
        (Get-RequiredBranchOwner -Path $_) -eq 'Unclassified'
    })
    if ($badPaths.Count -gt 0) {
        $errors.Add("$owner handoff contains paths owned elsewhere: $($badPaths -join ', ')")
    }
    if ($unknownPaths.Count -gt 0) {
        $warnings.Add("$owner handoff contains unclassified paths requiring manual review: $($unknownPaths -join ', ')")
    }
    $binaryPaths = @($paths | Where-Object { $_ -match '\.(uasset|umap)$' })
    if ($binaryPaths.Count -gt 0) {
        $warnings.Add("$owner handoff changes binary assets requiring unique-writer review: $($binaryPaths -join ', ')")
    }
    $notes.Add("$owner handoff=$commit mergeBase=$mergeBase paths=$($paths.Count)")
}

Write-Output "ABTS V3 Integration Preflight"
Write-Output "Root=$IntegrationRoot"
Write-Output "Branch=$branch"
Write-Output "Head=$head"
Write-Output "Manifest=$ManifestPath"
Write-Output "Status=$($manifest.status) ActivationAllowed=$($manifest.activationAllowed) ProductionContractVersion=$($manifest.productionContractVersion)"
foreach ($note in $notes) {
    Write-Output "NOTE: $note"
}
foreach ($warning in $warnings) {
    Write-Warning $warning
}
foreach ($preflightError in $errors) {
    Write-Error $preflightError -ErrorAction Continue
}
Write-Output "SUMMARY Errors=$($errors.Count) Warnings=$($warnings.Count)"

if ($errors.Count -gt 0) {
    exit 1
}
if ($FailOnWarning -and $warnings.Count -gt 0) {
    exit 2
}
exit 0
