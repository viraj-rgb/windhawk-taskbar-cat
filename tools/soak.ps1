# Runs the harness for a while and samples its handle and GDI object counts,
# to catch leaks in the window/rendering code. A healthy run stays flat.
#
#   .\tools\soak.ps1 -Minutes 8

param([int]$Minutes = 8, [int]$SampleSeconds = 30)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$harness = Join-Path $root 'build\harness.exe'

Add-Type @'
using System; using System.Runtime.InteropServices;
public static class Gui {
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);
}
'@

$proc = Start-Process $harness -ArgumentList ($Minutes * 60) -PassThru -WindowStyle Hidden
$samples = @()
try {
    while (-not $proc.HasExited) {
        Start-Sleep -Seconds $SampleSeconds
        $proc.Refresh()
        if ($proc.HasExited) { break }
        $sample = [pscustomobject]@{
            Minute  = [math]::Round(((Get-Date) - $proc.StartTime).TotalMinutes, 1)
            Handles = $proc.HandleCount
            Gdi     = [Gui]::GetGuiResources($proc.Handle, 0)
            User    = [Gui]::GetGuiResources($proc.Handle, 1)
            MemMB   = [math]::Round($proc.WorkingSet64 / 1MB, 1)
        }
        $samples += $sample
        $sample | Format-Table -HideTableHeaders | Out-String | Write-Host -NoNewline
    }
} finally {
    if (-not $proc.HasExited) { $proc.Kill() }
}
"exit code: $($proc.ExitCode)"
if ($samples.Count -ge 2) {
    $first = $samples[1]; $last = $samples[-1]   # skip the warm-up sample
    "handles $($first.Handles) -> $($last.Handles), GDI $($first.Gdi) -> $($last.Gdi), " +
    "USER $($first.User) -> $($last.User), memory $($first.MemMB) -> $($last.MemMB) MB"
}
