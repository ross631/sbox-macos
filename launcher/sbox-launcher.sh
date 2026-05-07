#!/bin/bash
# Roll-your-own s&box launcher.
#
# Configures wine to use a custom prefix + custom MoltenVK, then launches
# Steam. Once Steam is up, click Play on s&box. Patched MoltenVK reports
# enough descriptor headroom that s&box's launch gate passes.
#
# Configure these before running:
#   WINE_BUNDLE       absolute path to your Wineskin/Sikarugir-derived
#                     wine bundle (e.g. ~/Applications/Sikarugir/steam.app)
#   WINE_WRAPPER_BIN  absolute path to the C wrapper from wine-wrapper/
#                     (must have a wineserver symlink next to it)
#   PREFIX            absolute path to your dedicated WINEPREFIX
#                     (e.g. ~/Wine/sbox-prefix)
#   MVK_DIR           absolute path to dir containing your patched
#                     libMoltenVK.dylib (e.g. ~/Wine/sbox-moltenvk)
#
# This launcher is fully isolated from the bundle's own prefix — point
# PREFIX somewhere outside the bundle and it stays untouched.

: "${WINE_BUNDLE:?set WINE_BUNDLE}"
: "${WINE_WRAPPER_BIN:?set WINE_WRAPPER_BIN}"
: "${PREFIX:?set PREFIX}"
: "${MVK_DIR:?set MVK_DIR}"

LOG="${LOG:-$HOME/.sbox-launcher.log}"
exec >"$LOG" 2>&1
echo "=== sbox-launcher started $(date) ==="

# Reap any stragglers scoped to this prefix only — won't touch a parallel
# wine session running against a different prefix.
if pgrep -af "wineserver.*$PREFIX" >/dev/null; then
    echo "Reaping existing wineserver for $PREFIX…"
    WINEPREFIX="$PREFIX" \
    DYLD_FALLBACK_LIBRARY_PATH="$MVK_DIR:$WINE_BUNDLE/Contents/Frameworks" \
    WINE_BUNDLE="$WINE_BUNDLE" MVK_DIR="$MVK_DIR" \
        "$(dirname "$WINE_WRAPPER_BIN")/wineserver" -k -w 2>/dev/null || true
    pkill -TERM -f "wineserver.*$PREFIX|sbox.*\.exe" 2>/dev/null || true
    for _ in 1 2 3; do
        pgrep -qf "wineserver.*$PREFIX" || break
        sleep 1
    done
    pkill -9 -f "wineserver.*$PREFIX" 2>/dev/null || true
fi

export WINEPREFIX="$PREFIX"
export WINE_BUNDLE MVK_DIR

# Wine flags
export WINEMSYNC=1
export WINEESYNC=0
export WINEDEBUG=-plugplay,+loaddll
export WINEBOOT_HIDE_DIALOG=1

# MoltenVK config — surface arg-buffer limits to apps that don't ask for
# VK_EXT_descriptor_indexing. Required for Source 2.
export MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=1
export MVK_CONFIG_EMULATE_SINGLE_TEXEL_ALIGNMENT=0
export MVK_CONFIG_API_VERSION_TO_ADVERTISE=4210688
export MVK_CONFIG_RESUME_LOST_DEVICE=1

# Steam.exe is x86_64 Windows; spawned via Rosetta. Without this, AVX is
# hidden in CPUID and Source 2 bails before the renderer loads.
export ROSETTA_ADVERTISE_AVX=1

echo "Env primed. Launching Steam through wrapper at $WINE_WRAPPER_BIN"

# Steam exits 42 to signal "I just self-updated, restart me". On Windows
# the bootstrap shortcut handles this; under exec'd wine we have to loop
# ourselves. Cap at 3 restarts to avoid spinning on a real failure.
for i in 1 2 3; do
    "$WINE_WRAPPER_BIN" 'C:\Program Files (x86)\Steam\steam.exe'
    rc=$?
    echo "--- wine exited $rc (attempt $i) ---"
    [ "$rc" = "42" ] || break
done
exit "$rc"
