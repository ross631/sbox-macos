# sbox-macos

**Run [s&box](https://sbox.game) on Apple Silicon Macs** by patching
MoltenVK to surface argument-buffer-actual descriptor limits to apps
that don't enable `VK_EXT_descriptor_indexing`.

> ## ⚠️ Stability warning — read before applying
>
> **The patch as originally published triggered a macOS kernel panic
> on M4 Pro when s&box loaded a full sandbox scene.** WindowServer
> (the macOS compositor) couldn't get GPU time, Apple's kernel
> watchdog killed it twice, then panicked the system after 120
> seconds of compositor silence. The Mac rebooted cleanly with a
> crash report — no data loss in this case — but this is real risk.
> Don't run this on a machine where an unexpected reboot would cost
> you something.
>
> Symptom progression observed 2026-05-07:
> 1. Game launches, main menu loads, scene loads — runs fine for
>    tens of minutes.
> 2. Frame fences start timing out (`VK_TIMEOUT` on `vkWaitForFences`).
>    Frozen frame.
> 3. Keyboard stops responding, then mouse, then black screen.
> 4. Kernel panic and reboot ~2 minutes after the first fence timeout.
>
> Cause appears to be: the patch lets the engine *think* it can
> allocate millions of descriptors per set; the engine takes us up on
> it; Metal can technically back the arg-buffer allocations but at
> rates that completely starve WindowServer of GPU time. Lowering the
> patch values to "conservative" (131,072/4,096) didn't avert the
> panic on the second test.
>
> See [`docs/EVIDENCE.md`](docs/EVIDENCE.md#kernel-panic-2026-05-07)
> for the actual panic log and the planned next steps. **Until that
> is resolved, treat this repo as a diagnosis writeup, not a
> turn-key fix.**

## TL;DR

s&box (Source 2, Vulkan-native) ships with a launch gate that requires
`maxDescriptorSetSampledImages >= 65536` and `maxDescriptorSetSamplers
>= 2048`. Stock MoltenVK on Apple Silicon reports **1,280 / 80** for
those limits — the legacy Metal argument-table values — even though
modern Apple GPUs with argument buffers can back millions of
descriptors. Patch MoltenVK to expose the real numbers, drop the
patched dylib into a wine prefix, and the game launches.

| Limit | Vanilla MoltenVK 1.4.x | Patched | s&box minimum |
|---|---|---|---|
| `maxDescriptorSetSampledImages` | 1,280 | **1,000,000** | 65,536 |
| `maxDescriptorSetSamplers` | 80 | **~500,000** (= M-chip's `maxArgumentBufferSamplerCount`) | 2,048 |

Tested on M4 Pro / macOS 26.4 — see [`docs/EVIDENCE.md`](docs/EVIDENCE.md)
for actual probe output and `sbox.log` excerpts.

## What's in here

| Path | Purpose |
|---|---|
| [`moltenvk/descriptor-limits.patch`](moltenvk/descriptor-limits.patch) | The actual patch (~15 lines) against `MoltenVK/MoltenVK/GPUObjects/MVKDevice.mm`. Authored against [KhronosGroup/MoltenVK@dd34067](https://github.com/KhronosGroup/MoltenVK/commit/dd34067) (post-1.4.1 main). |
| [`wine-wrapper/`](wine-wrapper/) | Mach-O C wrapper that lets you invoke a Wineskin/Sikarugir/Kegworks-derivative wine binary against a custom prefix with a custom MoltenVK on the dyld path. Solves the SIP-strips-DYLD-vars problem and the winetricks `lipo -archs` arch-detection problem. |
| [`probe/`](probe/) | Tiny C program that links MoltenVK and prints the descriptor limits. Use it to verify the patch took effect. |
| [`launcher/sbox-launcher.sh`](launcher/sbox-launcher.sh) | Bash launcher template. Configures all the MVK env vars, loops on Steam's exit-code-42-restart-me signal, can be wrapped in a `.app` bundle for a Dock icon. |
| [`docs/DIAGNOSIS.md`](docs/DIAGNOSIS.md) | Full root-cause writeup. Read this if you want to understand *why* MoltenVK reports legacy limits and how the patch is consistent with the arg-buffer numbers MoltenVK already trusts internally. |
| [`docs/BUILD.md`](docs/BUILD.md) | End-to-end setup guide: build patched MoltenVK, build the wrapper, set up an isolated wine prefix, install Steam, install s&box. |
| [`docs/EVIDENCE.md`](docs/EVIDENCE.md) | Probe output before vs after the patch; the exact `sbox.log` lines that prove a passing launch. |

## Quick start

If you already have a Wineskin-derived wine bundle (Sikarugir, Kegworks, etc.):

```sh
# 1. Build patched MoltenVK
git clone https://github.com/KhronosGroup/MoltenVK.git
cd MoltenVK
git apply ../sbox-macos/moltenvk/descriptor-limits.patch
./fetchDependencies --macos
make macos
mkdir -p ~/sbox-rollyourown/moltenvk
cp Package/Release/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib ~/sbox-rollyourown/moltenvk/

# 2. Build the wine wrapper
cd ../sbox-macos/wine-wrapper
make
PREFIX=~/sbox-rollyourown/bin make install

# 3. Verify the patch took effect
cd ../probe
ln -s ~/sbox-rollyourown/moltenvk/libMoltenVK.dylib libMoltenVK.dylib
ln -s ../MoltenVK/MoltenVK/MoltenVK/include headers
make VULKAN_HEADERS_DIR=headers
MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=1 ./probe
# expect maxDescriptorSetSampledImages: 1000000+
```

For the rest (initialize prefix, install Steam + winetricks deps,
launch s&box), see [`docs/BUILD.md`](docs/BUILD.md).

## Why this exists

s&box has been shipping a Vulkan-1.0-without-`descriptor-indexing`
renderer for a year+. MoltenVK has been correctly reporting *legacy*
descriptor-table limits for that whole time. The two facts are
consistent — Vulkan 1.0 spec leaves it to drivers what to report —
but the practical effect is that s&box won't launch on any MacBook,
ever, until someone (a) patches MoltenVK, (b) Facepunch enables
descriptor-indexing, or (c) Apple ships a Metal-native Source 2
renderer.

(a) is small and tractable. This repo is (a).

## What this isn't

- **Not a fork of MoltenVK.** Just a patch. Apply it on top of upstream.
  When upstream eventually surfaces these limits in the base properties
  themselves, this repo becomes obsolete and that's fine.
- **Not a wine distribution.** You bring your own (Sikarugir, Whisky,
  Kegworks, CrossOver — anything that gives you a working wine binary
  on Apple Silicon). The wrapper just lets you redirect MoltenVK
  without modifying the bundle.
- **Not a Steam shortcut.** Standard Steam Windows client running in
  wine. The launcher just sets the right env vars before invoking it.

## Status

Working as of 2026-05-07 on M4 Pro / macOS 26.4 with Sikarugir
(Wineskin fork) providing wine 10.0. Should work with any
Wineskin-lineage wine bundle that supports a custom prefix +
MoltenVK swap-in.

If it doesn't work for you: open an issue with your Mac chip,
macOS version, wine bundle source, and the output of running the
probe in [`probe/`](probe/) against both vanilla and patched
MoltenVK dylibs.

## License

MIT. See [LICENSE](LICENSE).

The MoltenVK patch itself is offered for upstream consideration —
KhronosGroup is welcome to incorporate the change directly under
MoltenVK's existing Apache 2.0 license.

## Credits

Diagnostic + patch + writeup by [@ross631](https://github.com/ross631)
with substantial pairing assistance from Claude (Anthropic).

Built on top of:
- [KhronosGroup/MoltenVK](https://github.com/KhronosGroup/MoltenVK) — the Vulkan-on-Metal layer this all depends on
- [Sikarugir](https://github.com/Sikarugir-App/Sikarugir) / Wineskin lineage — the wine bundles this works alongside
- [Facepunch / s&box](https://sbox.game) — the game that finally made me chase this all the way down
