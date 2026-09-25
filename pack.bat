@echo off
chcp 65001 >nul
echo ========================================
echo  BakerChat 打包脚本
echo ========================================

set DIST=D:\c++项目\BakerChat\dist
set OUT=D:\c++项目\BakerChat\BakerChat-release

echo.
echo [1/5] 清理旧包...
if exist "%OUT%" rmdir /S /Q "%OUT%"
mkdir "%OUT%\BakerChat"

echo.
echo [2/5] 复制文件...
xcopy /E /I /Y "%DIST%\BakerChat.exe" "%OUT%\BakerChat\"
xcopy /E /I /Y "%DIST%\web" "%OUT%\BakerChat\web\"
xcopy /E /I /Y "%DIST%\prompts" "%OUT%\BakerChat\prompts\"

echo.
echo [3/5] 清理不需要的文件...
del /Q "%OUT%\BakerChat\prompts\*.zip" 2>nul
rmdir /S /Q "%OUT%\BakerChat\prompts\.vscode" 2>nul
rmdir /S /Q "%OUT%\BakerChat\config" 2>nul

echo.
echo [4/5] 复制 README...
copy /Y "%DIST%\README.md" "%OUT%\BakerChat\" 2>nul

echo.
echo [5/5] 打包 zip...
powershell -Command "Compress-Archive -Path '%OUT%\BakerChat' -DestinationPath '%OUT%\BakerChat.zip' -Force"

echo.
echo ========================================
echo  打包完成！
echo  输出: %OUT%\BakerChat.zip
echo ========================================
pause