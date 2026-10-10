@echo off

del *.obj 2>nul
del *.err 2>nul

wcc -2 -ox -oa -ol -mm -oneatx -bt=dos mpumid.c
wcc -2 -ox -oa -ol -mm -oneatx -bt=dos smf.c
wcc -2 -ox -oa -ol -mm -oneatx -bt=dos timer.c
wcc -2 -ox -oa -ol -mm -oneatx -bt=dos vgafont.c
wcc -2 -ox -oa -ol -mm -bt=dos screen.c
wcc -2 -ox -oa -ol -mm -bt=dos browser.c
wcc -2 -ox -oa -ol -mm -bt=dos effect.c
wcc -2 -ox -oa -ol -mm -bt=dos about.c

if errorlevel 1 (
    echo.
    echo ABORTADO: fallo la compilacion de algun .c -- ver arriba.
    exit /b 1
)

wlink system dos option stack=13k file mpumid.obj,smf.obj,timer.obj,vgafont.obj,screen.obj,browser.obj,effect.obj,about.obj name MPUMID.EXE

if errorlevel 1 (
    echo.
    echo ABORTADO: fallo el LINKER -- ver arriba, no se genero un .exe nuevo.
    exit /b 1
)

echo.
echo Listo: MPUMID.EXE
