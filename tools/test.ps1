# Builds and runs the host tests with MSVC (cl must be on PATH).
$ErrorActionPreference = 'Stop'
# pick up cl from the user environment even in shells opened before it was set
$env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
foreach ($k in 'INCLUDE','LIB','LIBPATH') { Set-Item "env:$k" ([Environment]::GetEnvironmentVariable($k,'User')) }
Set-Location (Split-Path $PSScriptRoot)
New-Item build -ItemType Directory -Force | Out-Null
cl /nologo /std:c++17 /EHsc /W3 /I lib\evolve /I test\host /Fo:build\ /Fe:build\test_evolve.exe test\host\test_evolve.cpp lib\evolve\evolve.cpp lib\evolve\quips.cpp
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& build\test_evolve.exe
exit $LASTEXITCODE
