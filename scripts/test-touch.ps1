[CmdletBinding()]
param([string]$Compiler = 'gcc')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$CompilerBin = Split-Path -Parent (Get-Command $Compiler -ErrorAction Stop).Source
$Root = Split-Path -Parent $PSScriptRoot
$Touch = Join-Path $Root 'AndroidProject/app/jni/touch'
$Output = Join-Path $Root 'dist/touch-tests'
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Exe = Join-Path $Output 'touch_controls_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS "-I$Touch" `
    (Join-Path $Touch 'touch_controls.c') (Join-Path $Touch 'tests/touch_controls_test.c') -lm -o $Exe
if ($LASTEXITCODE -ne 0) { throw "Touch test compilation failed: $LASTEXITCODE" }
& $Exe
if ($LASTEXITCODE -ne 0) { throw "Touch tests failed: $LASTEXITCODE" }
$Alpha11Exe = Join-Path $Output 'touch_alpha11_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS "-I$Touch" `
    (Join-Path $Touch 'touch_controls.c') (Join-Path $Touch 'tests/touch_alpha11_test.c') -lm -o $Alpha11Exe
if ($LASTEXITCODE -ne 0) { throw "Touch Alpha 1.1 test compilation failed: $LASTEXITCODE" }
& $Alpha11Exe
if ($LASTEXITCODE -ne 0) { throw "Touch Alpha 1.1 tests failed: $LASTEXITCODE" }
$Alpha2Exe = Join-Path $Output 'touch_alpha2_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS "-I$Touch" `
    (Join-Path $Touch 'touch_controls.c') (Join-Path $Touch 'tests/touch_alpha2_test.c') -lm -o $Alpha2Exe
if ($LASTEXITCODE -ne 0) { throw "Touch Alpha 2 test compilation failed: $LASTEXITCODE" }
& $Alpha2Exe
if ($LASTEXITCODE -ne 0) { throw "Touch Alpha 2 tests failed: $LASTEXITCODE" }
$SurfaceExe = Join-Path $Output 'touch_surface_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS -DDECLSPEC= -DSDL_MAIN_HANDLED `
    "-I$(Join-Path $Root '.deps/SDL2/include')" "-I$Touch" (Join-Path $Touch 'tests/touch_surface_test.c') -o $SurfaceExe
if ($LASTEXITCODE -ne 0) { throw "Touch surface test compilation failed: $LASTEXITCODE" }
& $SurfaceExe
if ($LASTEXITCODE -ne 0) { throw "Touch surface tests failed: $LASTEXITCODE" }
$BridgeExe = Join-Path $Output 'touch_bridge_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DTOUCH_HEADLESS `
    "-I$(Join-Path $Touch 'tests/stubs')" "-I$Touch" `
    (Join-Path $Touch 'touch_controls.c') (Join-Path $Touch 'touch_shock_bridge.c') `
    (Join-Path $Touch 'tests/touch_bridge_test.c') -lm -o $BridgeExe
if ($LASTEXITCODE -ne 0) { throw "Touch bridge test compilation failed: $LASTEXITCODE" }
& $BridgeExe
if ($LASTEXITCODE -ne 0) { throw "Touch bridge tests failed: $LASTEXITCODE" }
$TargetExe = Join-Path $Output 'touch_target_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 `
    "-I$(Join-Path $Touch 'tests/stubs')" "-I$Touch" `
    (Join-Path $Touch 'tests/touch_target_test.c') -o $TargetExe
if ($LASTEXITCODE -ne 0) { throw "Touch target test compilation failed: $LASTEXITCODE" }
& $TargetExe
if ($LASTEXITCODE -ne 0) { throw "Touch target tests failed: $LASTEXITCODE" }

# Compile the real SDL queue adapter in one TU. Whole-program optimization
# omits unrelated platform entry points, so this test needs only SDL headers.
$Includes = @(
    "-I$Root", "-I$(Join-Path $Root '.deps/SDL2/include')", "-I$Touch",
    "-I$(Join-Path $Root 'SourceSnapshot/src/GameSrc/Headers')",
    "-I$(Join-Path $Root 'SourceSnapshot/src/MacSrc')",
    "-I$(Join-Path $Root 'SourceSnapshot/src/Libraries/H')",
    "-I$(Join-Path $Root 'SourceSnapshot/src/Libraries/LG/Source/LOG/src')"
)
$Includes += Get-ChildItem (Join-Path $Root 'SourceSnapshot/src/Libraries') -Directory | ForEach-Object {
    "-I$(Join-Path $_.FullName 'Source')"
}
$InputExe = Join-Path $Output 'touch_input_test.exe'
& $Compiler "-B$CompilerBin/" -std=gnu11 -O2 -fwhole-program -D__ANDROID__ -DDECLSPEC= -DSDL_MAIN_HANDLED -DTOUCH_HEADLESS `
    @Includes -include precompiled.h (Join-Path $Touch 'tests/touch_input_test.c') -lm -o $InputExe
if ($LASTEXITCODE -ne 0) { throw "Touch input test compilation failed: $LASTEXITCODE" }
& $InputExe
if ($LASTEXITCODE -ne 0) { throw "Touch input tests failed: $LASTEXITCODE" }

$RenderExe = Join-Path $Output 'touch_render_test.exe'
& $Compiler "-B$CompilerBin/" -std=c11 -Wall -Wextra -Werror -pedantic -O2 -DDECLSPEC= -DSDL_MAIN_HANDLED `
    "-I$(Join-Path $Root '.deps/SDL2/include')" "-I$Touch" `
    (Join-Path $Touch 'touch_controls.c') (Join-Path $Touch 'tests/touch_render_test.c') -lm -o $RenderExe
if ($LASTEXITCODE -ne 0) { throw "Touch render test compilation failed: $LASTEXITCODE" }
& $RenderExe
if ($LASTEXITCODE -ne 0) { throw "Touch render tests failed: $LASTEXITCODE" }
