@echo off
rem Abre a ferramenta de revisao das lesoes (roda so neste computador).
cd /d "%~dp0"
start "" http://127.0.0.1:8770
python scripts\revisao_servidor.py --porta 8770
pause
