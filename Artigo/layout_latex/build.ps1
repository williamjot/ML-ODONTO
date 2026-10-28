param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

# Compila o artigo com LuaLaTeX (duas passagens) e copia o PDF para Artigo/.
$ErrorActionPreference = 'Stop'
$layoutDir = $PSScriptRoot
$jobName = 'ARTIGO_LESOES_PERIAPICAIS'

Push-Location $layoutDir
try {
    foreach ($passagem in 1, 2) {
        & lualatex --interaction=nonstopmode --halt-on-error --file-line-error `
            --job-name=$jobName artigo_lesoes_periapicais.tex
        if ($LASTEXITCODE -ne 0) { throw "A passagem $passagem do LuaLaTeX falhou." }
    }
    Copy-Item -LiteralPath "$jobName.pdf" -Destination (Join-Path $RepositoryRoot "Artigo\$jobName.pdf") -Force
}
finally {
    Pop-Location
}

Write-Output (Join-Path $RepositoryRoot "Artigo\$jobName.pdf")
