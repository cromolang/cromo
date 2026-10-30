@echo off
rem Runs cromo for Linux in WSL with the arguments of Windows (tools\cromo-wsl.sh), to run
rem the tests with it: tests\run-tests.ps1 -Cromo tools\cromo-wsl.cmd -Flags "--target linux"
wsl.exe -e sh -c "exec sh \"$(wslpath '%~dp0cromo-wsl.sh')\" \"$@\"" cromo %*
