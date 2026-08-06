param(
    [ValidateSet('LandingPreviews', 'FinaleRemotePreview')]
    [string]$Slice = 'LandingPreviews',
    [ValidateSet('Off', 'On')]
    [string]$Style = 'On',
    [ValidateSet(50, 75, 100)]
    [int]$ScreenPercentage = 100,
    [ValidateRange(0, 11)]
    [int]$Rank = 11,
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'
$Repo = (git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
if (-not $Repo) { throw 'Unable to resolve repository root.' }
$Engine = 'C:\Program Files\Epic Games\UE_5.8'
$Editor = Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor.exe'
$Project = Join-Path $Repo 'AngryBirdsToSpace.uproject'
if (-not (Test-Path -LiteralPath $Editor)) { throw "UE 5.8 editor not found: $Editor" }
if (-not (Test-Path -LiteralPath $Project)) { throw "Project not found: $Project" }
$Dirty = @(git -C $Repo status --porcelain)
if (-not $AllowDirty -and $Dirty.Count -ne 0) {
    throw 'Formal T2-C1 evidence requires a clean worktree.'
}
$BuildId = (git -C $Repo rev-parse HEAD).Trim()
$StyleValue = if ($Style -eq 'On') { 1 } else { 0 }
$RunId = 'T2C1-{0}-{1}-SP{2}-{3}' -f $Slice, $Style, $ScreenPercentage, (Get-Date -Format 'yyyyMMdd-HHmmss')
$Output = Join-Path $Repo "Saved\ABTSVisualCaptures\ToonT2C1\$RunId"
$Log = Join-Path $Repo "Saved\Logs\$RunId.log"
New-Item -ItemType Directory -Path $Output -Force | Out-Null

$Arguments = @(
    $Project,
    '/Game/Maps/L_ABTS_M11',
    '-game', '-dx11', '-RenderOffscreen', '-ForceRes', '-ResX=1920', '-ResY=1080',
    '-ABTSM3R5Preview', '-ABTSM3R5PreviewCandidate=4',
    '-ABTSM3R31SlotPreviewCandidate=4',
    '-ABTSToonT2C1Capture', "-ABTSToonT2C1Slice=$Slice",
    "-ABTSToonT2C1Stylized=$StyleValue", '-ABTSToonT2C1ExpectedSeed=312503',
    "-ABTSToonT2C1ScreenPercentage=$ScreenPercentage",
    '-ABTSToonT2C1WarmupFrames=30', '-ABTSToonT2C1TimeoutSeconds=300',
    "-ABTSToonT2C1Output=$Output", "-ABTSToonT2C1BuildId=$BuildId",
    '-NoSound', "-AbsLog=$Log"
)
if ($Slice -eq 'LandingPreviews') {
    $Arguments += '-ABTSToonT2C1ExitWhenDone'
} else {
    $MovieOutput = Join-Path $Output 'M11Movie'
    New-Item -ItemType Directory -Path $MovieOutput -Force | Out-Null
    $Arguments += @(
        '-ABTSM11CameraCapture', "-ABTSM11CaptureRank=$Rank",
        "-ABTSM11CaptureStylized=$StyleValue", '-ABTSM11CaptureAutoExit=1',
        '-ABTSM11CaptureWarmupFrames=30', '-ABTSM11CaptureTerminalHoldFrames=24',
        '-ABTSM11CaptureTimeoutSeconds=300', "-MovieFolder=$MovieOutput",
        "-MovieName=$RunId", '-MovieFormat=JPG', '-MovieFrameRate=30', '-MovieQuality=90'
    )
}

$Process = Start-Process -FilePath $Editor -ArgumentList $Arguments -WindowStyle Hidden -Wait -PassThru
if ($Process.ExitCode -ne 0) { throw "T2-C1 process failed: $($Process.ExitCode). Log: $Log" }
$ManifestPath = Join-Path $Output 'manifest.json'
if (-not (Test-Path -LiteralPath $ManifestPath)) { throw "Missing manifest: $ManifestPath" }
$Manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($Manifest.status -ne 'Succeeded') { throw "T2-C1 manifest failed: $($Manifest.reason)" }
if ($Manifest.buildIdentity -ne $BuildId) { throw 'Manifest build identity mismatch.' }
if ($Manifest.expectedWorldSeed -ne 312503 -or $Manifest.actualWorldSeed -ne 312503) {
    throw 'Manifest world identity mismatch.'
}
if ($Manifest.screenPercentage -ne $ScreenPercentage -or
    $Manifest.screenPercentageCVar -ne $ScreenPercentage) {
    throw 'Manifest screen-percentage identity mismatch.'
}
if ($Manifest.m7AdapterReady) { throw 'No-M7 T2-C1 unexpectedly observed an M7 semantic adapter.' }
$ExpectedRecords = if ($Slice -eq 'LandingPreviews') { 2 } else { 1 }
if (@($Manifest.records).Count -ne $ExpectedRecords) {
    throw "Unexpected record count: $(@($Manifest.records).Count), expected $ExpectedRecords"
}
foreach ($Record in @($Manifest.records)) {
    if (-not (Test-Path -LiteralPath $Record.artifactPath)) {
        throw "Missing artifact: $($Record.artifactPath)"
    }
    if ($Record.width -le 0 -or $Record.height -le 0 -or -not $Record.artifactMD5) {
        throw "Invalid artifact evidence for $($Record.subject)"
    }
}
if ($Slice -eq 'LandingPreviews') {
    $Subjects = @($Manifest.records | ForEach-Object { $_.subject })
    if ($Subjects[0] -ne 'GroundLandingPreview' -or
        $Subjects[1] -ne 'SatelliteLandingPreview') {
        throw 'Landing preview subjects are not the required production views.'
    }
    if ($Manifest.records[0].fixtureHash -eq $Manifest.records[1].fixtureHash) {
        throw 'Ground and satellite fixture identities unexpectedly match.'
    }
} else {
    $Record = $Manifest.records[0]
    if ($Record.subject -ne 'FinaleRemotePreview' -or
        [uint64]$Record.runtimeCaptureRevision -le 0) {
        throw 'Finale remote preview did not consume a production capture revision.'
    }
    $M11ManifestPath = Get-ChildItem -LiteralPath $MovieOutput -Filter '*.manifest.json' |
        Select-Object -First 1 -ExpandProperty FullName
    if (-not $M11ManifestPath) { throw 'Missing M11 recorder manifest.' }
    $M11Manifest = Get-Content -LiteralPath $M11ManifestPath -Raw -Encoding UTF8 |
        ConvertFrom-Json
    if ($M11Manifest.status -ne 'Complete' -or
        $M11Manifest.reason -ne 'TargetHit' -or
        $M11Manifest.contractVersion -lt 4 -or
        $M11Manifest.rank -ne $Rank -or
        $M11Manifest.stylizedEnabled -ne ($StyleValue -ne 0) -or
        -not $M11Manifest.stylizedViewRegistered -or
        -not $M11Manifest.stylizedViewPolicyValid -or
        -not $M11Manifest.stylizedRuntimeStateMaintained -or
        $M11Manifest.stylizedRuntimeStateFailureFrame -ne -1 -or
        $M11Manifest.frameCountObserved -le 0 -or
        $M11Manifest.playbackPlanHash -ne $Record.fixtureHash) {
        throw 'M11 recorder identity or completion contract mismatch.'
    }
    if (-not (Test-Path -LiteralPath $M11Manifest.videoPath) -or
        (Get-Item -LiteralPath $M11Manifest.videoPath).Length -le 0) {
        throw 'M11 recorder video is missing or empty.'
    }
}
if (-not (Select-String -LiteralPath $Log -SimpleMatch '[ABTS][Rendering][T2-C1][Terminal] Success=1')) {
    throw "Missing T2-C1 success marker: $Log"
}
Write-Output "T2-C1 succeeded. Manifest=$ManifestPath Log=$Log"
