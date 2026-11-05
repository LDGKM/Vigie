# Downloads the local embedding model into models/ and checks its integrity.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\download_model.ps1
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$url = 'https://huggingface.co/hdrrayan/multilingual-e5-small-gguf/resolve/7cdbf977cc0d1b5091a9dc41cd62405e6d0ca9f8/multilingual-e5-small-f16.gguf'
$expectedHash = '6A5B2DFB958F98C851028F8C08424458249359EA921314BE378F8B82088DCAC1'

$projectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$directory = Join-Path $projectRoot 'models'
$target = Join-Path $directory 'multilingual-e5-small-f16.gguf'
$temporary = "$target.tmp"

function Test-Model {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $expectedHash
}

New-Item -ItemType Directory -Force -Path $directory | Out-Null

if ((Test-Path -LiteralPath $target) -and (Test-Model -Path $target)) {
    Write-Host "[DEJA]  $target"
    exit 0
}

Write-Host 'Telechargement du modele (environ 240 Mo)...'
$ProgressPreference = 'SilentlyContinue'
Invoke-WebRequest -Uri $url -OutFile $temporary -UseBasicParsing -TimeoutSec 1800

if (-not (Test-Model -Path $temporary)) {
    Remove-Item -LiteralPath $temporary -Force
    throw 'Empreinte incorrecte : le fichier telecharge a ete supprime.'
}

Move-Item -LiteralPath $temporary -Destination $target -Force
Write-Host "[OK]    $target"
