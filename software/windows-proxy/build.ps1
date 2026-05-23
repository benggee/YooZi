& "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

$env:PATH = "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;" + $env:PATH

Set-Location "D:\app\YooZi\software\windows-proxy"

if (Test-Path "build") { Remove-Item -Recurse -Force "build" }
New-Item -ItemType Directory -Path "build"
Set-Location "build"

cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:\Qt\Qt5.12.8\5.12.8\msvc2017_64"
cmake --build . --config Release