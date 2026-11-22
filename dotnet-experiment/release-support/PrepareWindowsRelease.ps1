<#
.SYNOPSIS
    Prepare a self-contained Windows x64 CLI and GUI release.
.DESCRIPTION
    Publishes both applications and stages the prepared FFmpeg bundle,
    MKVToolNix, GPAC and presets. Existing output is replaced only if it is
    marked as a win-x64 release. No installer or hardware probe is run.
.PARAMETER ReleaseRoot
    Output directory. Defaults to dotnet-experiment\release\win-x64.
.PARAMETER FfmpegBundle
    Prepared FFmpeg directory. Defaults to src\platform\windows\bin.
.PARAMETER MkvmergePath
    Path to mkvmerge.exe. Otherwise use MKVMERGE_BIN, MKVMERGE,
    a standard installation location, or PATH.
.PARAMETER Mp4BoxPath
    Path to MP4Box.exe. Otherwise use MP4BOX_BIN, MP4BOX,
    a standard installation location, or PATH.
.PARAMETER PresetsPath
    Preset catalog. Defaults to the repository's presets.json.
#>
[CmdletBinding()]
param(
    [string] $ReleaseRoot,
    [string] $FfmpegBundle,
    [string] $MkvmergePath,
    [string] $Mp4BoxPath,
    [string] $PresetsPath
)

$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') {
    throw 'PrepareWindowsRelease requires a Windows host.'
}

$dotnetRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repoRoot = [IO.Path]::GetFullPath((Join-Path $dotnetRoot '..'))
if (-not $ReleaseRoot) { $ReleaseRoot = Join-Path $dotnetRoot 'release\win-x64' }
if (-not $FfmpegBundle) { $FfmpegBundle = Join-Path $repoRoot 'src\platform\windows\bin' }
if (-not $PresetsPath) { $PresetsPath = Join-Path $repoRoot 'presets.json' }
$ReleaseRoot = [IO.Path]::GetFullPath($ReleaseRoot).TrimEnd('\')
$FfmpegBundle = [IO.Path]::GetFullPath($FfmpegBundle)
$PresetsPath = [IO.Path]::GetFullPath($PresetsPath)

function Find-Tool([string] $Explicit, [string[]] $EnvironmentNames, [string[]] $Candidates, [string] $Name) {
    if ($Explicit) {
        if (-not (Test-Path -LiteralPath $Explicit -PathType Leaf)) {
            throw "$Name executable not found: $Explicit"
        }
        return (Resolve-Path -LiteralPath $Explicit).Path
    }
    foreach ($variable in $EnvironmentNames) {
        $value = [Environment]::GetEnvironmentVariable($variable)
        if ($value) {
            if (-not (Test-Path -LiteralPath $value -PathType Leaf)) {
                throw "$variable points to a missing executable: $value"
            }
            return (Resolve-Path -LiteralPath $value).Path
        }
    }
    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    throw "$Name not found. Specify its path or install it in a standard location."
}

function Test-Tool([string] $Path, [string[]] $Arguments, [string] $Pattern) {
    $previousPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & $Path @Arguments 2>&1
        $code = $LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $previousPreference
    }
    $text = $output -join [Environment]::NewLine
    if ($code -ne 0 -or $text -notmatch $Pattern) {
        throw "Startup check failed for ${Path} (exit $code):`n$text"
    }
}

function Test-ContainedPath([string] $Parent, [string] $Child) {
    $parentPath = [IO.Path]::GetFullPath($Parent).TrimEnd('\')
    $childPath = [IO.Path]::GetFullPath($Child).TrimEnd('\')
    return $childPath.Equals($parentPath, [StringComparison]::OrdinalIgnoreCase) -or
        $childPath.StartsWith($parentPath + '\', [StringComparison]::OrdinalIgnoreCase)
}

function Assert-SafeOutput([string] $Output, [string[]] $Inputs) {
    if ([IO.Path]::GetPathRoot($Output).TrimEnd('\') -ieq $Output) {
        throw "ReleaseRoot must not be a drive root: $Output"
    }
    if (Test-ContainedPath $Output $dotnetRoot) {
        throw "ReleaseRoot must not contain the .NET project directory: $Output"
    }
    foreach ($inputPath in $Inputs) {
        if ((Test-ContainedPath $Output $inputPath) -or (Test-ContainedPath $inputPath $Output)) {
            throw "ReleaseRoot overlaps an input: $inputPath"
        }
    }
    $current = $Output
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "ReleaseRoot or an ancestor is a reparse point: $current"
            }
        }
        $current = [IO.Path]::GetDirectoryName($current)
    }
    if (Test-Path -LiteralPath $Output) {
        if (-not (Test-Path -LiteralPath $Output -PathType Container)) {
            throw "ReleaseRoot is not a directory: $Output"
        }
        $marker = Join-Path $Output 'ffmpeg-converter.release'
        if (-not (Test-Path -LiteralPath $marker -PathType Leaf) -or
            (Get-Content -LiteralPath $marker -Raw).Trim() -cne 'win-x64') {
            throw "Refusing to replace an unmarked or non-Windows release directory: $Output"
        }
    }
}

if (-not (Test-Path -LiteralPath $FfmpegBundle -PathType Container)) {
    throw "FFmpeg bundle not found: $FfmpegBundle"
}
foreach ($file in @((Join-Path $FfmpegBundle 'ffmpeg.exe'), (Join-Path $FfmpegBundle 'ffprobe.exe'), $PresetsPath)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -eq 0) {
        throw "Required release input missing or empty: $file"
    }
}
$MkvmergePath = Find-Tool $MkvmergePath @('MKVMERGE_BIN', 'MKVMERGE') @(
    (Join-Path $env:ProgramFiles 'MKVToolNix\mkvmerge.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'MKVToolNix\mkvmerge.exe')
) 'mkvmerge.exe'
$Mp4BoxPath = Find-Tool $Mp4BoxPath @('MP4BOX_BIN', 'MP4BOX') @(
    (Join-Path $env:ProgramFiles 'GPAC\MP4Box.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'GPAC\MP4Box.exe')
) 'MP4Box.exe'
$dotnet = Get-Command dotnet.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1
Assert-SafeOutput $ReleaseRoot @($FfmpegBundle, $PresetsPath, (Split-Path -Parent $MkvmergePath), (Split-Path -Parent $Mp4BoxPath))
Test-Tool (Join-Path $FfmpegBundle 'ffmpeg.exe') @('-version') 'ffmpeg version'
Test-Tool (Join-Path $FfmpegBundle 'ffprobe.exe') @('-version') 'ffprobe version'
Test-Tool $MkvmergePath @('--version') 'mkvmerge v'
Test-Tool $Mp4BoxPath @('-version') 'MP4Box.*GPAC version'

$parent = Split-Path -Parent $ReleaseRoot
New-Item -ItemType Directory -Force -Path $parent | Out-Null
$staging = Join-Path $parent ('.ffmpeg-converter-stage-' + [guid]::NewGuid().ToString('N'))
$backup = Join-Path $parent ('.ffmpeg-converter-backup-' + [guid]::NewGuid().ToString('N'))
$stageCreated = $false
$oldMoved = $false
$script:toolHashes = @{}

function Copy-ToolFile([string] $Source, [string] $RelativePath) {
    $hash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
    $key = $RelativePath.ToLowerInvariant()
    if ($script:toolHashes.ContainsKey($key)) {
        if ($script:toolHashes[$key] -ne $hash) {
            throw "Conflicting tool runtime files: $RelativePath"
        }
        return
    }
    $destination = Join-Path (Join-Path $staging 'tools') $RelativePath
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $destination
    $script:toolHashes[$key] = $hash
}

function Copy-ToolTree([string] $Source, [string] $RelativePath) {
    if (-not (Test-Path -LiteralPath $Source -PathType Container)) { return }
    $root = (Resolve-Path -LiteralPath $Source).Path.TrimEnd('\') + '\'
    Get-ChildItem -LiteralPath $Source -File -Recurse -Force | ForEach-Object {
        $name = $_.FullName.Substring($root.Length)
        if ($RelativePath) { $name = Join-Path $RelativePath $name }
        Copy-ToolFile $_.FullName $name
    }
}

function Copy-ToolRuntime([string] $Executable, [string] $Name, [string[]] $DataDirectories) {
    Copy-ToolFile $Executable $Name
    $directory = Split-Path -Parent $Executable
    Get-ChildItem -LiteralPath $directory -File -Filter '*.dll' | ForEach-Object {
        Copy-ToolFile $_.FullName $_.Name
    }
    foreach ($data in $DataDirectories) {
        Copy-ToolTree (Join-Path $directory $data) $data
    }
}

try {
    New-Item -ItemType Directory -Path $staging | Out-Null
    $stageCreated = $true
    $app = Join-Path $staging 'app'
    $tools = Join-Path $staging 'tools'
    New-Item -ItemType Directory -Path $app, $tools | Out-Null
    foreach ($project in @('FfmpegConverter.Cli', 'FfmpegConverter.Gui')) {
        & $dotnet.Source publish (Join-Path $dotnetRoot "$project\$project.csproj") `
            -c Release -r win-x64 --self-contained true `
            -p:PublishSingleFile=false -p:PublishTrimmed=false `
            -p:DebugType=None -p:DebugSymbols=false -p:PrepareReleasePublish=true -o $app
        if ($LASTEXITCODE -ne 0) { throw "dotnet publish failed for $project (exit $LASTEXITCODE)." }
    }
    Get-ChildItem -LiteralPath $app -Filter '*.pdb' -File -Recurse | Remove-Item -Force
    Copy-ToolTree $FfmpegBundle ''
    Copy-ToolRuntime $MkvmergePath 'mkvmerge.exe' @('data', 'locale', 'tools')
    Copy-ToolRuntime $Mp4BoxPath 'MP4Box.exe' @('share')
    Copy-Item -LiteralPath $PresetsPath -Destination (Join-Path $staging 'presets.json')
    [IO.File]::WriteAllText((Join-Path $staging 'ffmpeg-converter.release'), "win-x64`n")

    foreach ($relative in @(
        'ffmpeg-converter.release', 'presets.json', 'app\FfmpegConverter.Cli.exe',
        'app\FfmpegConverter.Gui.exe', 'app\FfmpegConverter.Core.dll', 'app\coreclr.dll',
        'app\hostfxr.dll', 'app\libSkiaSharp.dll', 'tools\ffmpeg.exe', 'tools\ffprobe.exe',
        'tools\mkvmerge.exe', 'tools\MP4Box.exe'
    )) {
        $path = Join-Path $staging $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
            throw "Staged release file missing or empty: $path"
        }
    }

    $previousPath = $env:PATH
    $previousDotnetRoot = $env:DOTNET_ROOT
    $previousMultilevel = $env:DOTNET_MULTILEVEL_LOOKUP
    try {
        $env:PATH = @($tools, (Join-Path $env:SystemRoot 'System32'), $env:SystemRoot) -join ';'
        $env:DOTNET_ROOT = Join-Path $staging 'no-installed-dotnet'
        $env:DOTNET_MULTILEVEL_LOOKUP = '0'
        Push-Location $staging
        try {
            Test-Tool (Join-Path $tools 'ffmpeg.exe') @('-version') 'ffmpeg version'
            Test-Tool (Join-Path $tools 'ffprobe.exe') @('-version') 'ffprobe version'
            Test-Tool (Join-Path $tools 'mkvmerge.exe') @('--version') 'mkvmerge v'
            Test-Tool (Join-Path $tools 'MP4Box.exe') @('-version') 'MP4Box.*GPAC version'
            Test-Tool (Join-Path $app 'FfmpegConverter.Cli.exe') @('--help') '(?s)--video-track.*Apple M4V'
        }
        finally { Pop-Location }
    }
    finally {
        $env:PATH = $previousPath
        $env:DOTNET_ROOT = $previousDotnetRoot
        $env:DOTNET_MULTILEVEL_LOOKUP = $previousMultilevel
    }

    Assert-SafeOutput $ReleaseRoot @($FfmpegBundle, $PresetsPath, (Split-Path -Parent $MkvmergePath), (Split-Path -Parent $Mp4BoxPath))
    if (Test-Path -LiteralPath $ReleaseRoot) {
        Move-Item -LiteralPath $ReleaseRoot -Destination $backup
        $oldMoved = $true
    }
    Move-Item -LiteralPath $staging -Destination $ReleaseRoot
    $stageCreated = $false
    if ($oldMoved) {
        Remove-Item -LiteralPath $backup -Recurse -Force
        $oldMoved = $false
    }
    Write-Host "Prepared Windows release: $ReleaseRoot"
}
finally {
    if ($stageCreated -and (Test-Path -LiteralPath $staging)) {
        Remove-Item -LiteralPath $staging -Recurse -Force
    }
    if ($oldMoved -and -not (Test-Path -LiteralPath $ReleaseRoot)) {
        Move-Item -LiteralPath $backup -Destination $ReleaseRoot
    }
}
