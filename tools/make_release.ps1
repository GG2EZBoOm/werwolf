# Erstellt ein Release für den Auto-Updater.
#
#   .\tools\make_release.ps1                      # Version aus CMakeLists.txt, Repo GG2EZBoOm/werwolf
#   .\tools\make_release.ps1 -Notes "Neue Rollen" # mit Änderungshinweis
#
# Ergebnis liegt in dist\<version>\: werwolf.exe, werwolf_server.exe und update.json.
# Diese drei Dateien als Assets an ein GitHub-Release mit dem Tag v<version> hängen.
# Wichtig: Das Repository muss öffentlich sein, sonst kann das Spiel die Dateien nicht laden.
param(
    [string]$Repo = "GG2EZBoOm/werwolf",
    [string]$Notes = "",
    [string]$BinaryDir = "", # optional: bereits geprüfter Build, zusammen mit -SkipBuild
    [switch]$SkipBuild
)
$ErrorActionPreference = "Stop"
if ($BinaryDir -and -not $SkipBuild) { throw "-BinaryDir erfordert -SkipBuild." }
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

# Version aus CMakeLists.txt lesen (project(Werwolf VERSION x.y.z ...))
$cmake = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($cmake -notmatch "project\(Werwolf VERSION (\d+\.\d+\.\d+)") { throw "Version in CMakeLists.txt nicht gefunden." }
$version = $Matches[1]
$tag = "v$version"
Write-Host "Release $tag für $Repo"

# Ein öffentliches Release muss die lokal lizenzierten Musikdateien enthalten.
$musicDir = Join-Path $root "game\assets\audio"
foreach ($track in @("day_tension_1", "day_tension_2", "night_foreshadow", "night_eyes_in_the_dark", "night_creeping_death")) {
    if (-not (Test-Path (Join-Path $musicDir "$track.wav"))) {
        throw "Musikdatei $track.wav fehlt. Vor dem Release tools/import_music.py mit den beiden lizenzierten Paketen ausführen."
    }
}

if (-not $SkipBuild) {
    & (Join-Path $root "build.ps1") -Preset release
    if ($LASTEXITCODE -ne 0) { throw "Build fehlgeschlagen." }
}

$bin = if ($BinaryDir) { (Resolve-Path -LiteralPath $BinaryDir).Path } else { Join-Path $root "build\release\bin" }
$dist = Join-Path $root "dist\$version"
New-Item -ItemType Directory -Force -Path $dist | Out-Null

$files = @("werwolf.exe", "werwolf_server.exe")
$entries = @()
foreach ($name in $files) {
    $src = Join-Path $bin $name
    if (-not (Test-Path $src)) { throw "$name fehlt in $bin" }
    Copy-Item $src (Join-Path $dist $name) -Force
    $item = Get-Item (Join-Path $dist $name)
    $hash = (Get-FileHash $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $entries += [ordered]@{
        path   = $name
        url    = "https://github.com/$Repo/releases/download/$tag/$name"
        sha256 = $hash
        size   = $item.Length
    }
}

$manifest = [ordered]@{
    version = $version
    notes   = $Notes
    files   = $entries
}
# UTF-8 ohne BOM schreiben (BOM würde den JSON-Leser stören)
$json = $manifest | ConvertTo-Json -Depth 5
[System.IO.File]::WriteAllText((Join-Path $dist "update.json"), $json, (New-Object System.Text.UTF8Encoding($false)))

Write-Host ""
Write-Host "Fertig: $dist"
Get-ChildItem $dist | Format-Table Name, Length
Write-Host "Nächster Schritt: Auf GitHub ein Release mit dem Tag $tag anlegen und diese Dateien als Assets hochladen:"
Write-Host "  https://github.com/$Repo/releases/new?tag=$tag"
if (Get-Command gh -ErrorAction SilentlyContinue) {
    Write-Host "Oder mit der GitHub-CLI:"
    Write-Host "  gh release create $tag `"$dist\werwolf.exe`" `"$dist\werwolf_server.exe`" `"$dist\update.json`" --repo $Repo --title `"Werwolf $version`""
}
