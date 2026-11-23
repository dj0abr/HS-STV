param(
    [Parameter(Mandatory)]
    [string] $PackageDir,
    [string] $Compiler = 'ISCC.exe',
    [string] $OutputDir = $PSScriptRoot
)

$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDir).Path
$required = @(
    'bin/hs-stv.exe', 'bin/hs-stv-rx-core.exe', 'bin/hs-stv-sstv-rx-core.exe',
    'bin/hs-stv-psk-rx-core.exe', 'bin/platforms/qwindows.dll',
    'bin/imageformats/qjpeg.dll', 'bin/libportaudio.dll', 'LICENSE'
)
foreach ($file in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $package $file) -PathType Leaf)) {
        throw "Incomplete Windows package. Missing $file"
    }
}

$cmake = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'CMakeLists.txt') -Raw
if ($cmake -notmatch '(?i)project\(\s*hs_stv\s+VERSION\s+([\d.]+)\s') {
    throw 'Cannot read the HS-STV version from CMakeLists.txt.'
}
$appVersion = $Matches[1]
$compilerPath = (Get-Command -Name $Compiler -CommandType Application).Source
$output = (New-Item -ItemType Directory -Path $OutputDir -Force).FullName
& $compilerPath "/DAppVersion=$appVersion" "/DPackageDir=$package" "/O$output" (Join-Path $PSScriptRoot 'installer-windows.iss')
if ($LASTEXITCODE -ne 0) {
    throw "Inno Setup exited with code $LASTEXITCODE."
}
