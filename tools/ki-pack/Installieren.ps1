# Optional local language pack. No administrator rights, service, cloud inference or account.
param([string]$Destination = (Join-Path $env:APPDATA 'Werwolf\ki'))
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$pack = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'pack.json') -Raw | ConvertFrom-Json
$Destination = [IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
Write-Host 'Werwolf: lokales KI-Sprachpaket / Local AI dialogue pack'
Write-Host 'Einmaliger Download: ca. 2,6 GB. Danach offline, ohne Konto oder API-Kosten.'
Write-Host ('Ziel / Destination: ' + $Destination)

function Download-Checked([string]$Url, [string]$Target, [string]$Hash) {
    if ((Test-Path -LiteralPath $Target) -and (Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash -eq $Hash) { return }
    $partial = $Target + '.partial'
    # curl streams to disk and can resume an interrupted multi-GB model download.
    & curl.exe --fail --location --retry 3 --connect-timeout 20 --proto '=https' --proto-redir '=https' --continue-at - --output $partial $Url
    if ($LASTEXITCODE -ne 0) { throw 'Download unterbrochen. Installieren.cmd erneut starten, um fortzusetzen.' }
    if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $Hash) {
        # This is exactly the single partial file belonging to this download, never a directory.
        Remove-Item -LiteralPath $partial
        throw 'Pruefsumme stimmt nicht. Bitte erneut installieren.'
    }
    Move-Item -LiteralPath $partial -Destination $Target -Force
}

$archive = Join-Path $Destination 'runtime-b11960.zip'
Download-Checked $pack.runtime.url $archive $pack.runtime.sha256
$runtime = Join-Path $Destination 'runtime'
New-Item -ItemType Directory -Path $runtime -Force | Out-Null
Expand-Archive -LiteralPath $archive -DestinationPath $runtime -Force
# Official archives can contain one enclosing directory; normalize to runtime/llama-server.exe.
if (-not (Test-Path -LiteralPath (Join-Path $runtime 'llama-server.exe'))) {
    $servers = @(Get-ChildItem -LiteralPath $runtime -Recurse -Filter llama-server.exe -File)
    if ($servers.Count -ne 1) { throw 'llama-server.exe fehlt im Laufzeitpaket.' }
    Get-ChildItem -LiteralPath $servers[0].Directory.FullName -File | Copy-Item -Destination $runtime -Force
}
Download-Checked $pack.model.url (Join-Path $Destination 'model.gguf') $pack.model.sha256
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'pack.json') -Destination (Join-Path $Destination 'pack.json') -Force
foreach ($license in @('LLAMA-LICENSE.txt', 'QWEN-LICENSE.txt')) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $license) -Destination (Join-Path $Destination $license) -Force
}
Write-Host ''
Write-Host 'Fertig. Werwolf starten und ein Spiel hosten. Einstellungen > Bots > Lokale Bot-KI: An.'
Write-Host 'Die KI startet und endet mit dem gehosteten Spiel. Mitspieler brauchen das Paket nicht.'
