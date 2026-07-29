param(
	[switch]$Clean,
	[switch]$SkipTests
)

$ErrorActionPreference = "Stop"

$RequiredVCToolsVersion = "14.44.35207"
$RepositoryRoot = [System.IO.Path]::GetFullPath(
	(Join-Path $PSScriptRoot "..\.."))
$BuildDirectory = [System.IO.Path]::GetFullPath(
	(Join-Path $RepositoryRoot "Intermediate\M11CoreStandalone\cmake"))
$AllowedBuildRoot = [System.IO.Path]::GetFullPath(
	(Join-Path $RepositoryRoot "Intermediate\M11CoreStandalone"))
$ExecutablePath = [System.IO.Path]::GetFullPath(
	(Join-Path $AllowedBuildRoot "bin\ABTSM11CoreConformance.exe"))
$SearchExecutablePath = [System.IO.Path]::GetFullPath(
	(Join-Path $AllowedBuildRoot "bin\ABTSM11SearchCLI.exe"))
$SourceManifestPath = [System.IO.Path]::GetFullPath(
	(Join-Path $BuildDirectory "m11_core_source_manifest.json"))
$ReplayCommand = '& "' + $ExecutablePath + '" --json'
$VsWhere =
	"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"

function Write-MachineSummary
{
	param(
		[bool]$Passed,
		[string]$Stage,
		[string]$Diagnostic
	)

	$SourceManifest = $null
	if (Test-Path -LiteralPath $SourceManifestPath -PathType Leaf)
	{
		try
		{
			$SourceManifest = Get-Content `
				-LiteralPath $SourceManifestPath `
				-Raw `
				-Encoding UTF8 | ConvertFrom-Json
		}
		catch
		{
			$SourceManifest = $null
		}
	}

	$Summary = [ordered]@{
		schema = "abts.m11_core.build.v2"
		passed = $Passed
		stage = $Stage
		diagnostic = $Diagnostic
		vcToolsVersion = $RequiredVCToolsVersion
		buildDirectory = $BuildDirectory
		executablePath = $ExecutablePath
		searchExecutablePath = $SearchExecutablePath
		replayCommand = $ReplayCommand
		sourceManifestPath = $SourceManifestPath
	}
	if ($null -ne $SourceManifest)
	{
		$Summary.toolBuildVersion =
			$SourceManifest.toolBuildVersion
		$Summary.sourceHashSchema =
			$SourceManifest.sourceHashSchema
		$Summary.sourceHashSchemaVersion =
			$SourceManifest.sourceHashSchemaVersion
		$Summary.productionCoreSourceHashSha256 =
			$SourceManifest.productionCoreSourceHashSha256
		$Summary.conformanceToolSourceHashSha256 =
			$SourceManifest.conformanceToolSourceHashSha256
		$Summary.searchSourceHashSha256 =
			$SourceManifest.searchSourceHashSha256
		$Summary.compilerIdentity =
			$SourceManifest.compilerIdentity
		$Summary.architecture =
			$SourceManifest.architecture
		$Summary.cxxStandard =
			$SourceManifest.cxxStandard
		$Summary.floatingPointMode =
			$SourceManifest.floatingPointMode
		$Summary.numericalCompileContract =
			$SourceManifest.numericalCompileContract
	}

	Write-Output ($Summary | ConvertTo-Json -Compress)
}

function Invoke-InPinnedToolchain
{
	param(
		[Parameter(Mandatory = $true)]
		[string]$Command
	)

	$PinnedCommand = (
		'chcp 65001 >nul ' +
		'&& call "{0}" -arch=x64 -host_arch=x64 -vcvars_ver={1} >nul ' +
		'&& set VSLANG=1033 ' +
		'&& {2}'
	) -f $script:VsDevCmd, $RequiredVCToolsVersion, $Command

	& cmd.exe /d /s /v:on /c $PinnedCommand
	$script:LastPinnedExitCode = $LASTEXITCODE
}

try
{
	if (-not (Test-Path -LiteralPath $VsWhere -PathType Leaf))
	{
		throw "vswhere.exe was not found."
	}

	$VisualStudioRoot = & $VsWhere `
		-latest `
		-products * `
		-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
		-property installationPath
	if ([string]::IsNullOrWhiteSpace($VisualStudioRoot))
	{
		throw "Visual Studio 2022 with C++ tools was not found."
	}

	$script:VsDevCmd = Join-Path $VisualStudioRoot `
		"Common7\Tools\VsDevCmd.bat"
	$CMake = Join-Path $VisualStudioRoot `
		"Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
	$CTest = Join-Path $VisualStudioRoot `
		"Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
	$Ninja = Join-Path $VisualStudioRoot `
		"Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
	$PinnedToolsDirectory = Join-Path $VisualStudioRoot `
		"VC\Tools\MSVC\$RequiredVCToolsVersion"

	foreach ($RequiredPath in @(
		$script:VsDevCmd,
		$CMake,
		$CTest,
		$Ninja,
		$PinnedToolsDirectory))
	{
		if (-not (Test-Path -LiteralPath $RequiredPath))
		{
			throw "Required pinned build tool was not found: $RequiredPath"
		}
	}

	if ($Clean -and (Test-Path -LiteralPath $BuildDirectory))
	{
		$AllowedPrefix =
			$AllowedBuildRoot.TrimEnd('\', '/') +
			[System.IO.Path]::DirectorySeparatorChar
		if (-not $BuildDirectory.StartsWith(
			$AllowedPrefix,
			[System.StringComparison]::OrdinalIgnoreCase))
		{
			throw "Refusing to clean outside $AllowedBuildRoot"
		}
		Remove-Item -LiteralPath $BuildDirectory -Recurse -Force
	}

	Push-Location $PSScriptRoot
	try
	{
		Write-Output (
			"[ABTS][M11-A-v2.1] Configure: VS='{0}' MSVC={1}" -f
			$VisualStudioRoot,
			$RequiredVCToolsVersion)
		Invoke-InPinnedToolchain (
			'"{0}" --preset m11-core-msvc-release' -f $CMake)
		$ConfigureExitCode = $script:LastPinnedExitCode
		if ($ConfigureExitCode -ne 0)
		{
			Write-MachineSummary $false "configure" `
				"CMakeConfigureFailed:$ConfigureExitCode"
			exit $ConfigureExitCode
		}

		Invoke-InPinnedToolchain (
			'"{0}" --build --preset m11-core-msvc-release' -f $CMake)
		$BuildExitCode = $script:LastPinnedExitCode
		if ($BuildExitCode -ne 0)
		{
			Write-MachineSummary $false "build" `
				"CMakeBuildFailed:$BuildExitCode"
			exit $BuildExitCode
		}
		if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf))
		{
			Write-MachineSummary $false "build" `
				"ExpectedExecutableNotFound"
			exit 1
		}
		if (-not (Test-Path -LiteralPath $SearchExecutablePath -PathType Leaf))
		{
			Write-MachineSummary $false "build" `
				"ExpectedSearchExecutableNotFound"
			exit 1
		}

		if (-not $SkipTests)
		{
			Invoke-InPinnedToolchain (
				'"{0}" --test-dir "{1}" --output-on-failure' -f
				$CTest,
				$BuildDirectory)
			$TestExitCode = $script:LastPinnedExitCode
			if ($TestExitCode -ne 0)
			{
				Write-MachineSummary $false "test" `
					"CTestFailed:$TestExitCode"
				exit $TestExitCode
			}
		}

		Write-MachineSummary $true `
			$(if ($SkipTests) { "build" } else { "test" }) `
			$(if ($SkipTests) {
				"BuildPassedTestsSkipped"
			} else {
				"ConfigureBuildCTestPassed"
			})
	}
	finally
	{
		Pop-Location
	}
}
catch
{
	Write-MachineSummary $false "bootstrap" $_.Exception.Message
	exit 1
}
