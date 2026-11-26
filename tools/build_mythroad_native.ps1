param(
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,
    [string]$VcVarsAll,
    [switch]$GraphicsTests
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$vm = Join-Path $root 'docs\research\mythroad'
if (-not $VcVarsAll) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'vswhere.exe is unavailable; pass -VcVarsAll with the x86 build tools path.'
    }
    $installation = & $vswhere -latest -products '*' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $installation) {
        throw 'Visual Studio x86 C build tools were not found.'
    }
    $VcVarsAll = Join-Path $installation 'VC\Auxiliary\Build\vcvarsall.bat'
}
if (-not (Test-Path -LiteralPath $VcVarsAll -PathType Leaf)) {
    throw "vcvarsall.bat not found: $VcVarsAll"
}

$entry = if ($GraphicsTests) { 'tests\mythroad_native_graphics.c' } else { 'tools\mythroad_native.c' }
$files = @(& (Join-Path $root 'tools\mrp_vm_sources.ps1') -VmDirectory $vm) +
    @((Join-Path $root 'tools\mrp_vm_host.c'), (Join-Path $root $entry))
foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Source not found: $file"
    }
}

if (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path (Get-Location).Path $OutputDirectory
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Output path already exists: $OutputDirectory"
}
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null

$response = Join-Path $OutputDirectory 'compiler.rsp'
$exeName = if ($GraphicsTests) { 'mythroad-native-graphics-tests.exe' } else { 'mythroad-native.exe' }
$exe = Join-Path $OutputDirectory $exeName
$arguments = @(
    '/nologo', '/utf-8', '/std:c11', '/Od', '/Gy', '/D_CRT_SECURE_NO_WARNINGS',
    ('/I"{0}"' -f (Join-Path $vm 'include')),
    ('/Fe"{0}"' -f $exe)
)
$arguments += @($files | ForEach-Object { '"{0}"' -f $_ })
[System.IO.File]::WriteAllLines($response, $arguments, [System.Text.UTF8Encoding]::new($true))

$command = 'call "{0}" x86 >nul && cd /d "{1}" && cl @"{2}" /link /OPT:REF user32.lib gdi32.lib' -f `
    $VcVarsAll, $OutputDirectory, $response
& $env:ComSpec /c $command
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE. Outputs: $OutputDirectory"
}
Write-Output "Built: $exe"
