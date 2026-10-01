# Re-render the documented scenes with fixed seeds (run from the repository root after building).
# Outputs go to PathTracer-CPP/output/final/ (PPM + linear PFM + log); tools/imgtool.py converts to PNG.
param(
    [string]$Exe = "x64\Release\PathTracer-CPP.exe",
    [string[]]$Only = @()
)

$ErrorActionPreference = "Stop"
# "powershell -File" passes "-Only a,b" as a single string.
$Only = @($Only | ForEach-Object { $_ -split "," } | Where-Object { $_ -ne "" })
Push-Location (Join-Path $PSScriptRoot "..\PathTracer-CPP")
try {
    New-Item -ItemType Directory -Force output/final | Out-Null
    # name, extra arguments (scene defaults otherwise)
    $jobs = @(
        @("readme_showcase", @()),
        @("pbr_ibl_test", @()),
        @("pbr_normal_map_test", @()),
        @("pbr_benchmark", @()),
        @("cornell_smoke", @("--spp", "1000")),
        @("cornell_box", @()),
        @("bouncing_spheres", @("--width", "800", "--spp", "256"))
    )
    foreach ($job in $jobs) {
        $name = $job[0]
        if ($Only.Count -gt 0 -and -not ($Only -contains $name)) { continue }
        $out = "output/final/$name.ppm"
        $log = "output/final/$name.log"
        $argList = @("--scene", $name, "--seed", "1", "--no-preview", "--output-mode", "both", "--out", $out) + $job[1]
        $sw = [Diagnostics.Stopwatch]::StartNew()
        # The renderer logs progress to stderr; keep it in the log file.
        Start-Process -FilePath $Exe -ArgumentList $argList -NoNewWindow -Wait -RedirectStandardError $log -RedirectStandardOutput "$log.out"
        Remove-Item "$log.out" -ErrorAction SilentlyContinue
        $sw.Stop()
        Add-Content $log ("wall_seconds " + $sw.Elapsed.TotalSeconds)
        Write-Host ("{0,-22} {1,8:N1} s" -f $name, $sw.Elapsed.TotalSeconds)
    }
}
finally {
    Pop-Location
}
