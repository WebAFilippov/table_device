# Publish firmware as a GitHub Release (for OTA updates).
# Usage (from project root, GH_TOKEN must be set):
#   .\tools\release.ps1 -Tag v1.0 -Notes "First version"
# The repo name is taken from git remote; firmware.bin is uploaded as asset.
param(
  [Parameter(Mandatory = $true)][string]$Tag,
  [string]$Notes = "",
  [string]$Bin = ".pio\build\firmware\firmware.bin"
)

$ErrorActionPreference = "Stop"

if (-not $env:GH_TOKEN) { throw "GH_TOKEN is not set" }
if (-not (Test-Path $Bin)) { throw "Firmware file not found: $Bin" }

$remote = git remote get-url origin
$repo = $remote -replace '^.*github\.com[:/]', '' -replace '\.git$', ''
Write-Host "Repo: $repo"

$headers = @{
  Authorization = "Bearer $env:GH_TOKEN"
  Accept        = "application/vnd.github+json"
  "X-GitHub-Api-Version" = "2022-11-28"
}

$release = $null
try {
  $release = Invoke-RestMethod -Method Get -Uri "https://api.github.com/repos/$repo/releases/tags/$Tag" -Headers $headers
  Write-Host "Release $Tag already exists - updating"
} catch {
  $body = @{ tag_name = $Tag; name = $Tag; body = $Notes; draft = $false; prerelease = $false } | ConvertTo-Json
  $release = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$repo/releases" -Headers $headers -Body $body
  Write-Host "Release $Tag created"
}

foreach ($a in $release.assets) {
  if ($a.name -eq "firmware.bin") {
    Invoke-RestMethod -Method Delete -Uri $a.url -Headers $headers | Out-Null
    Write-Host "Old asset removed"
  }
}

$upHeaders = @{
  Authorization  = "Bearer $env:GH_TOKEN"
  Accept         = "application/vnd.github+json"
  "Content-Type" = "application/octet-stream"
}
$uploadUrl = "https://uploads.github.com/repos/$repo/releases/$($release.id)/assets?name=firmware.bin"
$resp = Invoke-RestMethod -Method Post -Uri $uploadUrl -Headers $upHeaders -InFile $Bin
Write-Host "OK: asset uploaded - $($resp.browser_download_url)"
