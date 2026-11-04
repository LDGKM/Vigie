# Downloads the documents listed in data/corpus/sources.json into data/corpus/raw.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\download_corpus.ps1
[CmdletBinding()]
param(
    [string]$ManifestPath,
    [string]$OutputDir
)

$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# Windows PowerShell 5.1 leaves $PSScriptRoot empty in param(), so defaults are set here.
$projectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
if (-not $ManifestPath) { $ManifestPath = Join-Path $projectRoot 'data/corpus/sources.json' }
if (-not $OutputDir) { $OutputDir = Join-Path $projectRoot 'data/corpus/raw' }

function Test-Download {
    param([string]$Path, [string]$Format, [string]$Expect)

    if ((Get-Item -LiteralPath $Path).Length -lt 500) {
        return 'file too small'
    }

    if ($Format -eq 'pdf') {
        $bytes = [IO.File]::ReadAllBytes($Path)
        $header = [Text.Encoding]::ASCII.GetString($bytes, 0, 4)
        if ($header -ne '%PDF') { return 'not a PDF file' }
        return $null
    }

    if ($Expect) {
        $text = [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8)
        if (-not $text.Contains($Expect)) { return "expected text not found: $Expect" }
    }
    return $null
}

$manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

$results = @()
foreach ($document in $manifest.documents) {
    $target = Join-Path $OutputDir "$($document.id).$($document.format)"
    $temporary = "$target.tmp"
    $problem = $null

    # A valid file is kept, including one saved by hand from the browser.
    if ((Test-Path -LiteralPath $target) -and -not (Test-Download -Path $target -Format $document.format -Expect $document.expect)) {
        Write-Host "[DEJA]  $($document.id)"
        $results += [PSCustomObject]@{
            id        = $document.id
            status    = 'ok'
            retrieved = (Get-Item -LiteralPath $target).LastWriteTime.ToString('yyyy-MM-dd')
            sha256    = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash
            detail    = 'existing file'
        }
        continue
    }

    try {
        Invoke-WebRequest -Uri $document.url -OutFile $temporary -UseBasicParsing `
            -UserAgent 'Vigie-corpus/0.1' -TimeoutSec 120
        $problem = Test-Download -Path $temporary -Format $document.format -Expect $document.expect
    } catch {
        $problem = $_.Exception.Message
    }

    if ($problem) {
        Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
        Write-Host "[ECHEC] $($document.id): $problem"
        Write-Host "        Ouvre $($document.url)"
        Write-Host "        puis enregistre le document sous $target"
    } else {
        Move-Item -LiteralPath $temporary -Destination $target -Force
        Write-Host "[OK]    $($document.id)"
    }

    $results += [PSCustomObject]@{
        id        = $document.id
        status    = $(if ($problem) { 'failed' } else { 'ok' })
        retrieved = (Get-Date).ToString('yyyy-MM-dd')
        sha256    = $(if ($problem) { '' } else { (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash })
        detail    = $problem
    }
}

$results | Export-Csv -LiteralPath (Join-Path $OutputDir 'download_log.csv') -NoTypeInformation -Encoding UTF8

$failed = @($results | Where-Object { $_.status -eq 'failed' }).Count
Write-Host "$($results.Count - $failed) document(s) telecharge(s), $failed echec(s)."
if ($failed -gt 0) { exit 1 }
