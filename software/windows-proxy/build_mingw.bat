@echo off
set PATH=C:\Qt\Qt5.12.8\Tools\mingw730_64\bin;C:\Qt\Qt5.12.8\5.12.8\mingw73_64\bin;%PATH%

cd /d D:\app\YooZi\software\windows-proxy

if exist build rmdir /s /q build
mkdir build
cd build

qmake ..\windows-proxy.pro
mingw32-make