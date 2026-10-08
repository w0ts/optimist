@echo off
title Optimist editor
cd /d "%~dp0"
if not exist build\optimist-site\webapp\editor\index.html (
  echo The editor is not built yet: run INSTALL-SLOOP.bat first.
  pause
  exit /b 1
)
start "" http://localhost:8766/webapp/editor/
echo Optimist editor: http://localhost:8766/webapp/editor/  (keep this window open while you use it)
python -m http.server 8766 --bind 127.0.0.1 --directory build/optimist-site
pause
