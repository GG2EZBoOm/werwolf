# Baut das Projekt mit der MSVC-Toolchain aus Visual Studio.
# Beispiele:
#   .\build.ps1                  # Debug-Build
#   .\build.ps1 -Preset release  # optimierter Build
#   .\build.ps1 -Test            # bauen und Tests ausführen
#   .\build.ps1 -Run             # bauen und Spiel starten
param(
    [ValidateSet("debug", "release")] [string]$Preset = "debug",
    [switch]$Test,
    [switch]$Run,
    [string]$Target = ""
)
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

if (-not $env:VCINSTALLDIR) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { throw "Visual Studio wurde nicht gefunden (vswhere.exe fehlt)." }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw "Visual Studio mit C++-Workload wurde nicht gefunden." }
    $vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
    $env:PATH = (Split-Path $vswhere) + ";" + $env:PATH
    cmd /c "`"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match "^(.*?)=(.*)$") { Set-Item -Path "env:$($matches[1])" -Value $matches[2] }
    }
}

cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if ($Target) { cmake --build --preset $Preset --target $Target } else { cmake --build --preset $Preset }
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Test) {
    ctest --preset $Preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
if ($Run) {
    Start-Process (Join-Path $PSScriptRoot "build\$Preset\bin\werwolf.exe")
}
