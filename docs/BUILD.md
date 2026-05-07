# End-to-end setup guide

> ## ❌ This setup does not actually run s&box on M4 Pro
>
> See [`CONCLUSION.md`](CONCLUSION.md) for the experimental record:
> three patch attempts, all hit the same GPU-contention failure mode,
> first attempt escalated to a kernel panic + reboot.
>
> This guide is preserved for two reasons:
> 1. As a *recipe* for building patched MoltenVK against arbitrary
>    upstream commits, building the wine wrapper, setting up an
>    isolated Wineskin-derived prefix, and running the descriptor
>    probe — useful for anyone debugging similar Vulkan-on-Metal
>    issues, even outside s&box.
> 2. So the diagnosis writeup in this repo is reproducible end to end
>    (anyone with similar hardware can rerun the probe and see the
>    legacy 1,280 / 80 numbers, swap in patched MoltenVK and see
>    1,000,000+, then watch the engine fail in the same way under
>    sandbox-gamemode load).
>
> If you proceed with the patch despite the warning, the launcher in
> [`../launcher/sbox-launcher.sh`](../launcher/sbox-launcher.sh)
> includes a self-kill watchdog that catches `VK_TIMEOUT` /
> `QueuePresentAndWait` distress signals before macOS's WindowServer
> watchdog fires. That should reduce panic risk significantly. **It
> won't make the game playable; it will just make the failure mode
> survivable.**

---

This walks through standing up a new, isolated wine prefix
designed to run s&box on Apple Silicon, without touching any
existing wine bundle's own prefix. Other games already running in
your Wineskin/Sikarugir setup keep working.

## Prerequisites

- macOS Apple Silicon (tested on M4 Pro / macOS 26.4)
- Xcode command-line tools (`xcode-select --install`)
- A Wineskin / Sikarugir / Kegworks app bundle providing wine 9.x or
  10.x (we'll borrow its wine binary; we won't modify it)
- Homebrew, with `winetricks` installable
- ~10 GB free disk (~3 GB s&box, ~1 GB MoltenVK build, ~2 GB
  prefix + Steam)
- A Steam account that owns s&box

Set these at the top of your shell session and reuse them
throughout:

```sh
export WINE_BUNDLE="$HOME/Applications/Sikarugir/steam.app"  # or your bundle
export WORK="$HOME/sbox-rollyourown"
export PREFIX="$WORK/prefix"
export MVK_DIR="$WORK/moltenvk"
export WINE_BIN_DIR="$WORK/bin"
mkdir -p "$WORK" "$PREFIX" "$MVK_DIR" "$WINE_BIN_DIR"
```

## 1. Build patched MoltenVK

```sh
cd "$WORK"
git clone https://github.com/KhronosGroup/MoltenVK.git
cd MoltenVK
git apply /path/to/sbox-macos/moltenvk/descriptor-limits.patch
./fetchDependencies --macos
make macos
cp Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib "$MVK_DIR/"
```

Verify with the probe:

```sh
cd /path/to/sbox-macos/probe
ln -s "$MVK_DIR/libMoltenVK.dylib" libMoltenVK.dylib
ln -s "$WORK/MoltenVK/MoltenVK/MoltenVK/include" headers
make VULKAN_HEADERS_DIR=headers
MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=1 ./probe
```

Expected output:

```
device[0] Apple M? Pro
  maxDescriptorSetSampledImages: 1000000 (sbox needs 65536)
  maxDescriptorSetSamplers:      <large number> (sbox needs 2048)
```

## 2. Build the wine wrapper

```sh
cd /path/to/sbox-macos/wine-wrapper
make
PREFIX="$WINE_BIN_DIR" make install
```

This produces `$WINE_BIN_DIR/wine` and a `wineserver` symlink.

Smoke-test:

```sh
WINE_BUNDLE="$WINE_BUNDLE" MVK_DIR="$MVK_DIR" \
  "$WINE_BIN_DIR/wine" --version
# wine-10.0 (Sikarugir) — or whatever your bundle ships
```

## 3. Initialize the WINEPREFIX

```sh
WINEPREFIX="$PREFIX" \
WINE_BUNDLE="$WINE_BUNDLE" MVK_DIR="$MVK_DIR" \
  "$WINE_BIN_DIR/wine" wineboot --init
```

## 4. Install Visual C++ runtime + .NET 8 desktop runtime

Install Homebrew's `winetricks` if needed (`brew install winetricks`).

When invoking winetricks against our wrapper, pass both `WINE_BIN`
and `WINESERVER_BIN` env vars pointing at the *real* binaries inside
your wine bundle. winetricks does its own arch detection by parsing
`lipo -archs $WINE` and a universal-binary wrapper returns
`"x86_64 arm64"` (with a space) which doesn't match the expected
single-arch tokens. The `WINE_BIN`/`WINESERVER_BIN` escape hatch tells
winetricks where the *real* binaries are for arch checking only.

```sh
WINEPREFIX="$PREFIX" \
WINE="$WINE_BIN_DIR/wine" \
WINESERVER="$WINE_BIN_DIR/wineserver" \
WINE_BIN="$WINE_BUNDLE/Contents/SharedSupport/wine/bin/wine" \
WINESERVER_BIN="$WINE_BUNDLE/Contents/SharedSupport/wine/bin/wineserver" \
WINE_BUNDLE="$WINE_BUNDLE" MVK_DIR="$MVK_DIR" \
WINEMSYNC=1 WINEESYNC=0 W_OPT_UNATTENDED=1 \
  winetricks vcrun2022 dotnetdesktop8
```

Takes 15-30 minutes (mostly the .NET installer downloads + msi runs
inside wine).

## 5. Install Steam

```sh
curl -L "https://cdn.steamstatic.com/client/installer/SteamSetup.exe" \
  -o "$WORK/SteamSetup.exe"

WINEPREFIX="$PREFIX" \
WINE_BUNDLE="$WINE_BUNDLE" MVK_DIR="$MVK_DIR" \
WINEMSYNC=1 WINEESYNC=0 \
  "$WINE_BIN_DIR/wine" "$WORK/SteamSetup.exe" /S
```

Steam.exe should appear at
`$PREFIX/drive_c/Program Files (x86)/Steam/Steam.exe`.

## 6. Configure the launcher

```sh
cp /path/to/sbox-macos/launcher/sbox-launcher.sh "$WORK/sbox-launcher.sh"
chmod +x "$WORK/sbox-launcher.sh"
```

Set `WINE_BUNDLE`, `WINE_WRAPPER_BIN`, `PREFIX`, `MVK_DIR` env vars
before running it (or hard-code them into the script).

## 7. First launch

```sh
WINE_BUNDLE="$WINE_BUNDLE" \
WINE_WRAPPER_BIN="$WINE_BIN_DIR/wine" \
PREFIX="$PREFIX" \
MVK_DIR="$MVK_DIR" \
  "$WORK/sbox-launcher.sh"
```

Steam window appears → log in → install s&box → click Play.

The first launch self-updates Steam, which exits with code 42 to
signal "restart me." The launcher loops on 42 up to 3 times, so
the actual Steam UI shows up on the second iteration.

Watch the log:

```sh
tail -f ~/.sbox-launcher.log
```

s&box's own log lands at:

```
$PREFIX/drive_c/Program Files (x86)/Steam/steamapps/common/sbox/logs/sbox.log
```

A successful launch shows:

```
[engine/RenderSystem] Vulkan Physical Device: Apple ...
[engine/RenderSystem] Vulkan driver version: 0.2.2210
[engine/Engine] SteamAPI_Init succeeded.
[Generic] Loading startup scene: scenes/menu-main.scene
```

No `does not meet requirements` line.

## Optional: make it a clickable .app

Wrap the launcher in a simple `.app` bundle so it shows up in
Applications + can be added to the Dock.

```sh
APP="$HOME/Applications/SBoxRollLauncher.app"
mkdir -p "$APP/Contents/MacOS"
cp "$WORK/sbox-launcher.sh" "$APP/Contents/MacOS/SBoxRollLauncher"
chmod +x "$APP/Contents/MacOS/SBoxRollLauncher"
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>SBoxRollLauncher</string>
  <key>CFBundleIdentifier</key><string>local.rollyourown.sbox</string>
  <key>CFBundleName</key><string>SBoxRollLauncher</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>1.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF
```

Drag the .app onto your Dock.

## Troubleshooting

**"warning: Unknown file arch of /path/to/wine"** — winetricks didn't
get the `WINE_BIN` / `WINESERVER_BIN` escape hatch env vars. Re-check
step 4.

**Steam exits ~42 and never shows a window** — the launcher's loop
should re-spawn it. If it still doesn't, watch `~/.sbox-launcher.log`
for `wine exited 42` followed by the next attempt.

**`could not load ntdll.so`** — the C wrapper isn't substituting
argv[0] correctly. Verify the wrapper is the version from this repo.

**Game still rejects the GPU after the patch** — run the probe and
confirm it actually reports 1,000,000+. If yes but s&box still bails,
something else is in play; please file an issue with the sbox.log
output.
