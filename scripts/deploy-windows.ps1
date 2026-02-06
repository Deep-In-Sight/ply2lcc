# deploy-windows.ps1
# Creates a self-contained distribution of ply2lcc (CLI + GUI) for Windows
# Usage: .\scripts\deploy-windows.ps1 [-BuildDir <path>] [-OutputDir <path>] [-Zip]

param(
    [string]$BuildDir = "build",
    [string]$OutputDir = "dist",
    [switch]$Zip
)

$ErrorActionPreference = "Stop"

# Resolve paths
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$BuildPath = Join-Path $ProjectRoot $BuildDir
$OutputPath = Join-Path $ProjectRoot $OutputDir

Write-Host "=== ply2lcc Windows Deployment Script ===" -ForegroundColor Cyan
Write-Host "Build directory: $BuildPath"
Write-Host "Output directory: $OutputPath"

# Check build outputs exist
$CliExe = Join-Path $BuildPath "Release\ply2lcc.exe"
$GuiExe = Join-Path $BuildPath "gui\Release\ply2lcc-gui.exe"

if (-not (Test-Path $CliExe)) {
    Write-Error "CLI executable not found: $CliExe`nRun: cmake --build $BuildPath --config Release"
    exit 1
}

$HasGui = Test-Path $GuiExe
if (-not $HasGui) {
    Write-Warning "GUI executable not found. Building CLI-only distribution."
}

# Clean and create output directory
if (Test-Path $OutputPath) {
    Write-Host "Cleaning existing output directory..."
    Remove-Item -Recurse -Force $OutputPath
}
New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null

# Copy CLI
Write-Host "Copying CLI executable..." -ForegroundColor Green
Copy-Item $CliExe $OutputPath

# Copy GUI and deploy Qt dependencies
if ($HasGui) {
    Write-Host "Copying GUI executable..." -ForegroundColor Green
    Copy-Item $GuiExe $OutputPath

    # Find windeployqt
    $WinDeployQt = $null
    $VcpkgQt = "C:\vcpkg\installed\x64-windows\tools\Qt6\bin\windeployqt6.exe"
    if (Test-Path $VcpkgQt) {
        $WinDeployQt = $VcpkgQt
    } else {
        # Try to find in PATH or Qt installation
        $WinDeployQt = Get-Command windeployqt6.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
        if (-not $WinDeployQt) {
            $WinDeployQt = Get-Command windeployqt.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source
        }
    }

    if ($WinDeployQt) {
        Write-Host "Deploying Qt dependencies using: $WinDeployQt" -ForegroundColor Green
        $ErrorActionPreference = "SilentlyContinue"
        & $WinDeployQt (Join-Path $OutputPath "ply2lcc-gui.exe") --no-translations --no-opengl-sw *>&1 | Out-Null
        $ErrorActionPreference = "Stop"
    } else {
        Write-Warning "windeployqt not found. Qt DLLs must be copied manually."
    }
}

# Copy vcpkg Qt dependencies that windeployqt misses
Write-Host "Copying vcpkg dependencies..." -ForegroundColor Green
$VcpkgBin = "C:\vcpkg\installed\x64-windows\bin"
if (Test-Path $VcpkgBin) {
    $VcpkgDeps = @(
        "zlib1.dll",
        "libpng16.dll",
        "freetype.dll",
        "harfbuzz.dll",
        "brotlicommon.dll",
        "brotlidec.dll",
        "bz2.dll",
        "double-conversion.dll",
        "pcre2-16.dll",
        "zstd.dll",
        "md4c.dll",
        "icudt78.dll",
        "icuin78.dll",
        "icuuc78.dll"
    )
    foreach ($dll in $VcpkgDeps) {
        $src = Join-Path $VcpkgBin $dll
        if (Test-Path $src) {
            Copy-Item $src $OutputPath -Force
        }
    }
}

# Find and copy VC++ Runtime DLLs
Write-Host "Copying VC++ Runtime DLLs..." -ForegroundColor Green
$VCRedistPaths = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT\*.dll",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT\*.dll",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT\*.dll",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT\*.dll"
)

$CopiedVCRuntime = $false
foreach ($pattern in $VCRedistPaths) {
    $dlls = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue
    if ($dlls) {
        $dlls | Copy-Item -Destination $OutputPath -Force
        $CopiedVCRuntime = $true
        break
    }
}

if (-not $CopiedVCRuntime) {
    Write-Warning "VC++ Runtime DLLs not found. The app may not run on machines without Visual Studio."
}

# Copy OpenMP Runtime DLL
Write-Host "Copying OpenMP Runtime DLL..." -ForegroundColor Green
$OpenMPPaths = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC\*\x64\Microsoft.VC*.OpenMP\*.dll",
    "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Redist\MSVC\*\x64\Microsoft.VC*.OpenMP\*.dll",
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Redist\MSVC\*\x64\Microsoft.VC*.OpenMP\*.dll",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\*\VC\Redist\MSVC\*\x64\Microsoft.VC*.OpenMP\*.dll"
)

$CopiedOpenMP = $false
foreach ($pattern in $OpenMPPaths) {
    $dlls = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue
    if ($dlls) {
        $dlls | Copy-Item -Destination $OutputPath -Force
        $CopiedOpenMP = $true
        break
    }
}

if (-not $CopiedOpenMP) {
    Write-Warning "OpenMP Runtime DLL (vcomp140.dll) not found. Parallel processing may not work."
}

# List deployed files
Write-Host "`nDeployed files:" -ForegroundColor Cyan
Get-ChildItem $OutputPath -Recurse | Where-Object { -not $_.PSIsContainer } | ForEach-Object {
    $relativePath = $_.FullName.Substring($OutputPath.Length + 1)
    $sizeKB = [math]::Round($_.Length / 1KB, 1)
    Write-Host "  $relativePath ($sizeKB KB)"
}

# Calculate total size
$totalSize = (Get-ChildItem $OutputPath -Recurse | Measure-Object -Property Length -Sum).Sum
$totalSizeMB = [math]::Round($totalSize / 1MB, 1)
Write-Host "`nTotal size: $totalSizeMB MB" -ForegroundColor Cyan

# Create ZIP if requested
if ($Zip) {
    $ZipName = "ply2lcc-win64.zip"
    $ZipPath = Join-Path $ProjectRoot $ZipName
    Write-Host "`nCreating ZIP archive: $ZipPath" -ForegroundColor Green

    if (Test-Path $ZipPath) {
        Remove-Item $ZipPath -Force
    }

    Compress-Archive -Path "$OutputPath\*" -DestinationPath $ZipPath -Force

    $zipSizeMB = [math]::Round((Get-Item $ZipPath).Length / 1MB, 1)
    Write-Host "Created: $ZipName ($zipSizeMB MB)" -ForegroundColor Cyan
}

Write-Host "`n=== Deployment Complete ===" -ForegroundColor Green
Write-Host "Output: $OutputPath"
