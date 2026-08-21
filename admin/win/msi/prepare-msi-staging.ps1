param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDirectory,

    [Parameter(Mandatory = $true)]
    [string]$StagingDirectory
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $SourceDirectory -PathType Container)) {
    throw "Application directory does not exist: $SourceDirectory"
}

New-Item -ItemType Directory -Path $StagingDirectory -Force | Out-Null
Copy-Item -Path (Join-Path $SourceDirectory "*") -Destination $StagingDirectory -Recurse -Force

# Qt 6 finds platform plugins at <exedir>\platforms\ and QML imports at <exedir>\qml\
$craftRoot = Split-Path $SourceDirectory -Parent
foreach ($extraDir in @("plugins", "qml")) {
    $src = Join-Path $craftRoot $extraDir
    if (Test-Path -LiteralPath $src -PathType Container) {
        Copy-Item -Path (Join-Path $src "*") -Destination $StagingDirectory -Recurse -Force
    }
}

# The Craft prefix contains development tools alongside the application runtime.
$developmentExtensions = @(
    ".pdb", ".ilk", ".lib", ".exp", ".a", ".la", ".obj",
    ".py", ".pyc", ".whl", ".h", ".hpp", ".cpp", ".c"
)

Get-ChildItem -LiteralPath $StagingDirectory -Recurse -File |
    Where-Object { $developmentExtensions -contains $_.Extension.ToLowerInvariant() } |
    Remove-Item -Force

$applicationExecutable = "@APPLICATION_EXECUTABLE@"
Get-ChildItem -LiteralPath $StagingDirectory -Recurse -File -Filter "*.exe" |
    Where-Object {
        $_.BaseName -notlike "$applicationExecutable*" -and
        $_.Name -ne "QtWebEngineProcess.exe"
    } |
    Remove-Item -Force

$developmentDllPatterns = @(
    "clang*.dll", "libclang*.dll", "llvm*.dll", "LLVM*.dll", "LTO.dll",
    "msys-*.dll", "cyg*.dll"
)

Get-ChildItem -LiteralPath $StagingDirectory -Recurse -File |
    Where-Object {
        $name = $_.Name
        $developmentDllPatterns | Where-Object { $name -like $_ }
    } |
    Remove-Item -Force

Get-ChildItem -LiteralPath $StagingDirectory -Recurse -Directory |
    Where-Object { $_.Name -match "^(python|perl|clang|llvm|cmake|msys|mingw)" } |
    Sort-Object FullName -Descending |
    Remove-Item -Recurse -Force

Write-Host "Prepared MSI staging directory: $StagingDirectory"