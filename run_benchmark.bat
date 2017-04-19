cd test-spire
"Bin\x64\Release\test-spire.exe" -scene "burger.fscene" -specialize -benchmark>"..\result.txt"
"Bin\x64\Release\test-spire.exe" -scene "burger.fscene" -benchmark>>"..\result.txt"
cd ../
cd test-original
rem "Bin\x64\Release\test-original.exe" -scene "burger.fscene" -benchmark>>"..\result.txt"
