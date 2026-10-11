param(
    [ValidateSet('Setup','Scan','Inspect','Probe','Bridge','Stop')]
    [string]$Action = 'Bridge',
    [string]$Python = 'python',
    [string]$Port = 'COM11',
    [string]$Address = '',
    [switch]$Background
)
$ErrorActionPreference = 'Stop'
$taskRuntime = Join-Path $PSScriptRoot 'runtime'
$taskPython = Join-Path $PSScriptRoot '.venv/Scripts/python.exe'
$taskScript = Join-Path $PSScriptRoot 'ble_nova_bridge.py'
$taskStop = Join-Path $taskRuntime 'stop'
New-Item -ItemType Directory -Path $taskRuntime -Force | Out-Null
if ($Action -eq 'Stop') {
    New-Item -ItemType File -Path $taskStop -Force | Out-Null
    Write-Output 'Stop requested. The bridge should release its virtual port within one second.'
    exit 0
}
if ($Action -eq 'Setup') {
    & $Python -m venv (Join-Path $PSScriptRoot '.venv')
    if ($LASTEXITCODE -ne 0) { throw 'Python environment creation failed.' }
    & $taskPython -m pip install -r (Join-Path $PSScriptRoot 'requirements.txt')
    if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed.' }
    exit 0
}
if (-not (Test-Path -LiteralPath $taskPython)) {
    throw 'Run Bridge.ps1 -Action Setup -Python <python.exe path> first (Python 3.11 or newer).'
}
$taskArgs = @('-u', $taskScript, $Action.ToLowerInvariant())
if ($Address) { $taskArgs += @('--address', $Address) }
if ($Action -eq 'Bridge') { $taskArgs += @('--port', $Port, '--stop-file', $taskStop) }
if ($Background) {
    if ($Action -ne 'Bridge') { throw '-Background is only supported for Bridge.' }
    # Quote arguments for paths containing spaces; values are passed to Python, not a shell.
    $taskQuoted = @($taskArgs | ForEach-Object { '"' + $_.Replace('"','\"') + '"' })
    $taskStamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
    $taskLog = Join-Path $taskRuntime "bridge-$taskStamp.log"
    $taskErr = Join-Path $taskRuntime "bridge-$taskStamp.err.log"
    $taskProcess = Start-Process -FilePath $taskPython -ArgumentList $taskQuoted -WindowStyle Hidden `
        -RedirectStandardOutput $taskLog -RedirectStandardError $taskErr -PassThru
    Write-Output "Started PID $($taskProcess.Id). Check $taskErr for READY before connecting Nova to COM12."
} else {
    & $taskPython @taskArgs
    if ($LASTEXITCODE -ne 0) { throw 'BLE operation failed; read the message above.' }
}
