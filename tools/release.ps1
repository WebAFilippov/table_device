# Публикация прошивки как GitHub Release (для OTA-обновления стола).
# Использование (из корня проекта, GH_TOKEN уже в переменных окружения):
#   .\tools\release.ps1 -Tag v1.0 -Notes "Первая версия"
# Скрипт сам возьмёт имя репозитория из git remote и зальёт
# .pio\build\firmware\firmware.bin как ассет firmware.bin.
param(
  [Parameter(Mandatory = $true)][string]$Tag,
  [string]$Notes = "",
  [string]$Bin = ".pio\build\firmware\firmware.bin"
)

$ErrorActionPreference = "Stop"

if (-not $env:GH_TOKEN) { throw "GH_TOKEN не задан" }
if (-not (Test-Path $Bin)) { throw "Файл прошивки не найден: $Bin" }

$remote = git remote get-url origin
$repo = $remote -replace '^.*github\.com[:/]', '' -replace '\.git$', ''
Write-Host "Репозиторий: $repo"

$headers = @{
  Authorization = "Bearer $env:GH_TOKEN"
  Accept        = "application/vnd.github+json"
  "X-GitHub-Api-Version" = "2022-11-28"
}

# Создать (или найти) релиз с тегом
$release = $null
try {
  $release = Invoke-RestMethod -Method Get -Uri "https://api.github.com/repos/$repo/releases/tags/$Tag" -Headers $headers
  Write-Host "Релиз $Tag уже существует — обновляю"
} catch {
  $body = @{ tag_name = $Tag; name = $Tag; body = $Notes; draft = $false; prerelease = $false } | ConvertTo-Json
  $release = Invoke-RestMethod -Method Post -Uri "https://api.github.com/repos/$repo/releases" -Headers $headers -Body $body
  Write-Host "Релиз $Tag создан"
}

# Удалить старый ассет firmware.bin, если есть
foreach ($a in $release.assets) {
  if ($a.name -eq "firmware.bin") {
    Invoke-RestMethod -Method Delete -Uri $a.url -Headers $headers | Out-Null
    Write-Host "Старый ассет удалён"
  }
}

# Загрузить прошивку
$upHeaders = @{
  Authorization  = "Bearer $env:GH_TOKEN"
  Accept         = "application/vnd.github+json"
  "Content-Type" = "application/octet-stream"
}
$uploadUrl = "https://uploads.github.com/repos/$repo/releases/$($release.id)/assets?name=firmware.bin"
$resp = Invoke-RestMethod -Method Post -Uri $uploadUrl -Headers $upHeaders -InFile $Bin
Write-Host "OK: ассет загружен — $($resp.browser_download_url)"
