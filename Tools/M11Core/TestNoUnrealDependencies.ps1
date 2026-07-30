param(
	[Parameter(Mandatory = $true)]
	[string]$Executable
)

$ErrorActionPreference = "Stop"

function ConvertTo-CompactJson
{
	param([hashtable]$Value)
	return ($Value | ConvertTo-Json -Compress -Depth 4)
}

$ResolvedExecutable = [System.IO.Path]::GetFullPath($Executable)
if (-not (Test-Path -LiteralPath $ResolvedExecutable -PathType Leaf))
{
	Write-Output (ConvertTo-CompactJson @{
		schema = "abts.m11_core.dependencies.v1"
		passed = $false
		executable = $ResolvedExecutable
		diagnostic = "ExecutableNotFound"
	})
	exit 1
}

$Dumpbin = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
if ($null -eq $Dumpbin)
{
	Write-Output (ConvertTo-CompactJson @{
		schema = "abts.m11_core.dependencies.v1"
		passed = $false
		executable = $ResolvedExecutable
		diagnostic = "DumpbinNotFoundInPinnedMSVCEnvironment"
	})
	exit 1
}

$DependencyOutput = & $Dumpbin.Source /nologo /dependents $ResolvedExecutable 2>&1
$DumpbinExitCode = $LASTEXITCODE
if ($DumpbinExitCode -ne 0)
{
	Write-Output (ConvertTo-CompactJson @{
		schema = "abts.m11_core.dependencies.v1"
		passed = $false
		executable = $ResolvedExecutable
		diagnostic = "DumpbinFailed"
		dumpbinExitCode = $DumpbinExitCode
	})
	exit 1
}

$Dependencies = @(
	$DependencyOutput |
		ForEach-Object { [string]$_ } |
		Where-Object { $_ -match '^\s+([^\s:]+\.dll)\s*$' } |
		ForEach-Object { $Matches[1] } |
		Sort-Object -Unique
)

$ForbiddenDependencies = @(
	$Dependencies |
		Where-Object {
			$_ -match '(?i)^(UnrealEditor|UE[45]Editor)' -or
			$_ -match '(?i)^ABTSRuntime\.dll$'
		}
)

$RepositoryRoot = [System.IO.Path]::GetFullPath(
	(Join-Path $PSScriptRoot "..\.."))
$CoreSourceRoots = @(
	(Join-Path $RepositoryRoot "Source\ABTSRuntime\Public\M11Core"),
	(Join-Path $RepositoryRoot "Source\ABTSRuntime\Private\M11Core")
)
$CoreSourceFiles = @(
	$CoreSourceRoots |
		ForEach-Object {
			Get-ChildItem -LiteralPath $_ -File |
				Where-Object { $_.Extension -in @(".h", ".cpp") }
		} |
		Sort-Object FullName
)
$ForbiddenSourceIncludes = @()
foreach ($CoreSourceFile in $CoreSourceFiles)
{
	$LineNumber = 0
	foreach ($Line in Get-Content -LiteralPath $CoreSourceFile.FullName)
	{
		++$LineNumber
		if ($Line -notmatch '^\s*#\s*include\s*([<"])([^>"]+)[>"]')
		{
			continue
		}

		$Delimiter = $Matches[1]
		$IncludeTarget = $Matches[2].Replace('\', '/')
		$IsPortableQuotedInclude =
			$Delimiter -eq '"' -and
			$IncludeTarget.StartsWith(
				"M11Core/",
				[System.StringComparison]::Ordinal)
		$IsKnownUnrealInclude =
			$IncludeTarget -match (
				'(?i)^(CoreMinimal|CoreTypes|Math/|Containers/|' +
				'Templates/|HAL/|Misc/|UObject/|World/|Engine/|' +
				'Async/|Algo/|Modules/|Serialization/|' +
				'Internationalization/|Delegates/|ABTS)')
		if (($Delimiter -eq '"' -and -not $IsPortableQuotedInclude) -or
			$IsKnownUnrealInclude)
		{
			$RelativePath = [System.IO.Path]::GetRelativePath(
				$RepositoryRoot,
				$CoreSourceFile.FullName).Replace('\', '/')
			$ForbiddenSourceIncludes +=
				"${RelativePath}:${LineNumber}:${IncludeTarget}"
		}
	}
}

$Passed =
	$ForbiddenDependencies.Count -eq 0 -and
	$ForbiddenSourceIncludes.Count -eq 0
Write-Output (ConvertTo-CompactJson @{
	schema = "abts.m11_core.dependencies.v1"
	passed = $Passed
	executable = $ResolvedExecutable
	dependencies = $Dependencies
	forbiddenDependencies = $ForbiddenDependencies
	scannedCoreSourceFileCount = $CoreSourceFiles.Count
	forbiddenSourceIncludes = $ForbiddenSourceIncludes
	diagnostic = if ($Passed) {
		"NoUnrealEditorDllOrSourceDependencies"
	} else {
		"ForbiddenUnrealOrABTSRuntimeDependency"
	}
})

exit $(if ($Passed) { 0 } else { 1 })
