param([switch]$Package)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $projectRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmakePath = $cmakeCommand.Source }
else {
    $vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (!(Test-Path -LiteralPath $vswherePath)) { throw 'Visual Studio C++ Build Tools와 CMake를 설치하십시오.' }
    $vsPath = & $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmakePath = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
}
& $cmakePath --preset windows-x64
if ($LASTEXITCODE) { throw 'CMake 구성 실패' }
& $cmakePath --build --preset release
if ($LASTEXITCODE) { throw '빌드 실패' }
$ctestPath = Join-Path (Split-Path $cmakePath) 'ctest.exe'
& $ctestPath --preset release
if ($LASTEXITCODE) { throw '단위 시험 실패' }
if ($Package) {
    $cpackPath = Join-Path (Split-Path $cmakePath) 'cpack.exe'
    & $cpackPath --config build/CPackConfig.cmake -C Release -B dist
    if ($LASTEXITCODE) { throw '패키징 실패' }
}
