$ErrorActionPreference = "Stop"

$searchDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$searchExe = Join-Path $searchDir "search.exe"

if (-not (Test-Path -LiteralPath $searchExe)) {
    Write-Host "search.exe was not found in: $searchDir" -ForegroundColor Red
    Write-Host "Build it first with: gcc .\search_dir_in_local.c -o .\search.exe -lshell32"
    exit 1
}

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ([string]::IsNullOrWhiteSpace($userPath)) {
    $paths = @()
} else {
    $paths = $userPath -split ';' | Where-Object { $_ -ne "" }
}

$alreadyInstalled = $false
foreach ($p in $paths) {
    if ($p.TrimEnd('\') -ieq $searchDir.TrimEnd('\')) {
        $alreadyInstalled = $true
        break
    }
}

if (-not $alreadyInstalled) {
    $newPath = (($paths + $searchDir) -join ';')
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host "Added to user PATH:" -ForegroundColor Green
    Write-Host "  $searchDir"
} else {
    Write-Host "Already in user PATH:" -ForegroundColor Yellow
    Write-Host "  $searchDir"
}

Write-Host ""
Write-Host "Close and reopen your terminal, then run:" -ForegroundColor Cyan
Write-Host "  search cmake"
Write-Host "  search cmake C:\msys64"
Write-Host "  search `"name with spaces`" `"D:\New folder`""

