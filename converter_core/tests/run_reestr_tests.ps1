# Tests for the converter_core registry (GetReestr), Windows.
# Builds tests/reestr_tests.pas into a temporary folder with heap and range
# checks and runs it. Fails on any FAIL line or unfreed memory.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File converter_core/tests/run_reestr_tests.ps1
#   ... run_reestr_tests.ps1 -Dump windows_nvenc7 windows_vulkan_nvidia   # print the registry tree
#   ... run_reestr_tests.ps1 -Detected                                     # tree for this machine
#
# FPC is taken from -Fpc, $env:FPC, PATH or the default Lazarus location.

[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Fpc = '',
    [switch]$Detected,
    [switch]$Dump,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Platforms = @()
)

$ErrorActionPreference = 'Stop'

$CoreDir = Split-Path -Parent $PSScriptRoot
$Source = Join-Path $PSScriptRoot 'reestr_tests.pas'

if (-not $Fpc) { $Fpc = $env:FPC }
if (-not $Fpc) {
    $cmd = Get-Command fpc -ErrorAction SilentlyContinue
    if ($cmd) { $Fpc = $cmd.Source }
}
if (-not $Fpc) {
    $Fpc = Get-ChildItem 'C:\lazarus\fpc\*\bin\x86_64-win64\fpc.exe' -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
}
if (-not $Fpc -or -not (Test-Path $Fpc)) {
    Write-Host 'NOT AVAILABLE  fpc not found (use -Fpc or $env:FPC)'
    exit 2
}

$Work = Join-Path ([IO.Path]::GetTempPath()) ('reestr_tests_' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $Work | Out-Null
try {
    $Exe = Join-Path $Work 'reestr_tests.exe'
    # -gh: report memory leaks; -Cr -Co -Ct: range, overflow and stack checks
    $build = & $Fpc -Mobjfpc -Sh -gh -gl -Cr -Co -Ct -vew "-Fu$CoreDir\src" "-FU$Work" "-o$Exe" $Source 2>&1
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $Exe)) {
        $build | ForEach-Object { Write-Host $_ }
        Write-Host 'FAIL  build'
        exit 1
    }

    # heaptrc writes its report to stderr; Windows PowerShell 5 must not treat it as an error
    $ErrorActionPreference = 'Continue'
    # HQ_converter release used by the HQ checks and by -Dump hq-converter
    $HqArgs = @('--hq', (Join-Path (Split-Path -Parent $CoreDir) 'third_party\hq_converter'))
    if ($Dump) { & $Exe @HqArgs --dump @Platforms; exit $LASTEXITCODE }
    if ($Detected) { & $Exe --detected; exit $LASTEXITCODE }

    $output = & $Exe @HqArgs 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $output | Where-Object { $_ -match '^(PASS|FAIL|INFO)|passed,' } | ForEach-Object { Write-Host $_ }

    $leak = $output | Where-Object { $_ -match '^(\d+) unfreed memory blocks' -and [int]$Matches[1] -gt 0 }
    if ($leak) {
        Write-Host "FAIL  memory: $leak"
        exit 1
    }
    exit $code
}
finally {
    Remove-Item $Work -Recurse -Force -ErrorAction SilentlyContinue
}
