#requires -Version 7.3
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('help', 'runtime', 'web', 'audio', 'unicorn', 'native-deps', 'package', 'desktop', 'debug', 'wasm', 'de')]
    [string]$Command = 'help',
    [string]$Destination = 'bin',
    [string]$CMake = 'cmake',
    [string]$SourceDirectory = 'build/unicorn-wasm/source',
    [string]$EngineDirectory = 'build/unicorn-wasm/engine',
    [ValidateRange(1, 2147483647)]
    [int]$Jobs = 4,
    [string]$Version,
    [ValidateSet('windows', 'linux', 'wasm')]
    [string]$Platform,
    [string]$UnicornRoot = 'deps/unicorn',
    [string]$SDL2Root = 'deps/SDL2-2.32.10/x86_64-w64-mingw32'
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
if ($PSBoundParameters.ContainsKey('Destination') -and $Command -ne 'runtime') {
    throw '-Destination is only supported by runtime.'
}
foreach ($name in @('CMake', 'SourceDirectory', 'EngineDirectory', 'Jobs')) {
    $allowed = $name -in @('CMake', 'Jobs') ? @('unicorn', 'native-deps') : @('unicorn')
    if ($PSBoundParameters.ContainsKey($name) -and $Command -notin $allowed) {
        throw "-$name is only supported by unicorn or native-deps (source/engine paths: unicorn only)."
    }
}
foreach ($name in @('Version', 'Platform')) {
    if ($PSBoundParameters.ContainsKey($name) -and $Command -ne 'package') {
        throw "-$name is only supported by package."
    }
}
foreach ($name in @('UnicornRoot', 'SDL2Root')) {
    if ($PSBoundParameters.ContainsKey($name) -and $Command -notin @('package', 'native-deps')) {
        throw "-$name is only supported by package or native-deps."
    }
}
if ($Command -eq 'package' -and ($Version -cnotmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' -or !$Platform)) {
    throw 'package requires -Version vX.Y.Z (no suffix or leading zeros) and -Platform windows|linux|wasm.'
}
$root = Split-Path $PSScriptRoot -Parent
$webOutput = Join-Path $root 'build/web'
$audioOutput = Join-Path $webOutput 'vendor/spessasynth'
$audioPackages = @('spessasynth_lib', 'spessasynth_core', 'fflate', 'stb-vorbis')
$audioFiles = @('index.js', 'spessasynth_processor.min.js', 'GeneralUser-GS.sf2',
    'GeneralUser-GS-LICENSE.txt') + @($audioPackages | ForEach-Object { "$_-LICENSE.txt" })

function Copy-RuntimeAssets {
    param([string]$OutputDirectory)

    $source = Join-Path $root 'assets/runtime'
    $output = [IO.Path]::GetFullPath($OutputDirectory, $root)
    $manifest = Get-Content -LiteralPath (Join-Path $source 'manifest.json') -Raw | ConvertFrom-Json
    if ($output -eq $source -or $output.StartsWith($source + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Runtime output must not overwrite the seed assets.'
    }
    foreach ($file in $manifest.files) {
        if ([IO.Path]::IsPathRooted($file) -or $file -match '(^|[/\\])\.\.([/\\]|$)') {
            throw "Invalid runtime asset path: $file"
        }
        if (-not (Test-Path -LiteralPath (Join-Path $source $file) -PathType Leaf)) {
            throw "Missing runtime seed asset: $file"
        }
    }
    foreach ($file in $manifest.files) {
        $target = Join-Path $output $file
        New-Item -ItemType Directory -Path (Split-Path $target -Parent) -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $source $file) -Destination $target -Force
    }
    Copy-Item -LiteralPath (Join-Path $source 'manifest.json') -Destination (Join-Path $output 'manifest.json') -Force
    Write-Host "Prepared runtime seeds in $output (application data preserved)."
}

function Prepare-Web {
    foreach ($name in $audioFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $audioOutput $name) -PathType Leaf)) {
            throw "Missing local MIDI asset $name; run make web-deps and make prepare-web-audio."
        }
    }
    New-Item -ItemType Directory -Path $webOutput -Force | Out-Null
    foreach ($name in @('main.html', 'styles.css', 'controls.js', 'midi.js', 'filesystem.js', 'catalog.js')) {
        Copy-Item -LiteralPath (Join-Path $root "wasm/web/$name") -Destination (Join-Path $webOutput $name) -Force
    }
    $catalog = Join-Path $root 'wasm/web/catalog'
    if (-not (Test-Path -LiteralPath (Join-Path $catalog 'catalog.json') -PathType Leaf)) {
        throw 'Missing catalog.json; restore the versioned wasm/web/catalog assets or run make catalog MRP_SOURCE=...'
    }
    New-Item -ItemType Directory -Path (Join-Path $webOutput 'catalog') -Force | Out-Null
    foreach ($file in Get-ChildItem -LiteralPath $catalog -Recurse -File) {
        if ($file.Extension -notin @('.json', '.png')) { throw "Unexpected catalog asset: $($file.FullName)" }
        $relative = [IO.Path]::GetRelativePath($catalog, $file.FullName)
        Copy-PackageFile $file.FullName (Join-Path $webOutput "catalog/$relative")
    }
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $webOutput 'catalog') -Recurse -File) {
        $relative = [IO.Path]::GetRelativePath((Join-Path $webOutput 'catalog'), $file.FullName)
        if (-not (Test-Path -LiteralPath (Join-Path $catalog $relative) -PathType Leaf)) {
            Remove-Item -LiteralPath $file.FullName
        }
    }
    Copy-RuntimeAssets (Join-Path $webOutput 'runtime')
    $runtime = Join-Path $webOutput 'runtime'
    $runtimeManifest = Get-Content -LiteralPath (Join-Path $runtime 'manifest.json') -Raw | ConvertFrom-Json
    foreach ($file in Get-ChildItem -LiteralPath $runtime -Recurse -File) {
        $relative = [IO.Path]::GetRelativePath($runtime, $file.FullName).Replace('\', '/')
        if ($relative -ne 'manifest.json' -and $relative -notin $runtimeManifest.files) {
            Remove-Item -LiteralPath $file.FullName
        }
    }
    Write-Host 'Prepared web sources and runtime in build/web; deploy this directory after the WASM build.'
}

function Save-AudioAsset {
    param([string]$RelativeUrl, [string]$Name, [string]$Hash)

    $cache = Join-Path $root 'build/web-audio-downloads'
    $destination = Join-Path $cache $Name
    $temporary = "$destination.$([Guid]::NewGuid().ToString('N')).download"
    $downloaded = -not (Test-Path -LiteralPath $destination -PathType Leaf)
    try {
        $inputFile = $destination
        if ($downloaded) {
            $revision = '684543d5e5efaef08d02be50dcda8d552478fa60'
            Write-Host "Downloading $Name from pinned GeneralUser GS revision..."
            Invoke-WebRequest "https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/$revision/$RelativeUrl" `
                -TimeoutSec 180 -OutFile $temporary
            $inputFile = $temporary
        }
        if ($Hash -and (Get-FileHash -LiteralPath $inputFile -Algorithm SHA256).Hash -ne $Hash) {
            throw "${Name}: SHA-256 mismatch; remove $destination and retry."
        }
        if ($downloaded) {
            Move-Item -LiteralPath $temporary -Destination $destination -Force
        }
        Copy-Item -LiteralPath $destination -Destination (Join-Path $audioOutput $Name) -Force
    } finally {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
    }
}

function Prepare-Audio {
    Get-Command node -ErrorAction Stop | Out-Null
    $esbuild = Join-Path $root ($IsWindows ? 'node_modules/.bin/esbuild.cmd' : 'node_modules/.bin/esbuild')
    $entry = Join-Path $root 'node_modules/spessasynth_lib/dist/index.js'
    $processor = Join-Path $root 'node_modules/spessasynth_lib/dist/spessasynth_processor.min.js'
    $inputs = @($esbuild, $entry, $processor) +
        @($audioPackages | ForEach-Object { Join-Path $root "node_modules/$_/LICENSE" })
    foreach ($inputFile in $inputs) {
        if (-not (Test-Path -LiteralPath $inputFile -PathType Leaf)) {
            throw "Missing npm dependency file $inputFile; run make web-deps."
        }
    }
    $cache = Join-Path $root 'build/web-audio-downloads'
    New-Item -ItemType Directory -Path $audioOutput, $cache -Force | Out-Null
    Save-AudioAsset 'GeneralUser-GS.sf2' 'GeneralUser-GS.sf2' `
        '9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe'
    Save-AudioAsset 'documentation/LICENSE.txt' 'GeneralUser-GS-LICENSE.txt'

    $metadata = Join-Path $cache 'bundle-meta.json'
    & $esbuild $entry --bundle --format=esm --platform=browser --target=es2022 `
        --legal-comments=linked "--outfile=$(Join-Path $audioOutput 'index.js')" "--metafile=$metadata"
    $bundle = Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json -AsHashtable
    foreach ($output in $bundle.outputs.Values) {
        foreach ($import in $output.imports) {
            if ($import.external) {
                throw 'The MIDI browser bundle must not contain external imports.'
            }
        }
    }
    Copy-Item -LiteralPath $processor -Destination (Join-Path $audioOutput 'spessasynth_processor.min.js') -Force
    foreach ($name in $audioPackages) {
        Copy-Item -LiteralPath (Join-Path $root "node_modules/$name/LICENSE") `
            -Destination (Join-Path $audioOutput "$name-LICENSE.txt") -Force
    }
    Write-Host 'Prepared local SpessaSynth 4.4.10 + GeneralUser GS 2.0.3 (32.3 MB SoundFont).'
}

function Get-CMakePath {
    $cmakeCommand = (Get-Command $CMake -ErrorAction Stop).Source
    if ($IsWindows -and $cmakeCommand -match '[\\/]usr[\\/]bin[\\/]cmake(\.exe)?$') {
        throw 'Use native Windows CMake, not MSYS CMake. Pass its full path with -CMake.'
    }
    return $cmakeCommand
}

function Save-BuildDownload {
    param([string]$Url, [string]$Name, [string]$Hash)

    $cache = Join-Path $root 'deps/downloads'
    New-Item -ItemType Directory -Path $cache -Force | Out-Null
    $file = Join-Path $cache $Name
    $temporary = "$file.$([Guid]::NewGuid().ToString('N')).download"
    try {
        if (!(Test-Path -LiteralPath $file -PathType Leaf)) {
            Invoke-WebRequest $Url -OutFile $temporary -TimeoutSec 180
            if ((Get-FileHash -LiteralPath $temporary).Hash -ne $Hash) { throw "$Name SHA-256 mismatch." }
            Move-Item -LiteralPath $temporary -Destination $file -Force
        }
        if ((Get-FileHash -LiteralPath $file).Hash -ne $Hash) { throw "$Name cached SHA-256 mismatch." }
        return $file
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force }
    }
}

function Expand-BuildArchive {
    param([string]$Archive, [string]$Destination, [switch]$StripRoot)

    # MSYS tar treats a Windows drive letter in -f as a remote host.
    $tarCommand = $IsWindows ? (Join-Path $env:SystemRoot 'System32/tar.exe') : 'tar'
    $tarCommand = (Get-Command $tarCommand -ErrorAction Stop).Source
    $options = @('-xzf', $Archive, '-C', $Destination)
    if ($StripRoot) { $options += '--strip-components=1' }
    & $tarCommand @options
}

function Prepare-NativeDependencies {
    Get-Command gcc, ninja -ErrorAction Stop | Out-Null
    $cmakeCommand = Get-CMakePath
    $source = [IO.Path]::GetFullPath($UnicornRoot, $root)
    if ($source -match '\s') { throw 'Native Unicorn source path must not contain spaces; choose -UnicornRoot in a short SDK directory.' }
    $marker = Join-Path $source '.vmrp-native-version'
    if (Test-Path -LiteralPath $marker) {
        if ((Get-Content -LiteralPath $marker -Raw).Trim() -ne '2.1.4') { throw 'Native Unicorn version mismatch.' }
    } else {
        if ((Test-Path -LiteralPath $source) -and @(Get-ChildItem -LiteralPath $source -Force).Count) {
            throw 'Native Unicorn output must be empty; use -UnicornRoot for a separate source SDK.'
        }
        $archive = Save-BuildDownload 'https://github.com/unicorn-engine/unicorn/archive/refs/tags/2.1.4.tar.gz' `
            'unicorn-2.1.4.tar.gz' 'ea8863f095a0136388694e5a6063afd9bb7650e30243dd6251af59c5ce5601f4'
        New-Item -ItemType Directory -Path $source -Force | Out-Null
        Expand-BuildArchive -Archive $archive -Destination $source -StripRoot
        Set-Content -LiteralPath $marker -Value '2.1.4'
    }
    if ($IsWindows) {
        $sdl = [IO.Path]::GetFullPath($SDL2Root, $root)
        if (!(Test-Path -LiteralPath (Join-Path $sdl 'bin/SDL2.dll'))) {
            if ($SDL2Root -ne 'deps/SDL2-2.32.10/x86_64-w64-mingw32') { throw 'Custom SDL2 SDK is missing SDL2.dll.' }
            $archive = Save-BuildDownload `
                'https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz' `
                'SDL2-devel-2.32.10-mingw.tar.gz' '83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2'
            Expand-BuildArchive -Archive $archive -Destination (Join-Path $root 'deps')
        }
    }
    & $cmakeCommand -S $source -B (Join-Path $source 'build') -G Ninja `
        "-DCMAKE_C_COMPILER=$((Get-Command gcc).Source)" `
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DUNICORN_ARCH=arm -DUNICORN_BUILD_TESTS=OFF
    & $cmakeCommand --build (Join-Path $source 'build') --target unicorn --parallel $Jobs
}

function Copy-PackageFile {
    param([string]$Source, [string]$Target)

    if (!(Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Missing package input: $Source" }
    New-Item -ItemType Directory -Path (Split-Path $Target -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Target -Force
}

function Copy-WindowsLibraries {
    param([string]$BinaryDirectory)

    $compiler = Split-Path (Get-Command gcc -ErrorAction Stop).Source -Parent
    $sdk = [IO.Path]::GetFullPath($SDL2Root, $root)
    $unicorn = [IO.Path]::GetFullPath($UnicornRoot, $root)
    $directories = @((Join-Path $sdk 'bin'), (Join-Path $unicorn 'lib'), (Join-Path $unicorn 'build'), $compiler)
    $system = @('ADVAPI32.dll', 'COMDLG32.dll', 'D3D9.dll', 'D3D11.dll', 'D3D12.dll',
        'DINPUT8.dll', 'DNSAPI.dll', 'DWMAPI.dll', 'DWRITE.dll', 'DXGI.dll', 'GDI32.dll',
        'IMM32.dll', 'IPHLPAPI.dll', 'KERNEL32.dll', 'msvcrt.dll', 'ntdll.dll', 'ole32.dll',
        'OLEAUT32.dll', 'OPENGL32.dll', 'RPCRT4.dll', 'SETUPAPI.dll', 'SHELL32.dll', 'SHLWAPI.dll',
        'ucrtbase.dll', 'USER32.dll', 'USERENV.dll', 'USP10.dll', 'VERSION.dll', 'WINHTTP.dll',
        'WINMM.dll', 'WS2_32.dll')
    $queue = [Collections.Generic.Queue[string]]::new()
    $queue.Enqueue((Join-Path $BinaryDirectory 'main.exe'))
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    while ($queue.Count) {
        $file = $queue.Dequeue()
        $imports = & objdump -p $file
        foreach ($line in $imports) {
            if ($line -notmatch 'DLL Name:\s*(\S+)') { continue }
            $name = $Matches[1]
            if (!$seen.Add($name)) { continue }
            if ($name -in $system -or $name -match '^(api|ext)-ms-win-') { continue }
            $inputFile = $null
            foreach ($directory in $directories) {
                $candidate = Join-Path $directory $name
                if (Test-Path -LiteralPath $candidate -PathType Leaf) { $inputFile = $candidate; break }
            }
            if (!$inputFile) { throw "Cannot package runtime DLL $name from SDL2/Unicorn/compiler SDKs." }
            $target = Join-Path $BinaryDirectory $name
            Copy-PackageFile $inputFile $target
            $queue.Enqueue($target)
        }
    }
    Copy-PackageFile (Join-Path $sdk 'include/SDL2/SDL.h') (Join-Path (Split-Path $BinaryDirectory -Parent) 'licenses/SDL2-SDL.h')
    $licenses = Join-Path (Split-Path $compiler -Parent) 'share/licenses'
    if (Test-Path -LiteralPath $licenses -PathType Container) {
        Copy-Item -LiteralPath $licenses -Destination (Join-Path (Split-Path $BinaryDirectory -Parent) 'licenses/toolchain') -Recurse
    }
}

function Build-ReleasePackage {
    if (($Platform -eq 'windows' -and !$IsWindows) -or ($Platform -eq 'linux' -and !$IsLinux)) {
        throw 'Native packages must be produced on their target OS.'
    }
    $python = $IsWindows ? 'python' : 'python3'
    Get-Command $python -ErrorAction Stop | Out-Null
    $output = Join-Path $root 'build/releases'
    $temporary = Join-Path $output ('stage-' + [Guid]::NewGuid().ToString('N'))
    $name = "mrpx-$Version-$Platform" + ($Platform -ne 'wasm' ? '-amd64' : '')
    $package = Join-Path $temporary $name
    New-Item -ItemType Directory -Path $package -Force | Out-Null
    $executables = @()
    try {
        Copy-PackageFile (Join-Path $root 'LICENSE') (Join-Path $package 'licenses/MRPX-LICENSE')
        if ($Platform -eq 'wasm') {
            Get-Command go -ErrorAction Stop | Out-Null
            foreach ($file in @('COPYING', 'COPYING.LGPL2', 'COPYING_GLIB', 'qemu/COPYING', 'qemu/COPYING.LIB', 'qemu/LICENSE')) {
                Copy-PackageFile (Join-Path $root "build/unicorn-wasm/source/$file") (Join-Path $package "licenses/unicorn-wasm/$file")
            }
            $site = Join-Path $package 'web'
            $catalogData = Get-Content -LiteralPath (Join-Path $webOutput 'catalog/catalog.json') -Raw | ConvertFrom-Json
            if ($catalogData.schema_version -ne 1 -or $catalogData.summary.pending -ne 0 -or
                $catalogData.summary.total -ne @($catalogData.packages).Count) {
                throw 'Catalog is incomplete or invalid; finish make catalog and run make prepare-web before packaging.'
            }
            foreach ($entry in $catalogData.packages) {
                if ($entry.preview) {
                    if ($entry.preview -notmatch '^previews/[a-f0-9]{64}\.png$') { throw 'Invalid catalog preview path.' }
                    if (-not (Test-Path -LiteralPath (Join-Path $webOutput "catalog/$($entry.preview)") -PathType Leaf)) {
                        throw "Missing catalog screenshot: $($entry.preview)"
                    }
                }
            }
            foreach ($file in @('main.html', 'styles.css', 'controls.js', 'midi.js', 'filesystem.js', 'catalog.js', 'mrpx.js', 'mrpx.wasm', 'catalog/catalog.json')) {
                Copy-PackageFile (Join-Path $webOutput $file) (Join-Path $site $file)
            }
            $catalog = Join-Path $webOutput 'catalog'
            foreach ($file in Get-ChildItem -LiteralPath $catalog -Recurse -File) {
                if ($file.Extension -notin @('.json', '.png')) { throw "Unexpected catalog asset: $($file.FullName)" }
                Copy-PackageFile $file.FullName (Join-Path $site ('catalog/' + [IO.Path]::GetRelativePath($catalog, $file.FullName)))
            }
            foreach ($file in $audioFiles + @('index.js.LEGAL.txt')) {
                Copy-PackageFile (Join-Path $audioOutput $file) (Join-Path $site "vendor/spessasynth/$file")
            }
            Copy-RuntimeAssets (Join-Path $site 'runtime')
            $server = Join-Path $temporary 'server'
            New-Item -ItemType Directory -Path $server -Force | Out-Null
            foreach ($file in @('go.mod', 'main.go')) {
                Copy-PackageFile (Join-Path $root "webserver/$file") (Join-Path $server $file)
            }
            Copy-Item -LiteralPath $site -Destination (Join-Path $server 'site') -Recurse
            $originalGoos = $env:GOOS
            $originalGoarch = $env:GOARCH
            $originalCgo = $env:CGO_ENABLED
            Push-Location $server
            try {
                $env:GOARCH = 'amd64'
                $env:CGO_ENABLED = '0'
                foreach ($os in @('windows', 'linux')) {
                    $env:GOOS = $os
                    $file = "mrpx-web-$os-amd64" + ($os -eq 'windows' ? '.exe' : '')
                    & go build -trimpath -buildvcs=false "-ldflags=-s -w -X main.version=$Version" -o (Join-Path $package $file) .
                    if ($os -eq 'linux') { $executables += $file }
                }
            } finally {
                $env:GOOS = $originalGoos
                $env:GOARCH = $originalGoarch
                $env:CGO_ENABLED = $originalCgo
                Pop-Location
            }
            $instructions = "mrpx $Version`nRun mrpx-web-windows-amd64.exe or ./mrpx-web-linux-amd64, then open http://127.0.0.1:8080/ .`nThe servers embed the complete site; the web/ directory is optional and may also be deployed independently.`nUse -listen 127.0.0.1:PORT to change the port. Public deployments need HTTPS for AudioWorklet.`n"
        } else {
            $binary = $Platform -eq 'windows' ? 'main.exe' : 'main'
            Copy-PackageFile (Join-Path $root "bin/$binary") (Join-Path $package "bin/$binary")
            Copy-RuntimeAssets (Join-Path $package 'bin')
            $source = [IO.Path]::GetFullPath($UnicornRoot, $root)
            foreach ($file in @('COPYING', 'COPYING.LGPL2', 'COPYING_GLIB', 'qemu/COPYING', 'qemu/COPYING.LIB', 'qemu/LICENSE')) {
                Copy-PackageFile (Join-Path $source $file) (Join-Path $package "licenses/unicorn/$file")
            }
            if ($Platform -eq 'windows') {
                Copy-WindowsLibraries (Join-Path $package 'bin')
                $instructions = "mrpx $Version (Windows x64)`nRun bin/main.exe. Startup opens dsm_gm.mrp (the application list).`nPlace applications in bin/mythroad, then use the application list, Open MRP (Ctrl+O), or drag a file from that folder onto the emulator.`nKeep cfunction.ext and mythroad/ beside main.exe. Runtime DLLs come from the build SDKs; no developer toolchain is required.`n"
            } else {
                $executables += @('bin/main', 'run.sh')
                Set-Content -LiteralPath (Join-Path $package 'run.sh') -NoNewline -Value "#!/bin/sh`nset -eu`ncd -- `"`$(dirname -- `"`$0`")`"`nexec ./bin/main `"`$@`"`n"
                $instructions = "mrpx $Version (Ubuntu 24.04 x86_64)`nInstall runtime libraries: sudo apt-get install libsdl2-2.0-0 libgtk-4-1`nRun ./run.sh inside an X11/Wayland desktop. GTK4/SDL2 come from the system; no Unicorn SDK or compiler is required.`nStartup opens dsm_gm.mrp (the application list). Place applications in bin/mythroad, then use the application list, Open MRP (Ctrl+O), or drag a file from that folder onto the emulator.`nKeep cfunction.ext and mythroad/ beside bin/main. F1 opens the keypad/Console.`n"
            }
        }
        Set-Content -LiteralPath (Join-Path $package 'README.txt') -Value $instructions -NoNewline
        $projectSource = $env:GITHUB_REPOSITORY ? "https://github.com/$env:GITHUB_REPOSITORY/tree/$Version" : 'Local checkout; no published mrpx source URL supplied.'
        Set-Content -LiteralPath (Join-Path $package 'SOURCES.txt') -Value @"
mrpx $Version source: $projectSource
Original VMRP: https://github.com/vmrp/vmrp
Unicorn native: https://github.com/unicorn-engine/unicorn/tree/2.1.4
Unicorn WASM: https://github.com/elliott-wen/unicorn.wasm/tree/4ce0fc93249d0f581ed22049a36baa547ef70447
SDL2: https://github.com/libsdl-org/SDL/tree/release-2.32.10
Web audio versions and notices: web/vendor/spessasynth/
Build and packaging instructions: docs/RELEASING.md in the mrpx source checkout
"@
        $archive = Join-Path $output "$name.zip"
        $arguments = @((Join-Path $PSScriptRoot 'package_zip.py'), $package, $archive)
        foreach ($file in $executables) { $arguments += @('--executable', $file) }
        & $python @arguments
        Write-Host "Packaged $archive"
    } finally {
        Remove-Item -LiteralPath $temporary -Recurse -Force
    }
}

function Build-Unicorn {
    Get-Command emcc, emcmake, ninja, node, git -ErrorAction Stop | Out-Null
    $cmakeCommand = Get-CMakePath
    $env:PATH = (Split-Path $cmakeCommand -Parent) + [IO.Path]::PathSeparator + $env:PATH
    $revision = '4ce0fc93249d0f581ed22049a36baa547ef70447'
    $source = [IO.Path]::GetFullPath($SourceDirectory, $root)
    if (-not (Test-Path -LiteralPath $source)) {
        git clone --no-checkout https://github.com/elliott-wen/unicorn.wasm.git $source
        git -C $source checkout --detach $revision
    }
    if ((git -C $source rev-parse HEAD).Trim() -ne $revision) {
        throw "Unicorn source must be pinned to $revision"
    }
    $patch = Join-Path $PSScriptRoot 'unicorn-wasm-2.1.4.patch'
    $PSNativeCommandUseErrorActionPreference = $false
    git -C $source apply --reverse --check $patch 2>$null
    $alreadyPatched = $LASTEXITCODE -eq 0
    $PSNativeCommandUseErrorActionPreference = $true
    if (-not $alreadyPatched) {
        git -C $source apply --check $patch
        git -C $source apply $patch
    }

    $engine = [IO.Path]::GetFullPath($EngineDirectory, $root)
    # CMake may shorten compiler paths on Windows; use separate SDK sanity caches.
    $env:EM_CACHE = Join-Path $root 'build/unicorn-wasm/engine-cache'
    emcmake $cmakeCommand -S $source -B $engine -G Ninja `
        -DUNICORN_ARCH=arm -DUNICORN_BUILD_TESTS=OFF `
        -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release
    & $cmakeCommand --build $engine --target unicorn --parallel $Jobs
    $env:EM_CACHE = Join-Path $root 'build/unicorn-wasm/link-cache'
    emcc -O1 -g -Wall -sASSERTIONS=2 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1 `
        "-I$(Join-Path $source 'include')" (Join-Path $root 'tests/unicorn_wasm_smoke.c') `
        '-Wl,--start-group' (Join-Path $engine 'libunicorn.a') (Join-Path $engine 'libarm-softmmu.a') `
        (Join-Path $engine 'libunicorn-common.a') '-Wl,--end-group' -lm `
        -o (Join-Path $root 'build/unicorn-wasm/smoke.html')
    node (Join-Path $root 'tests/unicorn_wasm_tests.js') (Join-Path $root 'build/unicorn-wasm/smoke.js')
}

$originalPath = $env:PATH
$originalCache = $env:EM_CACHE
Push-Location $root
try {
    switch ($Command) {
        'runtime' { Copy-RuntimeAssets $Destination }
        'web' { Prepare-Web }
        'audio' { Prepare-Audio }
        'unicorn' { Build-Unicorn }
        'native-deps' { Prepare-NativeDependencies }
        'package' { Build-ReleasePackage }
        'desktop' { & make main }
        'debug' { & make debug }
        'wasm' { & make wasm }
        'de' { & make de }
        'help' {
            Write-Host @'
mrpx build commands (PowerShell 7.3+)
User entry point: make help. Commands below are implementation/compatibility interfaces.
  pwsh -File scripts/build.ps1 runtime [-Destination <path>]  Copy manifest seeds; preserve app data
  pwsh -File scripts/build.ps1 audio              Bundle local MIDI assets (npm ci first)
  pwsh -File scripts/build.ps1 web                Stage HTML/CSS/JS and runtime (audio first)
  pwsh -File scripts/build.ps1 unicorn [-CMake <path>]  Build pinned WASM backend and run smoke
      [-SourceDirectory <path>] [-EngineDirectory <path>] [-Jobs <count>]
  pwsh -File scripts/build.ps1 wasm               Compile/stage Web (audio and unicorn first)
  pwsh -File scripts/build.ps1 native-deps [-CMake <path>] [-Jobs <count>]
      Prepare pinned native Unicorn 2.1.4 static SDK; Windows SDL2 2.32.10 SDK
  pwsh -File scripts/build.ps1 package -Version vX.Y.Z -Platform windows|linux|wasm
      Package fresh manifest seeds; wasm includes Windows/Linux embedded Go servers
      [-UnicornRoot <path>] [-SDL2Root <path>]  Native dependency SDK paths
  pwsh -File scripts/build.ps1 desktop|debug|de    Build native targets using Make

Make remains the compiler dependency graph and reads config.local.mk.
Paths are relative to the repository, not the current working directory.
Activate Emscripten before unicorn/wasm; only native-deps explicitly downloads native SDKs.
'@
        }
    }
} finally {
    $env:PATH = $originalPath
    $env:EM_CACHE = $originalCache
    Pop-Location
}
