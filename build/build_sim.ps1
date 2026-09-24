# =============================================================================
#  build_sim.ps1 - RoboControl V1.0 host-simulation build script
#
#  Usage (from the RoboControl directory):
#      powershell -ExecutionPolicy Bypass -File build\build_sim.ps1
#      powershell -ExecutionPolicy Bypass -File build\build_sim.ps1 -Target tests -Werror
#      powershell -ExecutionPolicy Bypass -File build\build_sim.ps1 -Target clean
#
#  Compiler selection order:
#      1) environment variable RC_CC (full path to a C compiler)
#      2) gcc / clang found on PATH
#      3) py -3 -m ziglang cc   (pip install ziglang - a complete C toolchain)
#
#  NOTE: this file is intentionally ASCII-only. Windows PowerShell 5.1 reads
#  .ps1 files without a BOM as ANSI, which corrupts non-ASCII comments and
#  produces bogus parser errors. Keeping build scripts 7-bit avoids an entire
#  class of "works on my machine" failures. Chinese documentation lives in
#  docs\ (UTF-8 Markdown).
#
#  Why compile file-by-file and then link instead of one cc call with every
#  .c file: clang/zig cc compile multiple inputs in parallel and interleave
#  their diagnostics, which makes a failing build nearly undebuggable.
# =============================================================================

param(
    [ValidateSet('tests', 'demo', 'all', 'clean')]
    [string]$Target = 'all',

    [switch]$Werror
)

$ErrorActionPreference = 'Stop'

$Root   = Split-Path -Parent $PSScriptRoot
$Build  = Join-Path $Root 'build'
$ObjDir = Join-Path $Build 'obj'

# ---------------------------------------------------------------- clean
if ($Target -eq 'clean') {
    if (Test-Path -LiteralPath $ObjDir) { Remove-Item -LiteralPath $ObjDir -Recurse -Force }
    foreach ($f in @('tests.exe', 'sim_demo.exe')) {
        $p = Join-Path $Build $f
        if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
    }
    Write-Host '[build] cleaned'
    exit 0
}

if (-not (Test-Path -LiteralPath $ObjDir)) {
    New-Item -ItemType Directory -Path $ObjDir -Force | Out-Null
}

# ---------------------------------------------------------------- toolchain
$CcExe = $null
$CcPre = @()

if ($env:RC_CC) {
    $CcExe = $env:RC_CC
} else {
    foreach ($name in @('gcc', 'clang')) {
        $c = Get-Command $name -ErrorAction SilentlyContinue
        if ($c) { $CcExe = $c.Source; break }
    }
}

if (-not $CcExe) {
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        & $py.Source -3 -m ziglang version *> $null
        if ($LASTEXITCODE -eq 0) {
            $CcExe = $py.Source
            $CcPre = @('-3', '-m', 'ziglang', 'cc')
        }
    }
}

if (-not $CcExe) {
    throw 'No C compiler found. Install gcc/clang, or run: pip install ziglang'
}

Write-Host ("[build] compiler : {0} {1}" -f $CcExe, ($CcPre -join ' '))

# ---------------------------------------------------------------- flags
# -Wconversion / -Wsign-conversion are deliberately on: most defects in this
# style of code come from implicit narrowing (lengths, indices, tick
# arithmetic), and that warning is far cheaper than a field investigation.
$CFlags = @(
    '-std=c11',
    '-Wall', '-Wextra', '-Wpedantic', '-Wshadow',
    '-Wconversion', '-Wsign-conversion',
    '-Wundef', '-Wmissing-prototypes', '-Wstrict-prototypes',
    '-O2', '-g'
)
if ($Werror) { $CFlags += '-Werror' }

$IncDirs = @(
    'Common', 'Control', 'Simulation', 'Service',
    'RTOS', 'RTOS\port_sim', 'BSP', 'Application', 'Core',
    'Tests', 'Sim'
)
$Inc = $IncDirs | ForEach-Object { '-I' + (Join-Path $Root $_) }

# ---------------------------------------------------------------- sources
# Portable product code: no RTOS, no HAL, no UART (ICD section 23).
$PortableSrc = @(
    'Control\pid.c',
    'Simulation\virtual_motor.c',
    'Service\motor_manager.c',
    'Service\command_service.c',
    'Service\monitor_service.c'
)

# Host-simulation platform layer (RTOS + BSP).
$SimPlatformSrc = @(
    'RTOS\port_sim\port_sim.c',
    'BSP\bsp_uart_sim.c'
)

# RTOS task layer - identical source on host and on STM32.
$TaskSrc = @(
    'RTOS\task_control.c',
    'RTOS\task_motor.c',
    'RTOS\task_command.c',
    'RTOS\task_monitor.c',
    'RTOS\task_debug.c'
)

$AppSrc = @(
    'Application\app.c'
)

$TestSrc = @(
    'test_framework.c',
    'test_config.c',
    'test_pid.c',
    'test_virtual_motor.c',
    'test_motor_manager.c',
    'test_command_service.c',
    'test_monitor_service.c',
    'test_closed_loop.c',
    'test_main.c'
)

$DemoSrc = @(
    'Sim\sim_demo.c'
)

# Compiled for syntax checking only (it defines main(), so it is never linked
# into either executable). This is the product entry point used on STM32.
$CheckOnlySrc = @(
    'Core\main.c'
)

function ConvertTo-ObjPath {
    param([string]$RelativeSource)
    $flat = ($RelativeSource -replace '[\\/]', '_') -replace '\.c$', '.o'
    return (Join-Path $ObjDir $flat)
}

function Invoke-CC {
    param([string[]]$Arguments, [string]$Step)

    # Compiler diagnostics arrive on native stderr. With $ErrorActionPreference
    # set to Stop, PowerShell turns that into a terminating error and discards
    # the message body. Redirect to a file and echo it back so the real error
    # is visible regardless of how the script is invoked.
    $tmp = Join-Path $ObjDir 'cc-step.log'
    & $CcExe @($CcPre + $Arguments) *> $tmp
    $code = $LASTEXITCODE

    if (Test-Path -LiteralPath $tmp) {
        $text = [string](Get-Content -LiteralPath $tmp -Raw)
        if ($text -and $text.Trim().Length -gt 0) { Write-Host $text }
    }

    if ($code -ne 0) {
        throw ("build step failed (exit {0}): {1}" -f $code, $Step)
    }
}

function Build-Sources {
    param([string[]]$Sources)

    $objs = @()
    foreach ($rel in $Sources) {
        $src = Join-Path $Root $rel
        $obj = ConvertTo-ObjPath $rel
        Write-Host ("[build] cc  {0}" -f $rel)
        Invoke-CC ($CFlags + $Inc + @('-c', $src, '-o', $obj)) ("cc " + $rel)
        $objs += $obj
    }
    return ,$objs
}

$productSrc = $PortableSrc + $SimPlatformSrc + $TaskSrc + $AppSrc

$testSources = @()
foreach ($f in $TestSrc) { $testSources += ('Tests\' + $f) }

$demoSources = @()
foreach ($f in $DemoSrc) { $demoSources += $f }

# ---------------------------------------------------------------- targets
if (($Target -eq 'tests') -or ($Target -eq 'all')) {
    $null = Build-Sources -Sources $CheckOnlySrc   # syntax check only, never linked
    $objs = Build-Sources -Sources ($productSrc + $testSources)
    $exe  = Join-Path $Build 'tests.exe'
    Write-Host '[build] link tests.exe'
    Invoke-CC ($objs + @('-o', $exe)) 'link tests.exe'
    Write-Host ("[build] OK -> {0}" -f $exe)
}

if (($Target -eq 'demo') -or ($Target -eq 'all')) {
    $objs = Build-Sources -Sources ($productSrc + $demoSources)
    $exe  = Join-Path $Build 'sim_demo.exe'
    Write-Host '[build] link sim_demo.exe'
    Invoke-CC ($objs + @('-o', $exe)) 'link sim_demo.exe'
    Write-Host ("[build] OK -> {0}" -f $exe)
}
