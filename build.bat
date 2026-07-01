@echo off
setlocal

echo Setting environment variables PATH, DEVKITARM, DEVKITPRO, LIBGBA...
set "PATH=C:\devkitPro\msys\bin;C:\devkitPro\msys2\usr\bin;C:\devkitPro\devkitARM\bin;%PATH%"
set "DEVKITARM=/c/devkitPro/devkitARM"
set "DEVKITPRO=/c/devkitPro"
set "LIBGBA=/c/devkitPro/libgba"
echo Done!

if not exist images\blank\SD_LIST.h (
	echo Missing images\blank\SD_LIST.h
	echo Add SD_LIST.bmp, SD_HORIZONTAL.bmp, SD_VERTICAL.bmp, SET.bmp, START.bmp, and HELP.bmp to images\blank, then run Build Skin Files.bat first.
	goto :done
)
if not exist images\blank\SET.h (
	echo Missing images\blank\SET.h
	echo Add SD_LIST.bmp, SD_HORIZONTAL.bmp, SD_VERTICAL.bmp, SET.bmp, START.bmp, and HELP.bmp to images\blank, then run Build Skin Files.bat first.
	goto :done
)
if not exist images\blank\START.h (
	echo Missing images\blank\START.h
	echo Add SD_LIST.bmp, SD_HORIZONTAL.bmp, SD_VERTICAL.bmp, SET.bmp, START.bmp, and HELP.bmp to images\blank, then run Build Skin Files.bat first.
	goto :done
)
if not exist images\blank\HELP.h (
	echo Missing images\blank\HELP.h
	echo Add SD_LIST.bmp, SD_HORIZONTAL.bmp, SD_VERTICAL.bmp, SET.bmp, START.bmp, and HELP.bmp to images\blank, then run Build Skin Files.bat first.
	goto :done
)

echo Making it with make...
set "BUILD_CWD=%CD%"
set "BUILD_DRIVE="
set "BUILD_OUTPUT_GBA=x.gba"

echo "%BUILD_CWD%" | findstr /C:" " >NUL
if not errorlevel 1 (
	for %%D in (X W V U T S R Q P O N M L K J I H G) do (
		if not exist %%D:\NUL (
			set "BUILD_DRIVE=%%D:"
			goto :drive_found
		)
	)
	echo Could not find a free temporary drive letter for this path.
	goto :done
)

:drive_found
if defined BUILD_DRIVE (
	set "BUILD_OUTPUT_GBA=%BUILD_DRIVE:~0,1%.gba"
	subst %BUILD_DRIVE% "%BUILD_CWD%" >NUL
	if errorlevel 1 (
		echo Could not map %BUILD_DRIVE% to "%BUILD_CWD%".
		goto :done
	)
	pushd %BUILD_DRIVE%\
) else (
	pushd "%BUILD_CWD%"
)

if exist "%BUILD_OUTPUT_GBA%" del "%BUILD_OUTPUT_GBA%" >NUL
if exist x.gba del x.gba >NUL
make clean
make
set "MAKE_EXIT=%ERRORLEVEL%"
popd

if defined BUILD_DRIVE subst %BUILD_DRIVE% /D >NUL
if not "%MAKE_EXIT%"=="0" goto :done
if exist "%BUILD_OUTPUT_GBA%" (
	copy /Y "%BUILD_OUTPUT_GBA%" ezkernelnew.bin >NUL
	echo Output: ezkernelnew.bin
)
echo Done!

:done
pause
