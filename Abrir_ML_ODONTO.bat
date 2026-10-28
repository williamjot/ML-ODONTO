@echo off
rem Abre o ML ODONTO (roda só neste computador). Deixe esta janela aberta enquanto usa o programa.
title ML ODONTO
"C:\Program Files\R\R-4.6.1\bin\Rscript.exe" -e "odontolesao::run_app()"
if errorlevel 1 pause
