# Lakitu flight-data report

This directory contains the reproducible post-flight analysis for the public
2026-07-28 flight window.

Build from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File flight-data/report/build_report.ps1
```

The build regenerates `analysis_summary.json`, the compact plot-driving CSVs in
`processed/`, eight standalone vector graphs in `figures/`, and the detailed
LaTeX report. The final PDF is copied to
`output/pdf/Lakitu_Flight_Data_Report.pdf`.

The source telemetry is read-only input. Plot tables use five- or ten-second
aggregation only for visual clarity; full-rate rows are used for reported
extrema and event statistics.
