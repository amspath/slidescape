@echo off

REM This script crops all WSI files in the current folder (with the ROI defined by the first annotation in the associated XML) to an output TIFF file.

set INPUT_EXT=mrxs
set JPEG_QUALITY=90
set POSTFIX=".cropped"

FOR /R %%f in (.\*xml) do (
  slidescape_console.exe "%%~nf.%INPUT_EXT%" --export --first-roi --quality %JPEG_QUALITY% --postfix %POSTFIX%
)
