# Downloads the local search models into models/ and checks their integrity.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\download_model.ps1
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$ProgressPreference = 'SilentlyContinue'

$models = @(
    @{
        Name = 'multilingual-e5-small-f16.gguf'
        Url  = 'https://huggingface.co/hdrrayan/multilingual-e5-small-gguf/resolve/7cdbf977cc0d1b5091a9dc41cd62405e6d0ca9f8/multilingual-e5-small-f16.gguf'
        Hash = '6A5B2DFB958F98C851028F8C08424458249359EA921314BE378F8B82088DCAC1'
    },
    @{
        Name = 'bge-reranker-v2-m3-Q4_K_M.gguf'
        Url  = 'https://huggingface.co/gpustack/bge-reranker-v2-m3-GGUF/resolve/3093af03b1a635e67b084b1d8c03c5f5e020fd05/bge-reranker-v2-m3-Q4_K_M.gguf'
        Hash = 'E186A244ED455B4AB66EC64339CE7427A6AE13F5C0B5E544DE96E50F0F8B3673'
    }
)

$projectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$directory = Join-Path $projectRoot 'models'
New-Item -ItemType Directory -Force -Path $directory | Out-Null

function Test-Model {
    param([string]$Path, [string]$Expected)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Expected
}

foreach ($model in $models) {
    $target = Join-Path $directory $model.Name
    $temporary = "$target.tmp"

    if ((Test-Path -LiteralPath $target) -and (Test-Model -Path $target -Expected $model.Hash)) {
        Write-Host "[DEJA]  $($model.Name)"
        continue
    }

    Write-Host "Telechargement de $($model.Name)..."
    Invoke-WebRequest -Uri $model.Url -OutFile $temporary -UseBasicParsing -TimeoutSec 3600

    if (-not (Test-Model -Path $temporary -Expected $model.Hash)) {
        Remove-Item -LiteralPath $temporary -Force
        throw "Empreinte incorrecte pour $($model.Name) : le fichier a ete supprime."
    }

    Move-Item -LiteralPath $temporary -Destination $target -Force
    Write-Host "[OK]    $($model.Name)"
}
