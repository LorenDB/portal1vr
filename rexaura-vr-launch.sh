#!/bin/sh
# Start Rexaura VR from Portal's Steam entry (Linux/Proton). Steam launches
# Portal as "hl2.exe -game portal -steam", and Source uses the first -game on
# the command line, so appending "-game rexaura_vr" would start Portal. This
# replaces the game folder after -game and runs the command otherwise
# unchanged. Set as Portal's Steam launch options:
#   /path/to/rexaura-vr-launch.sh %command% -insecure -fullscreen -novid +mat_queue_mode 0 +mat_vsync 0 +mat_antialias 0
# and clear them again to play Portal.
game=rexaura_vr
previous=
replaced=0
for arg
do
    shift
    if [ "$previous" = "-game" ] && [ "$arg" = "portal" ]; then
        arg=$game
        replaced=1
    fi
    set -- "$@" "$arg"
    previous=$arg
done
if [ "$replaced" = 0 ]; then
    echo "rexaura-vr-launch.sh: no '-game portal' in the command; add %command% to the launch options" >&2
    exit 1
fi
exec "$@"
