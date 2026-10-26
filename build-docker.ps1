param([string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist'))
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath $PSScriptRoot).Path
$null = New-Item -ItemType Directory -Path $OutputDirectory -Force
$outputPath = (Resolve-Path -LiteralPath $OutputDirectory).Path
& docker build --platform linux/amd64 -t wwhd-deadzone-builder $repoRoot
if ($LASTEXITCODE -ne 0) { throw 'Docker image build failed.' }
& docker run --rm --platform linux/amd64 --mount "type=bind,source=$repoRoot,target=/project,readonly" --mount "type=bind,source=$outputPath,target=/output" wwhd-deadzone-builder
if ($LASTEXITCODE -ne 0) { throw 'Container build failed.' }
