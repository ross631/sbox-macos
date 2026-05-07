// Mach-O wrapper for invoking a Wineskin/Sikarugir/Kegworks-derivative wine
// binary against a custom WINEPREFIX, with a custom MoltenVK on the dyld
// search path ahead of the wine bundle's bundled MoltenVK.
//
// Why a C binary instead of a shell script:
//   1. macOS SIP strips DYLD_* env vars when SIP-protected binaries (notably
//      /bin/bash, /bin/sh) launch shell scripts. winetricks calls
//      $WINE through such a chain, and a shell-script wrapper wouldn't
//      survive. Setting DYLD_FALLBACK_LIBRARY_PATH inside a Mach-O process
//      sidesteps the strip.
//   2. winetricks runs `lipo -archs $WINE` to detect bitness and chokes on
//      shell scripts. A real Mach-O binary passes that check.
//
// Configure these env vars before invoking:
//   WINE_BUNDLE   absolute path to the .app bundle providing wine
//                 (e.g. ~/Applications/Sikarugir/steam.app). The wrapper
//                 expects "$WINE_BUNDLE/Contents/SharedSupport/wine/bin/wine"
//                 and "$WINE_BUNDLE/Contents/Frameworks" to exist.
//   MVK_DIR       absolute path to a directory containing your custom
//                 libMoltenVK.dylib (placed *first* in the dyld search path).
//
// Build:    make
// Install:  cp wine /somewhere/in/your/PATH; ln -s wine wineserver
//
// At runtime the wrapper detects whether it was invoked as `wine` or
// `wineserver` (via argv[0]) and execs the matching real binary in the
// wine bundle. Substitutes argv[0] with the real path so wine resolves
// ntdll.so via its own bin/../lib/wine layout instead of looking next to
// our wrapper.
//
// MIT-licensed; see LICENSE in repo root.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    const char *bundle = getenv("WINE_BUNDLE");
    const char *mvk_dir = getenv("MVK_DIR");
    if (!bundle || !*bundle) {
        fprintf(stderr, "wine-wrapper: WINE_BUNDLE env var unset\n");
        return 2;
    }

    char dyld_path[8192];
    const char *prev = getenv("DYLD_FALLBACK_LIBRARY_PATH");
    int n;
    if (mvk_dir && *mvk_dir) {
        n = snprintf(dyld_path, sizeof(dyld_path),
                     "%s:%s/Contents/Frameworks", mvk_dir, bundle);
    } else {
        n = snprintf(dyld_path, sizeof(dyld_path),
                     "%s/Contents/Frameworks", bundle);
    }
    if (prev && *prev && (size_t)n < sizeof(dyld_path)) {
        snprintf(dyld_path + n, sizeof(dyld_path) - n, ":%s", prev);
    }
    setenv("DYLD_FALLBACK_LIBRARY_PATH", dyld_path, 1);

    const char *invoked_as = strrchr(argv[0], '/');
    invoked_as = invoked_as ? invoked_as + 1 : argv[0];
    const char *real_name = strstr(invoked_as, "wineserver") ? "wineserver" : "wine";

    char real_path[4096];
    snprintf(real_path, sizeof(real_path),
             "%s/Contents/SharedSupport/wine/bin/%s", bundle, real_name);

    // wine resolves ntdll.so relative to argv[0]'s directory; substitute the
    // real path so the bundle's ../lib/wine/ layout is found.
    argv[0] = real_path;
    execv(real_path, argv);
    perror("execv");
    return 1;
}
