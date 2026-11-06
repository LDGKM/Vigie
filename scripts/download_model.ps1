# Downloads the local models into models/ and checks their integrity.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\download_model.ps1
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$ProgressPreference = 'SilentlyContinue'

$models = @(
    @{
        Name = 'bge-m3-Q4_K_M.gguf'
        Url  = 'https://huggingface.co/lm-kit/bge-m3-gguf/resolve/9379ce497e8814b200f2dc0d18eb4045426dcb8c/bge-m3-Q4_K_M.gguf'
        Hash = 'E251234FCB7D050991A6BE491952F485BF5C641DD10C3272DC1301FD281AD50F'
    },
    @{
        Name = 'bge-reranker-v2-m3-Q4_K_M.gguf'
        Url  = 'https://huggingface.co/gpustack/bge-reranker-v2-m3-GGUF/resolve/3093af03b1a635e67b084b1d8c03c5f5e020fd05/bge-reranker-v2-m3-Q4_K_M.gguf'
        Hash = 'E186A244ED455B4AB66EC64339CE7427A6AE13F5C0B5E544DE96E50F0F8B3673'
    },
    @{
        Name = 'Qwen3.5-4B-Q4_K_M.gguf'
        Url  = 'https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/resolve/e87f176479d0855a907a41277aca2f8ee7a09523/Qwen3.5-4B-Q4_K_M.gguf'
        Hash = '00FE7986FF5F6B463E62455821146049DB6F9313603938A70800D1FB69EF11A4'
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
