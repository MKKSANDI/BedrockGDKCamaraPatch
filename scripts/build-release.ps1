[CmdletBinding()]
param(
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repoRoot 'camera_patch'
$build = Join-Path $source 'out'
$dist = Join-Path $repoRoot 'dist'
$stage = Join-Path $dist 'MCFIX'
$archive = Join-Path $dist 'MCFIX-win-x64.zip'
$checksum = "$archive.sha256"
$distFull = [IO.Path]::GetFullPath($dist).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
$stageFull = [IO.Path]::GetFullPath($stage)
if (-not $stageFull.StartsWith($distFull, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Release stage escaped the repository dist directory"
}

cmake -S $source -B $build -A x64
if ($LASTEXITCODE) { throw "CMake configure failed: $LASTEXITCODE" }
cmake --build $build --config $Configuration -- /m:1
if ($LASTEXITCODE) { throw "Build failed: $LASTEXITCODE" }
ctest --test-dir $build -C $Configuration --output-on-failure
if ($LASTEXITCODE) { throw "Tests failed: $LASTEXITCODE" }

if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Path $stage | Out-Null

$releaseRoot = Join-Path $build $Configuration
foreach ($name in @(
    'Patcher.exe',
    'MCFIXCameraPatch.dll',
    'MCFIXWatchdog.exe',
    'vcruntime140_1.dll'
)) {
    $sourceFile = Join-Path $releaseRoot $name
    if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
        throw "Required release file is missing: $sourceFile"
    }
    Copy-Item -LiteralPath $sourceFile -Destination (Join-Path $stage $name)
}

Copy-Item -LiteralPath (Join-Path $repoRoot 'README.md') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $stage
Copy-Item -LiteralPath (Join-Path $repoRoot 'THIRD_PARTY_NOTICES.md') -Destination $stage

if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
Compress-Archive -LiteralPath $stage -DestinationPath $archive -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
Set-Content -LiteralPath $checksum -Value "$hash  MCFIX-win-x64.zip" -Encoding ascii

[pscustomobject]@{
    Archive = $archive
    Sha256 = $hash
    ChecksumFile = $checksum
}
