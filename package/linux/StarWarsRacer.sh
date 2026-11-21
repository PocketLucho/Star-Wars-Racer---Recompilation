#!/bin/bash
# STAR WARS RACER ARCADE - RECOMP launcher (Linux / SteamOS).
# Use this file to start the game, also as the "non-Steam game" in Steam (Game Mode).
# Usa este archivo para arrancar el juego, también como "juego no de Steam" en Steam (Modo Juego).
# If something fails, the reason is written to swracer.log and shown in a window.
cd "$(dirname "$(readlink -f "$0")")"
chmod +x swracer build_game.sh 2>/dev/null

message() {	# title text
	if command -v kdialog >/dev/null; then kdialog --title "$1" --error "$2"
	elif command -v zenity >/dev/null; then zenity --error --title "$1" --text "$2"
	elif command -v notify-send >/dev/null; then notify-send "$1" "$2"
	fi
	printf '%s\n%s\n' "$1" "$2" >&2
}
in_terminal() {	# open a terminal window running the builder for this game
	for t in konsole gnome-terminal xfce4-terminal xterm; do
		command -v $t >/dev/null || continue
		case $t in
			gnome-terminal) gnome-terminal --wait -- env SWR_ONLY=arcade bash ./build_game.sh; return 0 ;;
			*) $t -e env SWR_ONLY=arcade bash ./build_game.sh; return 0 ;;
		esac
	done
	return 1
}
GAME=""; for f in roms/podrace.zip roms/swracer.zip podrace.zip swracer.zip; do [ -z "$GAME" ] && [ -f "$f" ] && GAME="$f"; done
# The recompiled code is built once from YOUR files. Without it the game runs on the interpreter (slower).
if [ ! -f libswr_game.so ] && [ -n "$GAME" ]; then
	if [ -t 1 ]; then SWR_ONLY=arcade ./build_game.sh
	else in_terminal; fi
	[ -f libswr_game.so ] || message "STAR WARS RACER ARCADE - RECOMP" "Falta libswr_game.so: el juego irá más lento. Ejecuta build_game.sh una vez (Modo Escritorio, en una terminal).

libswr_game.so is missing: the game will run slower. Run build_game.sh once (Desktop Mode, in a terminal)."
fi

./swracer ${GAME:+"$GAME"} "$@" 2> >(tee swracer.log >&2)
rc=$?
# 130/137/143 = closed by the user or the system (Ctrl+C, kill): no message
if [ $rc -ne 0 ] && [ $rc -ne 130 ] && [ $rc -ne 137 ] && [ $rc -ne 143 ]; then
	message "STAR WARS RACER ARCADE - RECOMP" "El juego se ha cerrado con un error ($rc). / The game exited with an error ($rc).

$(tail -n 15 swracer.log)"
fi
exit $rc
