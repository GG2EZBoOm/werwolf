param([string]$Output = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Output) { $Output = Join-Path $root 'dist\Werwolf-KI-Paket.zip' }
$source = Join-Path $PSScriptRoot 'ki-pack'
foreach ($name in @('Installieren.cmd','Installieren.ps1','LIESMICH.txt','pack.json','LLAMA-LICENSE.txt','QWEN-LICENSE.txt')) {
    if (-not (Test-Path -LiteralPath (Join-Path $source $name))) { throw "Paketdatei fehlt: $name" }
}
New-Item -ItemType Directory -Force -Path (Split-Path $Output -Parent) | Out-Null
Compress-Archive -Path (Join-Path $source '*') -DestinationPath $Output -Force
Get-Item -LiteralPath $Output | Select-Object FullName,Length
Get-FileHash -LiteralPath $Output -Algorithm SHA256
