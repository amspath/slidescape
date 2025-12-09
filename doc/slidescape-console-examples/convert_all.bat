@echo off

REM This script converts all WSIs with the specified filename extension in the current folder to an output TIFF file.

set INPUT_EXT=isyntax
set JPEG_QUALITY=90
set POSTFIX=".exported"

FOR /R %%f in (.\*%INPUT_EXT%) do (
  slidescape_console.exe "%%f" --export --quality %JPEG_QUALITY% --postfix %POSTFIX%
)
