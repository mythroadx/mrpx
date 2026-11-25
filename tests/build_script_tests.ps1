#requires -Version 7.3
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$fixture = Join-Path ([IO.Path]::GetTempPath()) "vmrp-build-tests-$([Guid]::NewGuid().ToString('N'))"
$script = Join-Path $fixture 'scripts\build.ps1'
$seeds = Join-Path $fixture 'assets\runtime'
$output = Join-Path $fixture 'bin'

function Assert-Equal {
    param($Actual, $Expected, [string]$Message)
    if ($Actual -cne $Expected) { throw $Message }
}

function Assert-Rejected {
    param([scriptblock]$Action, [string]$ExpectedMessage)
    try { & $Action } catch {
        if ($_.Exception.Message -notlike "*$ExpectedMessage*") { throw }
        return
    }
    throw "Expected failure: $ExpectedMessage"
}

New-Item -ItemType Directory -Path (Split-Path $script -Parent), (Join-Path $seeds 'mythroad'), $output -Force | Out-Null
try {
    Copy-Item -LiteralPath (Join-Path $root 'scripts\build.ps1') -Destination $script
    Set-Content -LiteralPath (Join-Path $seeds 'manifest.json') -Value '{"files":["mythroad/example.mrp"]}'
    Set-Content -LiteralPath (Join-Path $seeds 'mythroad\example.mrp') -Value 'seed'
    Set-Content -LiteralPath (Join-Path $seeds 'uncatalogued.mrp') -Value 'not a seed'
    Set-Content -LiteralPath (Join-Path $output 'application-data.txt') -Value 'preserved' -NoNewline
    Push-Location $output
    try { & $script runtime } finally { Pop-Location }
    Assert-Equal (Get-Content -LiteralPath (Join-Path $output 'application-data.txt') -Raw) 'preserved' 'Runtime command overwrote application data.'
    Assert-Equal (Get-FileHash -LiteralPath (Join-Path $output 'mythroad\example.mrp')).Hash `
        (Get-FileHash -LiteralPath (Join-Path $seeds 'mythroad\example.mrp')).Hash 'Runtime seed bytes differ.'
    Assert-Equal (Test-Path -LiteralPath (Join-Path $output 'uncatalogued.mrp')) $false 'Runtime copied an unlisted asset.'
    Write-Host 'PASS: runtime command resolves repository paths and preserves application data.'

    Assert-Rejected { & $script runtime -Destination $seeds } 'must not overwrite'
    Assert-Rejected { & $script runtime -Destination (Join-Path $seeds 'nested') } 'must not overwrite'
    Assert-Rejected { & $script web -Destination $output } 'only supported by runtime'
    Assert-Rejected { & $script web -CMake 'unused-cmake' } 'only supported by unicorn'
    foreach ($tag in @('v1.2', 'v1.2.3-rc.1', 'v1.2.3+build', 'v01.2.3', '1.2.3', 'V1.2.3', '../v1.2.3')) {
        Assert-Rejected { & $script package -Version $tag -Platform wasm } 'package requires'
    }
    Assert-Rejected { & $script package -Version v1.2.3 } 'package requires'
    Assert-Rejected { & $script web -Version v1.2.3 } 'only supported by package'
    Set-Content -LiteralPath (Join-Path $seeds 'manifest.json') -Value '{"files":["../outside.mrp"]}'
    Assert-Rejected { & $script runtime } 'Invalid runtime asset path'
    Set-Content -LiteralPath (Join-Path $seeds 'manifest.json') -Value '{"files":["mythroad/example.mrp"]}'
    Write-Host 'PASS: commands reject misplaced options, seed-tree output and escaping manifest paths.'

    Assert-Rejected { & $script web } 'Missing local MIDI asset'
    $audio = Join-Path $fixture 'build\web\vendor\spessasynth'
    $web = Join-Path $fixture 'wasm\web'
    New-Item -ItemType Directory -Path $audio, $web -Force | Out-Null
    foreach ($name in @('index.js', 'spessasynth_processor.min.js', 'GeneralUser-GS.sf2', 'GeneralUser-GS-LICENSE.txt',
        'spessasynth_lib-LICENSE.txt', 'spessasynth_core-LICENSE.txt', 'fflate-LICENSE.txt', 'stb-vorbis-LICENSE.txt')) {
        Set-Content -LiteralPath (Join-Path $audio $name) -Value 'fixture asset'
    }
    foreach ($name in @('main.html', 'styles.css', 'controls.js', 'midi.js', 'filesystem.js', 'catalog.js')) {
        Set-Content -LiteralPath (Join-Path $web $name) -Value "fixture $name"
    }
    New-Item -ItemType Directory -Path (Join-Path $web 'catalog\previews') -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $web 'catalog\catalog.json') -Value '{"schema_version":1,"packages":[]}'
    Set-Content -LiteralPath (Join-Path $web 'catalog\previews\example.png') -Value 'fixture screenshot'
    & $script web
    Assert-Equal (Get-Content -LiteralPath (Join-Path $fixture 'build\web\styles.css') -Raw) `
        (Get-Content -LiteralPath (Join-Path $web 'styles.css') -Raw) 'Web command did not stage CSS.'
    Assert-Equal (Get-FileHash -LiteralPath (Join-Path $fixture 'build\web\runtime\mythroad\example.mrp')).Hash `
        (Get-FileHash -LiteralPath (Join-Path $seeds 'mythroad\example.mrp')).Hash 'Web command did not stage runtime seeds.'
    Assert-Equal (Get-FileHash -LiteralPath (Join-Path $fixture 'build\web\catalog\previews\example.png')).Hash `
        (Get-FileHash -LiteralPath (Join-Path $web 'catalog\previews\example.png')).Hash 'Web command did not stage catalog screenshots.'
    Set-Content -LiteralPath (Join-Path $fixture 'build\web\catalog\previews\obsolete.png') -Value 'obsolete'
    Set-Content -LiteralPath (Join-Path $fixture 'build\web\runtime\mythroad\removed.mrp') -Value 'obsolete runtime seed'
    & $script web
    Assert-Equal (Test-Path -LiteralPath (Join-Path $fixture 'build\web\catalog\previews\obsolete.png')) $false `
        'Web command retained a removed catalog screenshot.'
    Assert-Equal (Test-Path -LiteralPath (Join-Path $fixture 'build\web\runtime\mythroad\removed.mrp')) $false `
        'Web command retained a removed runtime seed.'
    Set-Content -LiteralPath (Join-Path $web 'catalog\example.mrp') -Value 'must not ship'
    Assert-Rejected { & $script web } 'Unexpected catalog asset'
    Remove-Item -LiteralPath (Join-Path $web 'catalog\example.mrp')
    Write-Host 'PASS: web command checks audio dependencies and stages frontend plus runtime together.'

    foreach ($name in @(($IsWindows ? '.bin\esbuild.cmd' : '.bin\esbuild'), 'spessasynth_lib\dist\index.js',
        'spessasynth_lib\dist\spessasynth_processor.min.js', 'spessasynth_lib\LICENSE',
        'spessasynth_core\LICENSE', 'fflate\LICENSE', 'stb-vorbis\LICENSE')) {
        $file = Join-Path $fixture "node_modules\$name"
        New-Item -ItemType Directory -Path (Split-Path $file -Parent) -Force | Out-Null
        Set-Content -LiteralPath $file -Value 'fixture dependency'
    }
    $cache = Join-Path $fixture 'build\web-audio-downloads'
    New-Item -ItemType Directory -Path $cache -Force | Out-Null
    $font = Join-Path $cache 'GeneralUser-GS.sf2'
    Set-Content -LiteralPath $font -Value 'invalid cached font'
    Assert-Rejected { & $script audio } 'SHA-256 mismatch'
    Assert-Equal (Get-Content -LiteralPath (Join-Path $audio 'GeneralUser-GS.sf2') -Raw) `
        "fixture asset$([Environment]::NewLine)" 'Invalid cached font replaced the published asset.'
    Remove-Item -LiteralPath $font
    function Invoke-WebRequest {
        param([string]$Uri, [int]$TimeoutSec, [string]$OutFile)
        Set-Content -LiteralPath $OutFile -Value 'invalid downloaded font'
    }
    try {
        Assert-Rejected { & $script audio } 'SHA-256 mismatch'
    } finally {
        Remove-Item Function:\Invoke-WebRequest
    }
    Assert-Equal (Test-Path -LiteralPath $font) $false 'Invalid download entered the cache.'
    Assert-Equal @(Get-ChildItem -LiteralPath $cache -Filter '*.download').Count 0 'Failed download left a temporary file.'
    Write-Host 'PASS: audio command rejects corrupt cached/downloaded fonts and cleans failed downloads.'

    $archiveSource = Join-Path $fixture "archive input's files"
    $archiveOutput = Join-Path $fixture "archive output's files"
    $archive = Join-Path $fixture "sdk archive's files.tar.gz"
    New-Item -ItemType Directory -Path (Join-Path $archiveSource 'sdk'), $archiveOutput -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $archiveSource 'sdk\input.txt') -Value 'archive fixture' -NoNewline
    $tarCommand = $IsWindows ? (Join-Path $env:SystemRoot 'System32\tar.exe') : (Get-Command tar).Source
    & $tarCommand -czf $archive -C $archiveSource sdk
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the archive fixture.' }
    & {
        . $script help | Out-Null
        if ($IsWindows) {
            function tar { throw 'Windows extraction must not use tar from PATH.' }
        }
        Expand-BuildArchive -Archive $archive -Destination $archiveOutput -StripRoot
        Assert-Equal (Get-Content -LiteralPath (Join-Path $archiveOutput 'input.txt') -Raw) `
            'archive fixture' 'Archive extraction damaged paths or failed to strip the root.'
        Expand-BuildArchive -Archive $archive -Destination $archiveOutput
        Assert-Equal (Get-Content -LiteralPath (Join-Path $archiveOutput 'sdk\input.txt') -Raw) `
            'archive fixture' 'Archive extraction did not preserve the root.'
    }
    Write-Host 'PASS: SDK archives extract from absolute paths with and without their root directory.'

    Copy-Item -LiteralPath (Join-Path $root 'Makefile') -Destination $fixture
    Copy-Item -LiteralPath (Join-Path $root 'wasm\config.mk') -Destination (Join-Path $fixture 'wasm\config.mk')
    $PSNativeCommandUseErrorActionPreference = $false
    $rejected = & make -C $fixture package 'VERSION=v1.2.3"; touch injected; #' PLATFORM=wasm 2>&1
    Assert-Equal ($LASTEXITCODE -ne 0) $true 'Make swallowed the package validation failure.'
    if (($rejected -join "`n") -notlike '*package requires*') {
        throw "Make did not reach package validation: $rejected"
    }
    Assert-Equal (Test-Path -LiteralPath (Join-Path $fixture 'injected')) $false 'Make evaluated a release parameter as shell code.'
    $PSNativeCommandUseErrorActionPreference = $true
    Set-Content -LiteralPath $script -Value @'
param($Command, $CMake, [int]$Jobs, $UnicornRoot, $SDL2Root, $Destination)
$PSBoundParameters | ConvertTo-Json | Set-Content (Join-Path (Split-Path $PSScriptRoot -Parent) 'invocation.json')
'@
    & make -C $fixture native-deps "UNICORN_ROOT=sdk root's files" "SDL2_ROOT=sdl root's files" "NATIVE_CMAKE=cmake root's tool.exe" JOBS=2
    $invocation = Get-Content -LiteralPath (Join-Path $fixture 'invocation.json') -Raw | ConvertFrom-Json
    Assert-Equal $invocation.Command 'native-deps' 'Make invoked the wrong helper command.'
    Assert-Equal $invocation.UnicornRoot "sdk root's files" 'Make damaged the Unicorn SDK path.'
    Assert-Equal $invocation.SDL2Root "sdl root's files" 'Make damaged the SDL2 SDK path.'
    Assert-Equal $invocation.CMake "cmake root's tool.exe" 'Make damaged the CMake path.'
    Assert-Equal $invocation.Jobs 2 'Make did not forward SDK parallelism.'
    & make -C $fixture prepare-runtime "RUNTIME_DIR=runtime root's files"
    $invocation = Get-Content -LiteralPath (Join-Path $fixture 'invocation.json') -Raw | ConvertFrom-Json
    Assert-Equal $invocation.Destination "runtime root's files" 'Make damaged the runtime destination.'
    Write-Host 'PASS: Make forwards spaced/quoted paths as data and propagates package validation failures.'

    $npm = Join-Path $fixture "npm root's tool.ps1"
    Set-Content -LiteralPath $npm -Value @'
param($Command)
$Command | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'npm-command.txt')
'@
    & make -C $fixture web-deps "NPM=$npm"
    Assert-Equal $LASTEXITCODE 0 'Make failed to invoke the npm tool path.'
    Assert-Equal (Get-Content -LiteralPath (Join-Path $fixture 'npm-command.txt') -Raw).Trim() `
        'ci' 'Make did not request a locked dependency install.'
    Set-Content -LiteralPath $npm -Value 'exit 17'
    $PSNativeCommandUseErrorActionPreference = $false
    $failedInstall = & make -C $fixture web-deps "NPM=$npm" 2>&1
    Assert-Equal ($LASTEXITCODE -ne 0) $true "Make swallowed the npm install failure: $failedInstall"
    $PSNativeCommandUseErrorActionPreference = $true
    Write-Host 'PASS: Make invokes spaced/quoted npm paths and propagates install failures.'
} finally {
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
