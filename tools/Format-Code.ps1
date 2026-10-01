param(
    [switch]$Check,
    [string]$Formatter
)

$ErrorActionPreference = 'Stop'
$taskRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path

if (-not $Formatter) {
    $taskCommand = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($taskCommand) {
        $Formatter = $taskCommand.Source
    }
    else {
        $taskExtensionRoot = Join-Path $env:USERPROFILE '.vscode\extensions'
        $taskCandidates = @(Get-ChildItem -LiteralPath $taskExtensionRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -like 'ms-vscode.cpptools-*-win32-x64' } |
            Sort-Object { [version]($_.Name -replace '^ms-vscode\.cpptools-|\-win32-x64$', '') } -Descending |
            ForEach-Object { Join-Path $_.FullName 'LLVM\bin\clang-format.exe' } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
        if ($taskCandidates.Count -gt 0) {
            $Formatter = $taskCandidates[0]
        }
    }
}

if (-not $Formatter) {
    throw 'clang-format not found. Pass -Formatter with the executable path (clang-format 23 or later).'
}

# 只处理自维护框架与测试，第三方库和 CubeMX 生成文件不在范围内。
$taskFiles = @(foreach ($taskDirectory in @('FrameComponets', 'tests\frame')) {
    Get-ChildItem -LiteralPath (Join-Path $taskRoot $taskDirectory) -Recurse -File |
        Where-Object { $_.Extension -in @('.c', '.h', '.cpp', '.hpp') } |
        Sort-Object FullName
})

foreach ($taskFile in $taskFiles) {
    if ($Check) {
        & $Formatter '--style=file' '--dry-run' '--Werror' $taskFile.FullName
    }
    else {
        & $Formatter '--style=file' '-i' $taskFile.FullName
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Formatting failed: $($taskFile.FullName)"
    }
}

$taskVerb = if ($Check) { 'Checked' } else { 'Formatted' }
Write-Output "$taskVerb $($taskFiles.Count) C/C++ files."
