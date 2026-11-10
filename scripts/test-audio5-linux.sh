#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
out="$root/dist/audio5-tests"
mkdir -p "$out"
exe=""
case "$(uname -s)" in MINGW*|MSYS*) exe=".exe";; esac
python3 "$root/scripts/test-audio2.py"
common=(-std=c99 -Wall -Wextra -Werror -pedantic -O2 -DDECLSPEC= -DSDL_MAIN_HANDLED
    "-I$root/.deps/SDL2/include" "-I$root/.deps/SDL2_mixer/include"
    "-I$root/SourceSnapshot/src/MacSrc")
gcc "${common[@]}" "$root/SourceSnapshot/src/MacSrc/ExternalMusic.c" \
    "$root/tests/audio/external_music_test.c" -o "$out/external_music_nomixer$exe"
"$out/external_music_nomixer$exe"
gcc "${common[@]}" -DUSE_SDL_MIXER -DAUDIO4_TEST_TIMING \
    "$root/SourceSnapshot/src/MacSrc/ExternalMusic.c" \
    "$root/tests/audio/dynamic_music_test.c" -o "$out/dynamic_music$exe"
"$out/dynamic_music$exe"
gcc "${common[@]}" -DUSE_SDL_MIXER -DAUDIO4_TEST_TIMING \
    "$root/SourceSnapshot/src/MacSrc/ExternalMusic.c" \
    "$root/tests/audio/external_music_test.c" -o "$out/external_music$exe"
"$out/external_music$exe"
gcc -std=gnu11 -Wall -Wextra -Werror -O2 "-I$root/tests/audio/stubs" \
    "$root/tests/audio/ambient_director_test.c" -o "$out/ambient_director$exe"
fixture="$(mktemp -d "$out/ambient-fixture-XXXXXXXX")"
(cd "$fixture" && "$out/ambient_director$exe")
gcc -std=c11 -Wall -Wextra -Werror -pedantic -O2 \
    "$root/tests/audio/audio_master_gain_test.c" -lm -o "$out/audio_master_gain$exe"
"$out/audio_master_gain$exe"
python3 "$root/scripts/validate-ambient-assets.py" "$root/audio/ambient" \
    --json "$out/ambient-validation.json"
python3 "$root/tests/audio/test_ambient_validator.py"
echo 'AUDIO5_HOST_TESTS=PASS'
