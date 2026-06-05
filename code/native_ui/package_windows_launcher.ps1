param(
    [string]$Version = '1.3.3',
    [string]$ZipPath = '',
    [string]$OutputExe = '',
    [string]$QtBinRoot = $env:ASEAPP_QT_BIN_ROOT,
    [string]$SignCertThumbprint = $env:ASEAPP_CODESIGN_THUMBPRINT,
    [string]$SignCertSubject = $env:ASEAPP_CODESIGN_SUBJECT
)

$ErrorActionPreference = 'Stop'

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($ZipPath)) {
    $ZipPath = Join-Path $PSScriptRoot "dist\ASEappSurfaceBuilder-$Version-Windows.zip"
}
if ([string]::IsNullOrWhiteSpace($OutputExe)) {
    $OutputExe = Join-Path $projectRoot "standalone_exe\windows\ASEappSurfaceBuilder-$Version-Windows.exe"
}

function Get-X64File([string]$root, [string]$name) {
    $file = Get-ChildItem $root -Recurse -File -Filter $name -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match '\\x64\\' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $file) { throw "Could not find x64 $name under $root" }
    return $file.FullName
}

$zipPath = (Resolve-Path $ZipPath).Path
$zipDir = Split-Path -Parent $zipPath
$outputExe = [System.IO.Path]::GetFullPath($OutputExe)
$outputDir = Split-Path -Parent $outputExe
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

function Get-SigningCertificate {
    if (-not [string]::IsNullOrWhiteSpace($SignCertThumbprint)) {
        $thumbprint = $SignCertThumbprint -replace '\s', ''
        $cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
            Where-Object { $_.Thumbprint -eq $thumbprint -and $_.HasPrivateKey } |
            Select-Object -First 1
        if (-not $cert) {
            throw "Code signing certificate was not found in Cert:\CurrentUser\My: $SignCertThumbprint"
        }
        return $cert
    }

    if (-not [string]::IsNullOrWhiteSpace($SignCertSubject)) {
        return Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
            Where-Object { $_.Subject -eq $SignCertSubject -and $_.HasPrivateKey } |
            Sort-Object NotAfter -Descending |
            Select-Object -First 1
    }

    return $null
}

$signingCert = Get-SigningCertificate
if ($signingCert) {
    Write-Host "Code signing enabled: $($signingCert.Subject) [$($signingCert.Thumbprint)]"
}
else {
    Write-Host 'Code signing skipped: no local code signing certificate was found.'
}

function Sign-IfNeeded([string]$path) {
    if (-not $signingCert) {
        return
    }

    $current = Get-AuthenticodeSignature -LiteralPath $path
    if ($current.Status -eq 'Valid') {
        return
    }

    $signature = Set-AuthenticodeSignature -FilePath $path -Certificate $signingCert -HashAlgorithm SHA256
    if ($signature.Status -ne 'Valid') {
        throw "Failed to sign ${path}: $($signature.Status) $($signature.StatusMessage)"
    }
}

function Sign-PayloadPortableExecutableFiles([string]$root) {
    if (-not $signingCert) {
        return
    }

    Get-ChildItem -LiteralPath $root -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in @('.exe', '.dll') } |
        Sort-Object FullName |
        ForEach-Object {
            Sign-IfNeeded $_.FullName
        }
}

function Copy-WindowsTrustSupportFiles([string]$root) {
    $windowsToolsDir = Join-Path $PSScriptRoot 'tools\windows'
    $supportFiles = @(
        'ASEapp-Windows-Trust-LocalCertificate.ps1',
        'README-Windows.txt'
    )

    New-Item -ItemType Directory -Path $root -Force | Out-Null
    foreach ($supportFile in $supportFiles) {
        $source = Join-Path $windowsToolsDir $supportFile
        if (Test-Path -LiteralPath $source) {
            Copy-Item -LiteralPath $source -Destination (Join-Path $root $supportFile) -Force
        }
    }

    $certDestination = Join-Path $root 'ASEappSurfaceBuilderLocalCodeSigning.cer'
    if ($signingCert) {
        Export-Certificate -Cert $signingCert -FilePath $certDestination -Force | Out-Null
    }
    elseif (Test-Path -LiteralPath $certDestination) {
        Remove-Item -LiteralPath $certDestination -Force
    }
}

if (-not ('AseappFnv1a64' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;

public static class AseappFnv1a64
{
    public static string HashFile(string path)
    {
        const ulong offset = 14695981039346656037UL;
        const ulong prime = 1099511628211UL;
        ulong hash = offset;
        byte[] buffer = new byte[1024 * 1024];
        using (FileStream stream = File.OpenRead(path))
        {
            int read;
            while ((read = stream.Read(buffer, 0, buffer.Length)) > 0)
            {
                for (int i = 0; i < read; ++i)
                {
                    hash ^= buffer[i];
                    hash *= prime;
                }
            }
        }
        return hash.ToString("x16");
    }
}
'@
}

function Write-PayloadManifest([string]$root) {
    $manifestPath = Join-Path $root 'ASEAPP_PAYLOAD_MANIFEST.tsv'
    $rootFull = [System.IO.Path]::GetFullPath($root).TrimEnd('\')
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add('# ASEapp payload manifest v1')
    Get-ChildItem -LiteralPath $root -Recurse -File |
        Where-Object { $_.FullName -ne $manifestPath } |
        Sort-Object FullName |
        ForEach-Object {
            $fileFull = [System.IO.Path]::GetFullPath($_.FullName)
            if (-not $fileFull.StartsWith($rootFull + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Manifest input is outside the payload root: $fileFull"
            }
            $relative = $fileFull.Substring($rootFull.Length + 1).Replace('\', '/')
            $lines.Add(("{0}`t{1}`t{2}" -f $relative, $_.Length, [AseappFnv1a64]::HashFile($_.FullName)))
        }
    Set-Content -LiteralPath $manifestPath -Value $lines -Encoding ASCII
}

$msvcRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC'
$rcRoot = 'C:\Program Files (x86)\Windows Kits\10\bin'
Get-X64File $msvcRoot 'cl.exe' | Out-Null
Get-X64File $rcRoot 'rc.exe' | Out-Null

$redistRoot = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC'
$vcDlls = Get-ChildItem $redistRoot -Recurse -File -ErrorAction SilentlyContinue |
    Where-Object { $_.FullName -match '\\x64\\Microsoft\.VC143\.CRT\\.*\.dll$' }
if (-not $vcDlls) {
    throw "VC++ runtime DLLs were not found under $redistRoot"
}

function Get-QtBinRoot {
    if (-not [string]::IsNullOrWhiteSpace($QtBinRoot)) {
        $candidate = [System.IO.Path]::GetFullPath($QtBinRoot)
        if (Test-Path (Join-Path $candidate 'windeployqt.exe')) {
            return $candidate
        }
        if (Test-Path (Join-Path $candidate 'Qt6Core.dll')) {
            return $candidate
        }
        throw "QtBinRoot does not look like a Qt bin directory: $QtBinRoot"
    }

    if ($env:CONDA_PREFIX) {
        $candidate = Join-Path $env:CONDA_PREFIX 'Library\bin'
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $windeployqt = Get-Command windeployqt.exe -ErrorAction SilentlyContinue
    if ($windeployqt) {
        return Split-Path -Parent $windeployqt.Source
    }

    throw 'Could not locate a Qt bin directory. Activate the aseapp conda environment first.'
}

function Get-PythonExe {
    if ($env:CONDA_PREFIX) {
        $candidate = Join-Path $env:CONDA_PREFIX 'python.exe'
        if (Test-Path $candidate) {
            return $candidate
        }
    }

    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($python) {
        return $python.Source
    }

    throw 'Could not locate python.exe for icon generation. Activate the aseapp conda environment first.'
}

$qtBinRoot = Get-QtBinRoot
$qtOptionalRuntimeDllNames = @(
    'double-conversion.dll'
    'freetype.dll'
    'jpeg8.dll'
    'libcrypto-3-x64.dll'
    'libpng16.dll'
    'libssl-3-x64.dll'
    'pcre2-16.dll'
    'zlib.dll'
    'zstd.dll'
    'deflate.dll'
    'Lerc.dll'
    'liblzma.dll'
    'libsharpyuv.dll'
    'libwebp.dll'
    'libwebpdemux.dll'
    'libwebpmux.dll'
    'tiff.dll'
)

function Invoke-WindeployQt([string]$exePath, [string]$payloadRoot) {
    $windeployQt = Join-Path $qtBinRoot 'windeployqt.exe'
    if (-not (Test-Path -LiteralPath $windeployQt)) {
        Write-Host "windeployqt was not found under $qtBinRoot; using files already present in the payload."
        return
    }

    $deployArgs = @(
        '--release',
        '--no-compiler-runtime',
        '--no-translations',
        '--dir',
        '.',
        '--libdir',
        'bin',
        '--plugindir',
        'plugins',
        '--translationdir',
        'translations',
        '--force',
        $exePath
    )
    Push-Location -LiteralPath $payloadRoot
    try {
        & $windeployQt @deployArgs | Out-Host
        if ($LASTEXITCODE -ne 0) {
            throw "windeployqt failed for $exePath"
        }
    }
    finally {
        Pop-Location
    }
}

function Copy-RuntimeDlls([string]$binDir) {
    New-Item -ItemType Directory -Path $binDir -Force | Out-Null
    foreach ($dll in $vcDlls) {
        Copy-Item -LiteralPath $dll.FullName -Destination (Join-Path $binDir $dll.Name) -Force
    }
    foreach ($dllName in $qtOptionalRuntimeDllNames) {
        $source = Join-Path $qtBinRoot $dllName
        if (-not (Test-Path $source)) {
            Write-Host "Optional Qt runtime DLL was not found; skipped: $source"
            continue
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $binDir $dllName) -Force
    }
}

$stagingRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('aseapp_launcher_' + [Guid]::NewGuid().ToString('N'))
$payloadRoot = Join-Path $stagingRoot 'payload'
$launcherRoot = Join-Path $stagingRoot 'launcher'
$launcherBuild = Join-Path $launcherRoot 'build'
New-Item -ItemType Directory -Path $payloadRoot -Force | Out-Null
New-Item -ItemType Directory -Path $launcherRoot -Force | Out-Null

try {
    Expand-Archive -LiteralPath $zipPath -DestinationPath $payloadRoot -Force
    $binDir = Join-Path $payloadRoot 'bin'
    $payloadExe = Join-Path $binDir 'ASEappNativeUI.exe'
    if (-not (Test-Path -LiteralPath $payloadExe)) {
        throw "Payload executable was not found: $payloadExe"
    }
    Invoke-WindeployQt $payloadExe $payloadRoot
    Copy-RuntimeDlls $binDir
    Copy-WindowsTrustSupportFiles $payloadRoot
    Sign-PayloadPortableExecutableFiles $payloadRoot
    Write-PayloadManifest $payloadRoot

    $payloadZip = Join-Path $stagingRoot 'payload.zip'
    & 'C:\Program Files\7-Zip\7z.exe' a -tzip -mx=9 -mmt=on -y $payloadZip (Join-Path $payloadRoot '*') | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw 'Failed to create payload zip.'
    }
    Copy-Item -LiteralPath $payloadZip -Destination (Join-Path $zipDir "ASEappSurfaceBuilder-$Version-Windows.zip") -Force

    $expandedPayloadRoot = Join-Path $zipDir ([System.IO.Path]::GetFileNameWithoutExtension($zipPath))
    $expandedBinDir = Join-Path $expandedPayloadRoot 'bin'
    if (Test-Path $expandedPayloadRoot) {
        try {
            Copy-Item -Path (Join-Path $payloadRoot '*') -Destination $expandedPayloadRoot -Recurse -Force
            Write-Host "Updated expanded payload: $expandedPayloadRoot"
        }
        catch {
            Write-Warning "Could not update existing expanded payload: $($_.Exception.Message)"
        }
    }

    $iconSource = (Resolve-Path (Join-Path $PSScriptRoot 'assets\aseapp_surface_builder_icon.svg')).Path
    $iconGenerator = (Resolve-Path (Join-Path $PSScriptRoot 'tools\generate_icon_assets.py')).Path
    $generatedIconDir = Join-Path $stagingRoot 'generated_icons'
    $pythonExe = Get-PythonExe
    & $pythonExe $iconGenerator --source $iconSource --out-dir $generatedIconDir
    if ($LASTEXITCODE -ne 0) {
        throw 'Failed to generate launcher icon.'
    }
    $iconPath = Join-Path $generatedIconDir 'aseapp_surface_builder_icon.ico'
    if (-not (Test-Path $iconPath)) {
        throw "Generated launcher icon was not found: $iconPath"
    }

    $rcPath = Join-Path $launcherRoot 'launcher.rc'
    $rc = @(
        '1 ICON "' + $iconPath.Replace('\', '\\') + '"'
        '101 RCDATA "' + $payloadZip.Replace('\', '\\') + '"'
    )
    Set-Content -LiteralPath $rcPath -Value $rc -Encoding ASCII

    $cmakeLists = @'
cmake_minimum_required(VERSION 3.21)
project(aseapp_launcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(aseapp_launcher WIN32
    launcher.cpp
    launcher.rc
)

set_target_properties(aseapp_launcher PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded"
)

target_link_libraries(aseapp_launcher PRIVATE Shell32 User32 Gdi32)
'@
    Set-Content -LiteralPath (Join-Path $launcherRoot 'CMakeLists.txt') -Value $cmakeLists -Encoding ASCII

    $cppSource = (Join-Path $PSScriptRoot 'launcher\launcher.cpp')
    Copy-Item -LiteralPath $cppSource -Destination (Join-Path $launcherRoot 'launcher.cpp') -Force

    cmake -S $launcherRoot -B $launcherBuild -G "Visual Studio 17 2022" -A x64
    if ($LASTEXITCODE -ne 0) {
        throw 'Launcher configure failed.'
    }

    cmake --build $launcherBuild --config Release --parallel 2
    if ($LASTEXITCODE -ne 0) {
        throw 'Launcher build failed.'
    }

    $builtLauncher = Join-Path $launcherBuild 'Release\aseapp_launcher.exe'
    if (-not (Test-Path $builtLauncher)) {
        throw "Built launcher was not found: $builtLauncher"
    }

    Copy-Item -LiteralPath $builtLauncher -Destination $outputExe -Force
    Sign-IfNeeded $outputExe
    Copy-WindowsTrustSupportFiles $outputDir
    Write-Host "Created: $outputExe"
}
finally {
    Remove-Item -LiteralPath $stagingRoot -Recurse -Force -ErrorAction SilentlyContinue
}
