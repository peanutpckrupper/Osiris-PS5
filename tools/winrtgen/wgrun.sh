#!/bin/bash
S=/tmp/claude-0/-home-claude/a4006f9f-5295-57d4-a698-84cb6b64f70e/scratchpad; G=$S/wg
sleep 2; cp /mnt/user-data/uploads/Halo5-Guardians-Dev/forge-shim/bin/ForgeDesktopShim.dll $G/h5campaign/
cd $G; export WINEPREFIX=${WP:-$S/wp5} WINEDEBUG=${WD:--all,err+module,fixme+combase,err+seh} WINEDLLOVERRIDES="mscoree,mshtml="
wineserver -k 2>/dev/null; timeout ${T:-90} xvfb-run -a wine halo5forge.exe > $S/wg-run.txt 2>&1; echo rc=$?
grep -v "RoGetApartmentIdentifier\|X connection" $S/wg-run.txt | tail -${N:-25}; echo ----; tail -${M:-15} h5campaign/logs/game.log
