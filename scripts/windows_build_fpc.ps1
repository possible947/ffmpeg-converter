<#
.SYNOPSIS
    Build the Windows Pascal application and stage a complete release into
    fpc\bin\.

.DESCRIPTION
    Discovers FPC and lazbuild, builds the CLI and GUI, and stages ffmpeg,
    ffprobe, mkvmerge, MP4Box, their runtime files, and presets.json beside
    the application. Missing or non-working release tools stop the build.
    The build uses native Windows PowerShell/FPC/Lazarus and does not require
    MSYS2. FFmpeg is consumed as a prepared artifact from
    src\platform\windows\bin\.

    Use -LibraryOnly to build the converter shared library without packaging
    an application release.

.PARAMETER CLIOnly
    Build only the Pascal CLI (ffmpeg_converter.exe).  Skip GUI.

.PARAMETER GUIOnly
    Build only the Pascal GUI (ffmpeg_converter_gui.exe).  Skip CLI.

.PARAMETER LibraryOnly
    Build only the Pascal shared library (converter_pas.dll). This does not
    create an application release bundle.

.PARAMETER Clean
    Remove intermediate unit cache and Lazarus lib directories before
    building to force a full recompile.

.PARAMETER MkvmergePath
    Optional explicit path to mkvmerge.exe. Otherwise use the bundled copy,
    common MKVToolNix install locations, or PATH.

.PARAMETER MP4BoxPath
    Optional explicit path to MP4Box.exe. Otherwise use the bundled copy,
    common GPAC install locations, or PATH.

.PARAMETER Help
    Show this help message and exit.

.EXAMPLE
    # Normal build: CLI + GUI, then stage to fpc\bin
    .\scripts\windows_build_fpc.ps1

.EXAMPLE
    # Build CLI only
    .\scripts\windows_build_fpc.ps1 -CLIOnly

.EXAMPLE
    # Build GUI only
    .\scripts\windows_build_fpc.ps1 -GUIOnly

.EXAMPLE
    # Force full recompile of both
    .\scripts\windows_build_fpc.ps1 -Clean

.EXAMPLE
    # Build the shared library only
    .\scripts\windows_build_fpc.ps1 -LibraryOnly

.EXAMPLE
    # Use tools installed outside the standard locations
    .\scripts\windows_build_fpc.ps1 -MkvmergePath C:\tools\mkvmerge.exe -MP4BoxPath C:\tools\MP4Box.exe

.EXAMPLE
    # Clean + GUI only
    .\scripts\windows_build_fpc.ps1 -Clean -GUIOnly
#>

param(
    [switch] $CLIOnly,
    [switch] $GUIOnly,
    [switch] $LibraryOnly,
    [switch] $Clean,
    [string] $MkvmergePath,
    [string] $MP4BoxPath,
    [switch] $Help
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
#  -Help
# ---------------------------------------------------------------------------
if ($Help) {
    Get-Help $MyInvocation.MyCommand.Definition
    exit 0
}

$selectedModes = @()
if ($CLIOnly) { $selectedModes += 'CLIOnly' }
if ($GUIOnly) { $selectedModes += 'GUIOnly' }
if ($LibraryOnly) { $selectedModes += 'LibraryOnly' }
if ($selectedModes.Count -gt 1) {
    throw 'Choose at most one of -CLIOnly, -GUIOnly, or -LibraryOnly.'
}

# ---------------------------------------------------------------------------
#  Paths
# ---------------------------------------------------------------------------
$ScriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot   = Split-Path -Parent $ScriptDir
$FpcDir     = Join-Path $RepoRoot 'fpc'
$BinDir     = Join-Path $FpcDir   'bin'
$UnitsBase  = Join-Path $FpcDir   'build\.units'
$BundleDir  = Join-Path $RepoRoot 'src\platform\windows\bin'

# ---------------------------------------------------------------------------
#  Banner
# ---------------------------------------------------------------------------
Write-Host ''
Write-Host '=== ffmpeg-converter Pascal Build ===' -ForegroundColor White
if ($CLIOnly)      { Write-Host '  Mode  : CLI only'  -ForegroundColor Cyan }
elseif ($GUIOnly)  { Write-Host '  Mode  : GUI only'  -ForegroundColor Cyan }
elseif ($LibraryOnly) { Write-Host '  Mode  : library only' -ForegroundColor Cyan }
else               { Write-Host '  Mode  : CLI + GUI' -ForegroundColor Cyan }
if ($Clean)        { Write-Host '  Clean : yes (intermediate files will be removed)' -ForegroundColor Yellow }
Write-Host ''

# ---------------------------------------------------------------------------
#  Auto-discover lazbuild.exe
#  Priority: PATH -> well-known install locations -> versioned C:\lazarus* glob
# ---------------------------------------------------------------------------
function Find-LazBuild {
    $cmd = Get-Command lazbuild.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $roots = @(
        'C:\lazarus',
        'C:\Lazarus',
        (Join-Path $env:ProgramFiles          'Lazarus'),
        (Join-Path ${env:ProgramFiles(x86)}   'Lazarus'),
        (Join-Path $env:LocalAppData          'Lazarus')
    )
    foreach ($r in $roots) {
        $p = Join-Path $r 'lazbuild.exe'
        if (Test-Path $p) { return $p }
    }

    # Versioned installs: C:\lazarus3.6\ etc.
    $hits = Get-Item 'C:\lazarus*\lazbuild.exe' -ErrorAction SilentlyContinue
    if ($hits) { return ($hits | Sort-Object FullName -Descending)[0].FullName }

    return $null
}

# ---------------------------------------------------------------------------
#  Auto-discover fpc.exe
#  Derives the path from the lazbuild location so both stay in sync with
#  the same Lazarus installation.
#  Priority: PATH -> <lazarusdir>\fpc\*\bin\x86_64-win64\fpc.exe
# ---------------------------------------------------------------------------
function Find-FPC([string] $LazDir) {
    $cmd = Get-Command fpc.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    if ($LazDir -and (Test-Path $LazDir)) {
        $pattern = Join-Path $LazDir 'fpc\*\bin\x86_64-win64\fpc.exe'
        $hits = Get-ChildItem $pattern -ErrorAction SilentlyContinue |
                Sort-Object FullName -Descending
        if ($hits) { return $hits[0].FullName }
    }

    return $null
}

function Find-RequiredTool(
    [string] $ExplicitPath,
    [string[]] $EnvironmentNames,
    [string[]] $Candidates,
    [string] $CommandName,
    [string] $DisplayName
) {
    if ($ExplicitPath) {
        if (-not (Test-Path -LiteralPath $ExplicitPath -PathType Leaf)) {
            throw "$DisplayName was explicitly set but does not exist: $ExplicitPath"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }

    foreach ($name in $EnvironmentNames) {
        $environmentPath = [Environment]::GetEnvironmentVariable($name)
        if ($environmentPath) {
            if (-not (Test-Path -LiteralPath $environmentPath -PathType Leaf)) {
                throw "$name points to a missing $DisplayName executable: $environmentPath"
            }
            return (Resolve-Path -LiteralPath $environmentPath).Path
        }
    }

    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $command = Get-Command $CommandName -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "$DisplayName not found. Pass its path explicitly or install it in a supported location."
}

function Test-ToolExecutable(
    [string] $Path,
    [string[]] $Arguments,
    [string] $DisplayName,
    [string] $ExpectedPattern,
    [switch] $BundledRuntime
) {
    $previousPath = $env:PATH
    $pushedLocation = $false
    try {
        if ($BundledRuntime) {
            $toolDir = Split-Path -Parent $Path
            Push-Location $toolDir
            $pushedLocation = $true
            $env:PATH = @($toolDir, (Join-Path $env:SystemRoot 'System32'), $env:SystemRoot) -join ';'
        }

        $output = & $Path @Arguments 2>&1
        $exitCode = $LASTEXITCODE
        $outputText = $output -join [Environment]::NewLine
        if ($exitCode -ne 0) {
            throw "$DisplayName failed its startup check (exit $exitCode): $Path`n$outputText"
        }
        if ($ExpectedPattern -and $outputText -notmatch $ExpectedPattern) {
            throw "$Path did not identify itself as $DisplayName during its startup check."
        }
    }
    finally {
        if ($BundledRuntime) {
            $env:PATH = $previousPath
        }
        if ($pushedLocation) {
            Pop-Location
        }
    }
}

# ---------------------------------------------------------------------------
#  Tool discovery
# ---------------------------------------------------------------------------
$LazBuildExe = Find-LazBuild
$LazDir      = if ($LazBuildExe) { Split-Path -Parent $LazBuildExe } else { $null }
$FpcExe      = Find-FPC $LazDir
$MkvmergeExe = $null
$MP4BoxExe = $null
if (-not $LibraryOnly) {
    $MkvmergeExe = Find-RequiredTool -ExplicitPath $MkvmergePath `
        -EnvironmentNames @('MKVMERGE_BIN', 'MKVMERGE') `
        -Candidates @((Join-Path $BundleDir 'mkvmerge.exe'),
          (Join-Path $env:ProgramFiles 'MKVToolNix\mkvmerge.exe'),
          (Join-Path ${env:ProgramFiles(x86)} 'MKVToolNix\mkvmerge.exe')) `
        -CommandName 'mkvmerge.exe' -DisplayName 'mkvmerge'
    $MP4BoxExe = Find-RequiredTool -ExplicitPath $MP4BoxPath `
        -EnvironmentNames @('MP4BOX_BIN', 'MP4BOX') `
        -Candidates @((Join-Path $BundleDir 'MP4Box.exe'),
          (Join-Path $BundleDir 'mp4box.exe'),
          (Join-Path $env:ProgramFiles 'GPAC\MP4Box.exe'),
          (Join-Path ${env:ProgramFiles(x86)} 'GPAC\MP4Box.exe'),
          (Join-Path $env:ProgramFiles 'GPAC\mp4box.exe'),
          (Join-Path ${env:ProgramFiles(x86)} 'GPAC\mp4box.exe')) `
        -CommandName 'MP4Box.exe' -DisplayName 'MP4Box'

    foreach ($required in @(
        @{ Path = (Join-Path $BundleDir 'ffmpeg.exe'); Name = 'ffmpeg.exe' },
        @{ Path = (Join-Path $BundleDir 'ffprobe.exe'); Name = 'ffprobe.exe' }
    )) {
        if (-not (Test-Path -LiteralPath $required.Path -PathType Leaf)) {
            throw "Required bundled component not found: $($required.Name) in $BundleDir"
        }
    }

    Test-ToolExecutable (Join-Path $BundleDir 'ffmpeg.exe') @('-version') 'Bundled ffmpeg' 'ffmpeg version'
    Test-ToolExecutable (Join-Path $BundleDir 'ffprobe.exe') @('-version') 'Bundled ffprobe' 'ffprobe version'
    Test-ToolExecutable $MkvmergeExe @('--version') 'mkvmerge' 'mkvmerge v'
    Test-ToolExecutable $MP4BoxExe @('-version') 'MP4Box' 'MP4Box.*GPAC version'
}

Write-Host 'Tool discovery:' -ForegroundColor White
if ($FpcExe)      { Write-Host "  fpc.exe      : $FpcExe"      -ForegroundColor Cyan }
else              { Write-Host '  fpc.exe      : NOT FOUND'     -ForegroundColor Red  }
if ($LazBuildExe) { Write-Host "  lazbuild.exe : $LazBuildExe" -ForegroundColor Cyan }
else              { Write-Host '  lazbuild.exe : NOT FOUND'     -ForegroundColor Red  }
if (-not $LibraryOnly) {
    Write-Host "  ffmpeg.exe   : $(Join-Path $BundleDir 'ffmpeg.exe')" -ForegroundColor Cyan
    Write-Host "  ffprobe.exe  : $(Join-Path $BundleDir 'ffprobe.exe')" -ForegroundColor Cyan
    Write-Host "  mkvmerge.exe : $MkvmergeExe" -ForegroundColor Cyan
    Write-Host "  MP4Box.exe   : $MP4BoxExe" -ForegroundColor Cyan
}
Write-Host ''

if (-not $FpcExe) {
    Write-Host 'ERROR: fpc.exe not found.  Install Lazarus IDE (includes FPC).' -ForegroundColor Red
    Write-Host '       https://www.lazarus-ide.org/' -ForegroundColor Red
    exit 1
}
if (-not $CLIOnly -and -not $LibraryOnly -and -not $LazBuildExe) {
    Write-Host 'ERROR: lazbuild.exe not found.  Install Lazarus IDE.' -ForegroundColor Red
    Write-Host '       https://www.lazarus-ide.org/' -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
#  -Clean: remove intermediate files
# ---------------------------------------------------------------------------
if ($Clean) {
    $cleanDirs = @(
        (Join-Path $UnitsBase 'cli'),
        (Join-Path $UnitsBase 'gui'),
        (Join-Path $UnitsBase 'lib'),
        (Join-Path $UnitsBase 'bundle-test'),
        (Join-Path $FpcDir    'gui\lib')
    )
    foreach ($d in $cleanDirs) {
        if (Test-Path $d) {
            Write-Host "Cleaning: $d" -ForegroundColor Yellow
            Remove-Item -Recurse -Force $d
        }
    }
    Write-Host ''
}

# ---------------------------------------------------------------------------
#  Build-CLI
# ---------------------------------------------------------------------------
function Build-CLI {
    Write-Host '=== Building Pascal CLI ===' -ForegroundColor Cyan

    $UnitsDir = Join-Path $UnitsBase 'cli'
    New-Item -ItemType Directory -Force -Path $UnitsDir | Out-Null

    $OutExe  = Join-Path $FpcDir 'cli\ffmpeg_converter.exe'
    $LprFile = Join-Path $FpcDir 'cli\ffmpeg_converter_windows.lpr'

    if (-not (Test-Path $LprFile)) {
        Write-Host "ERROR: source not found: $LprFile" -ForegroundColor Red
        return $false
    }

    $FpcArgs = @(
        '-Mobjfpc', '-Sh', '-O2',
        "-Fu$FpcDir\converter",
        "-Fu$FpcDir\common",
        "-Fu$FpcDir\json",
        "-Fu$FpcDir\cli",
        "-Fu$FpcDir\platform",
        "-FU$UnitsDir",
        "-FE$FpcDir\cli",
        "-o$OutExe",
        $LprFile
    )

    Write-Host "Compiling ffmpeg_converter_windows.lpr ..."
    & $FpcExe @FpcArgs
    $rc = $LASTEXITCODE
    Write-Host ''

    if ($rc -eq 0) {
        Write-Host "CLI build succeeded." -ForegroundColor Green
        Write-Host "  Output: $OutExe" -ForegroundColor Green
        return $true
    }
    else {
        Write-Host "CLI build FAILED (exit $rc)." -ForegroundColor Red
        return $false
    }
}

# ---------------------------------------------------------------------------
#  Build-Library
# ---------------------------------------------------------------------------
function Build-Library {
    Write-Host '=== Building Pascal shared library ===' -ForegroundColor Cyan

    $UnitsDir = Join-Path $UnitsBase 'lib'
    New-Item -ItemType Directory -Force -Path $UnitsDir | Out-Null

    $OutDll = Join-Path $FpcDir 'converter\converter_pas.dll'
    $LprFile = Join-Path $FpcDir 'converter\converter_pas.lpr'
    $FpcArgs = @(
        '-Mobjfpc', '-Sh', '-O2', '-Cg',
        "-Fu$FpcDir\converter",
        "-Fu$FpcDir\common",
        "-Fu$FpcDir\json",
        "-Fu$FpcDir\platform",
        "-FU$UnitsDir",
        "-FE$FpcDir\converter",
        "-o$OutDll",
        $LprFile
    )

    & $FpcExe @FpcArgs
    $rc = $LASTEXITCODE
    if ($rc -eq 0 -and (Test-Path -LiteralPath $OutDll -PathType Leaf)) {
        Write-Host "Library build succeeded: $OutDll" -ForegroundColor Green
        return $true
    }

    Write-Host "Library build FAILED (exit $rc)." -ForegroundColor Red
    return $false
}

# ---------------------------------------------------------------------------
#  Build-GUI
# ---------------------------------------------------------------------------
function Build-GUI {
    Write-Host '=== Building Pascal GUI ===' -ForegroundColor Cyan

    $LpiFile = Join-Path $FpcDir 'gui\form.lpi'
    if (-not (Test-Path $LpiFile)) {
        Write-Host "ERROR: form.lpi not found: $LpiFile" -ForegroundColor Red
        return $false
    }

    Write-Host "Compiling form.lpi via lazbuild ..."
    Push-Location $RepoRoot
    try {
        & $LazBuildExe -B $LpiFile
        $rc = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    Write-Host ''

    # form.lpi targets ..\bin\ffmpeg_converter_gui.exe, so lazbuild writes
    # directly into fpc\bin.  Do not use the stale copy under fpc\gui here.
    $OutExe = Join-Path $BinDir 'ffmpeg_converter_gui.exe'
    if ($rc -eq 0 -and (Test-Path $OutExe)) {
        Write-Host "GUI build succeeded." -ForegroundColor Green
        Write-Host "  Output: $OutExe" -ForegroundColor Green
        return $true
    }
    else {
        Write-Host "GUI build FAILED (exit $rc)." -ForegroundColor Red
        return $false
    }
}

function Test-BundledToolResolution {
    $TestSource = Join-Path $RepoRoot 'fpc\test\test_windows_bundle_resolution.pas'
    $TestUnits = Join-Path $UnitsBase 'bundle-test'
    $TestExe = Join-Path $BinDir 'test_windows_bundle_resolution.exe'
    $DecoyExe = Join-Path $env:SystemRoot 'System32\where.exe'

    if (-not (Test-Path -LiteralPath $TestSource -PathType Leaf)) {
        throw "Bundle-resolution test source not found: $TestSource"
    }
    if (-not (Test-Path -LiteralPath $DecoyExe -PathType Leaf)) {
        throw "Windows where.exe was not found for the bundle-resolution test: $DecoyExe"
    }

    New-Item -ItemType Directory -Force -Path $TestUnits | Out-Null
    $FpcArgs = @(
        '-Mobjfpc', '-Sh', '-O2',
        "-Fu$FpcDir\common",
        "-Fu$FpcDir\platform",
        "-FU$TestUnits",
        "-FE$BinDir",
        "-o$TestExe",
        $TestSource
    )

    Write-Host 'Compiling bundled-tool resolution test ...'
    & $FpcExe @FpcArgs
    $compileCode = $LASTEXITCODE
    if ($compileCode -ne 0 -or -not (Test-Path -LiteralPath $TestExe -PathType Leaf)) {
        throw "Bundle-resolution test compilation failed (exit $compileCode)."
    }

    try {
        & $TestExe $DecoyExe
        $testCode = $LASTEXITCODE
        if ($testCode -ne 0) {
            throw "Bundle-resolution test failed (exit $testCode)."
        }
    }
    finally {
        if (Test-Path -LiteralPath $TestExe -PathType Leaf) {
            Remove-Item -LiteralPath $TestExe -Force
        }
    }
}

# ---------------------------------------------------------------------------
#  Stage-ToBin  — copy executables + bundled DLLs to fpc\bin\
# ---------------------------------------------------------------------------
function Stage-ToBin([string[]] $Exes) {
    Write-Host ''
    Write-Host '=== Staging to fpc\bin ===' -ForegroundColor Cyan
    New-Item -ItemType Directory -Force -Path $BinDir | Out-Null

    $script:StagedFileHashes = @{}

    function Copy-ReleaseFile([string] $Source, [string] $RelativePath) {
        if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
            throw "Required release file not found: $Source"
        }

        $sourcePath = (Resolve-Path -LiteralPath $Source).Path
        $destination = Join-Path $BinDir $RelativePath
        $destinationDir = Split-Path -Parent $destination
        New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null

        $hash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash
        $key = $RelativePath.ToLowerInvariant()
        if ($script:StagedFileHashes.ContainsKey($key)) {
            if ($script:StagedFileHashes[$key] -ne $hash) {
                throw "Conflicting runtime files would overwrite '$RelativePath'."
            }
            return
        }

        if ((Test-Path -LiteralPath $destination -PathType Leaf) -and
            ((Resolve-Path -LiteralPath $destination).Path -ieq $sourcePath)) {
            $script:StagedFileHashes[$key] = $hash
            return
        }

        Copy-Item -LiteralPath $Source -Destination $destination -Force
        $script:StagedFileHashes[$key] = $hash
    }

    function Copy-ReleaseTree([string] $SourceDir, [string] $RelativeDir) {
        if (-not (Test-Path -LiteralPath $SourceDir -PathType Container)) {
            return
        }

        $root = (Resolve-Path -LiteralPath $SourceDir).Path.TrimEnd('\') + '\'
        Get-ChildItem -LiteralPath $SourceDir -File -Recurse -Force | ForEach-Object {
            $relativeFile = $_.FullName.Substring($root.Length)
            $target = if ($RelativeDir) { Join-Path $RelativeDir $relativeFile } else { $relativeFile }
            Copy-ReleaseFile $_.FullName $target
        }
    }

    function Copy-ToolRuntime([string] $Executable, [string] $TargetName, [string[]] $DataDirs) {
        Copy-ReleaseFile $Executable $TargetName

        $toolDir = Split-Path -Parent $Executable
        Get-ChildItem -LiteralPath $toolDir -File -Filter '*.dll' -ErrorAction SilentlyContinue |
            ForEach-Object { Copy-ReleaseFile $_.FullName $_.Name }

        foreach ($dataDir in $DataDirs) {
            Copy-ReleaseTree (Join-Path $toolDir $dataDir) $dataDir
        }
    }

    foreach ($exe in $Exes) {
        if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
            throw "Build output missing; cannot create complete release: $exe"
        }
        Copy-ReleaseFile $exe (Split-Path -Leaf $exe)
    }

    Copy-ReleaseTree $BundleDir ''
    Copy-ToolRuntime $MkvmergeExe 'mkvmerge.exe' @('data', 'locale', 'tools')
    Copy-ToolRuntime $MP4BoxExe 'MP4Box.exe' @('share')

    $presetsFile = Join-Path $RepoRoot 'presets.json'
    if (-not (Test-Path -LiteralPath $presetsFile -PathType Leaf)) {
        throw "Required release file not found: $presetsFile"
    }
    Copy-ReleaseFile $presetsFile 'presets.json'

    foreach ($requiredName in @('ffmpeg.exe', 'ffprobe.exe', 'mkvmerge.exe', 'MP4Box.exe', 'presets.json')) {
        $requiredPath = Join-Path $BinDir $requiredName
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "Release staging incomplete: missing $requiredPath"
        }
    }

    Test-ToolExecutable (Join-Path $BinDir 'ffmpeg.exe') @('-version') 'Staged ffmpeg' 'ffmpeg version' -BundledRuntime
    Test-ToolExecutable (Join-Path $BinDir 'ffprobe.exe') @('-version') 'Staged ffprobe' 'ffprobe version' -BundledRuntime
    Test-ToolExecutable (Join-Path $BinDir 'mkvmerge.exe') @('--version') 'Staged mkvmerge' 'mkvmerge v' -BundledRuntime
    Test-ToolExecutable (Join-Path $BinDir 'MP4Box.exe') @('-version') 'Staged MP4Box' 'MP4Box.*GPAC version' -BundledRuntime
    Test-BundledToolResolution

    Write-Host ''
    Write-Host "Output directory: $BinDir" -ForegroundColor Green
    Get-ChildItem $BinDir -Filter '*.exe' | ForEach-Object {
        $sizeMB = [Math]::Round($_.Length / 1MB, 1)
        Write-Host "  $($_.Name)  ($sizeMB MB)" -ForegroundColor Green
    }
}

# ---------------------------------------------------------------------------
#  Main
# ---------------------------------------------------------------------------
$cliOk     = $true
$guiOk     = $true
$stagedExes = @()

if ($LibraryOnly) {
    $cliOk = Build-Library
}
else {
    if (-not $GUIOnly) {
        $cliOk = Build-CLI
        Write-Host ''
        if ($cliOk) {
            $stagedExes += Join-Path $FpcDir 'cli\ffmpeg_converter.exe'
        }
    }

    if (-not $CLIOnly) {
        $guiOk = Build-GUI
        if ($guiOk) {
            $stagedExes += Join-Path $BinDir 'ffmpeg_converter_gui.exe'
        }
    }
}

$allOk = $cliOk -and $guiOk
if ($allOk -and -not $LibraryOnly) {
    Stage-ToBin $stagedExes
}
Write-Host ''
if ($allOk) {
    Write-Host 'All builds succeeded.' -ForegroundColor Green
}
else {
    Write-Host 'One or more builds FAILED.' -ForegroundColor Red
}

exit $(if ($allOk) { 0 } else { 1 })
