"""Compile the real preference functions in isolation; test pack installer bytes.
Usage: python scripts/test-audio2.py --compiler PATH_TO_GCC
Playback tests also run through scripts/test-audio1.ps1 (AUDIO1 regressions).
"""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--compiler', default='gcc')
args = parser.parse_args()
out = root / 'dist/audio2-tests'
out.mkdir(parents=True, exist_ok=True)
prefs = (root / 'SourceSnapshot/src/MacSrc/Prefs.c').read_text()
constants = '\n'.join(line for line in prefs.splitlines() if line.startswith('static const char *PREF_'))
# Compile unchanged production parsing/saving functions, excluding unrelated
# engine/keybinding headers and replacing only the file path/global sync boundary.
functions = prefs[prefs.index('static char *trim('):prefs.index('//  Set the corresponding Shock globals')]
defaults = prefs[prefs.index('void SetDefaultPrefs('):prefs.index('static char *GetPrefsPathFilename(')]
parsers = prefs[prefs.index('static int parse_percent('):prefs.index('void SetDefaultPrefs(')]
fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "Prefs.h"
#include "ExternalMusic.h"
#define INFO(...) ((void)0)
ShockPrefs gShockPrefs;
int which_lang, curr_vol_lev=75, sfx_on=1, curr_sfx_vol=100, curr_alog_vol=100;
int mode_id=4, DoubleSize, _fr_global_detail=3, audiolog_setting=1;
static char *GetPrefsPathFilename(void) { return "prefs-test.txt"; }
static void SetShockGlobals(void) { curr_vol_lev=gShockPrefs.soMusicVolume; }
'''
checks = r'''
static void check(const char *text, int expected) {
    FILE *f=fopen("prefs-test.txt","wb"); assert(f);
    fputs(text,f); fclose(f);
    SetDefaultPrefs();
    gShockPrefs.soSoundtrack=999;
    assert(LoadPrefs()==0 && gShockPrefs.soSoundtrack==expected);
}
int main(void) {
    remove("prefs-test.txt"); SetDefaultPrefs(); curr_vol_lev=100;
    assert(LoadPrefs()==0 && curr_vol_lev==75); /* first-run defaults applied before save */
    check("",0);
    assert(gShockPrefs.soMasterVolume==100 && gShockPrefs.soAmbientVolume==70);
    assert(gShockPrefs.soDynamicMusic && !gShockPrefs.soEnhancedAmbience);
    check("music-volume = nonsense\nsfx-volume = -2\nalog-volume = 900\n",0);
    assert(gShockPrefs.soMusicVolume==75 && gShockPrefs.soSfxVolume==100);
    assert(gShockPrefs.soAudioLogVolume==100);
    check("master-volume = 35\nambient-volume = 44\ndynamic-music = false\nenhanced-ambience = true\n",0);
    assert(gShockPrefs.soMasterVolume==35 && gShockPrefs.soAmbientVolume==44);
    assert(!gShockPrefs.soDynamicMusic && gShockPrefs.soEnhancedAmbience);
    check("master-volume = bogus\nambient-volume = 500\ndynamic-music = sometimes\nenhanced-ambience = sometimes\n",0);
    assert(gShockPrefs.soMasterVolume==100 && gShockPrefs.soAmbientVolume==70);
    assert(gShockPrefs.soDynamicMusic && !gShockPrefs.soEnhancedAmbience);
    assert(!gShockPrefs.soExplorationMusicBreaks);
    check("exploration_music_breaks = true\n",0);
    assert(gShockPrefs.soExplorationMusicBreaks);
    check("exploration_music_breaks = false\n",0);
    assert(!gShockPrefs.soExplorationMusicBreaks);
    check("soundtrack = original\n",0);
    check("soundtrack = remake\n",1);
    check("soundtrack = ReMaKe\n",1);
    check("soundtrack = invalid\n",0);
    check("soundtrack = \n",0);
    check("replacement_medical_music = true\n",1);
    check("replacement_medical_music = false\n",0);
    check("replacement_medical_music = true\nsoundtrack = original\n",0);
    check("soundtrack = original\nreplacement_medical_music = true\n",0);
    check("soundtrack = invalid\nreplacement_medical_music = true\n",0);
    check("replacement_medical_music = false\nsoundtrack = remake\n",1);
    check("soundtrack = remake\nqa_cheats = true\nmusic-volume = 63\nsfx-volume = 47\ngamma = 38\n",1);
    assert(gShockPrefs.qaCheats && gShockPrefs.soMusicVolume==63);
    assert(gShockPrefs.soSfxVolume==47 && gShockPrefs.doGamma==38);
    assert(SavePrefs()==0);
    FILE *f=fopen("prefs-test.txt","rb"); assert(f);
    char data[4096]={0}; assert(fread(data,1,sizeof(data)-1,f)>0); fclose(f);
    assert((unsigned char)data[0]!=0xef && (unsigned char)data[0]!=0xff);
    assert(strstr(data,"soundtrack = remake") && !strstr(data,"replacement_medical_music"));
    assert(strstr(data,"exploration_music_breaks = false"));
    assert(strstr(data,"qa_cheats = true") && strstr(data,"gamma = 38"));
    assert(LoadPrefs()==0 && gShockPrefs.soSoundtrack==1);
    const char *expected[]={"reactor","medical","research","maintenance","storage",
        "flightdeck","executive","engineering","security","bridge",NULL,"groves","groves","groves"};
    for(int i=0;i<14;++i) {
        const char *path=ExternalMusicPath(1,i);
        if(expected[i]) { char p[80]; snprintf(p,sizeof(p),"music/remake/%s.mp3",expected[i]); assert(!strcmp(path,p)); }
        else assert(!path);
        assert(!ExternalMusicPath(0,i));
    }
    assert(!ExternalMusicPath(1,-1) && !ExternalMusicPath(1,14));
    assert(!ExternalMusicPath(999,1) && ExternalMusicMode(NULL)==0);
    puts("AUDIO2 production preferences / precedence / UTF-8 / exact deck IDs: PASS");
    return 0;
}
'''
(out / 'prefs_test.c').write_text(fixture + constants + '\n' + parsers + defaults + functions + checks)
compiler = str(Path(args.compiler).resolve()) if Path(args.compiler).is_file() else args.compiler
subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-O2', '-DRP5NP_QA_TOOLS',
                '-I' + str(root / 'SourceSnapshot/src/MacSrc'), str(out / 'prefs_test.c'),
                str(root / 'SourceSnapshot/src/MacSrc/ExternalMusic.c'), '-o', str(out / 'prefs_test.exe')], check=True)
subprocess.run([str(out / 'prefs_test.exe')], cwd=out, check=True)
spec = importlib.util.spec_from_file_location('installer', root / 'scripts/install-qa-soundtrack.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)
raw = b'\xef\xbb\xbfqa_cheats = true\r\nmusic-volume = 73\r\n# caf\xc3\xa9\r\nsoundtrack = original\r\nsoundtrack = bad\r\nreplacement_medical_music = true'
updated = installer.update_preferences(raw, 'remake')
assert len(installer.FILENAMES) == 33
for deck in ('medical', 'research', 'reactor', 'maintenance', 'storage', 'flightdeck',
             'executive', 'groves', 'engineering', 'security', 'bridge'):
    for suffix in ('', '_explore', '_combat'):
        assert deck + suffix + '.mp3' in installer.FILENAMES
assert updated == b'qa_cheats = true\r\nmusic-volume = 73\r\n# caf\xc3\xa9\r\nreplacement_medical_music = true\nsoundtrack = remake\n'
assert installer.update_preferences(b'soundtrack = remake\nexploration_music_breaks = false\n', breaks='on') == \
       b'soundtrack = remake\nexploration_music_breaks = true\n'
try:
    installer.update_preferences(b'\xff\xfea\x00', 'original')
    raise AssertionError('UTF-16 must not silently corrupt unrelated preferences')
except UnicodeDecodeError:
    pass
# Verify failed staging never replaces the destination.
device = installer.Device('unused', '')
device.run = lambda *args, **kwargs: b''
commands = []
def short_transfer(command, **kwargs):
    commands.append(command)
    return b'2' if command.startswith('wc') else b''
device.shell = short_transfer
try:
    device.put('files/prefs.txt', data=b'longer')
    raise AssertionError('Truncated transfer accepted')
except RuntimeError:
    assert not any(command.startswith('mv ') for command in commands)
print('AUDIO2 installer: BOM removal / unrelated bytes / UTF-16 rejection / failed transfer preservation: PASS')
