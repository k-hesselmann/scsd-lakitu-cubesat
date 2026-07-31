$ErrorActionPreference = "Stop"

$reportRoot = $PSScriptRoot
$repoRoot = Resolve-Path (Join-Path $reportRoot "..\..")
$figureRoot = Join-Path $reportRoot "figures"
$outputRoot = Join-Path $repoRoot "output\pdf"
$figureNames = @(
    "flight_profile",
    "vertical_dynamics",
    "environment",
    "power_health",
    "trajectory",
    "payload",
    "ttc_link",
    "data_quality"
)

Push-Location $repoRoot
try {
    node "flight-data\report\generate_analysis.mjs"
    if ($LASTEXITCODE -ne 0) { throw "Analysis generation failed." }
}
finally {
    Pop-Location
}

Push-Location $figureRoot
try {
    foreach ($figureName in $figureNames) {
        latexmk -pdf -interaction=nonstopmode -halt-on-error -silent "$figureName.tex"
        if ($LASTEXITCODE -ne 0) { throw "Figure compilation failed: $figureName" }
    }
}
finally {
    Pop-Location
}

Push-Location $reportRoot
try {
    latexmk -pdf -interaction=nonstopmode -halt-on-error -silent "flight_data_report.tex"
    if ($LASTEXITCODE -ne 0) { throw "Report compilation failed." }
    New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
    Copy-Item -Force -LiteralPath "flight_data_report.pdf" -Destination (Join-Path $outputRoot "Lakitu_Flight_Data_Report.pdf")
}
finally {
    Pop-Location
}

Write-Output "Built output\pdf\Lakitu_Flight_Data_Report.pdf"
