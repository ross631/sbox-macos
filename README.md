# sbox-macos

> ## ❌ This approach does not work on M4 Pro
>
> After three iterations (descriptor limits at 1,000,000 / 131,072 / 65,537)
> with progressively more conservative MoltenVK throttling and
> lowest-quality s&box settings, **every patched configuration triggered
> the same GPU-contention failure mode** when s&box loaded the sandbox
> gamemode. The first attempt escalated into a macOS kernel panic +
> reboot. Subsequent attempts hung on the present-event loop within
> seconds of scene load.
>
> **Vanilla MoltenVK fails s&box's launch gate cleanly with no GPU
> stress and no panic risk.** The patched MoltenVK clears the gate but
> lets the engine submit Vulkan work that Metal can't service while
> keeping the macOS compositor (WindowServer) alive.
>
> This repo is now a **negative-result diagnosis writeup**, not a
> turn-key fix. Read it to understand exactly why s&box doesn't run on
> Apple Silicon today and what would actually fix it upstream. **Don't
> apply the patch and expect to play.**
>
> See [`docs/CONCLUSION.md`](docs/CONCLUSION.md) for the full
> experimental record and what would need to change for s&box to run
> properly.

## What we tried, what happened

s&box (Source 2, Vulkan-native) ships with a launch gate that requires
`maxDescriptorSetSampledImages >= 65536` and `maxDescriptorSetSamplers
>= 2048`. Stock MoltenVK on Apple Silicon reports **1,280 / 80** for
those limits — the legacy Metal argument-table values — even though
modern Apple GPUs with argument buffers can back millions of
descriptors. The intuition: patch MoltenVK to expose the real numbers,
the gate passes, the game runs.

The intuition was half right. The gate did pass. The game did launch
to its main menu. **But under any non-trivial scene load, the engine
allocated enough arg-buffer-backed descriptors that Metal couldn't
service WindowServer's compositor frames — and macOS's userspace
watchdog protected itself by panicking the system.**

Three patch values were tested. All three failed:

| Patch reports | Game | Result |
|---|---|---|
| `1,000,000 / ~500,000` (M-chip's actual `maxArgumentBufferSamplerCount`) | Sandbox + zoo | **Kernel panic + reboot** ~2 min after first `VK_TIMEOUT` |
| `131,072 / 4,096` ("conservative") | Sandbox + zoo | **Kernel panic + reboot** within 2 min of scene load |
| `65,537 / 2,049` (literally minimum-passing) + MVK queue throttling + low-quality video config | Sandbox + flatgrass | Render thread stalled at `QueuePresentAndWait()` within 30 s of gamemode init. Caught manually before WindowServer watchdog fired. |

**The patch is the variable.** With vanilla MoltenVK, sbox.exe quits
at the gate within 2 seconds of launch — no GPU stress, no panic,
just no game. Every patched configuration produces GPU contention.

Even at *one descriptor over* the engine's stated minimum, the engine
allocated enough that the GPU couldn't be shared with the compositor.

## What this repo still has of value

This is the most thorough public writeup we know of explaining *why*
s&box doesn't run on macOS Apple Silicon. Specifically:

| Path | Purpose |
|---|---|
| [`docs/DIAGNOSIS.md`](docs/DIAGNOSIS.md) | Root-cause analysis: where in MoltenVK's source the legacy limits are reported, why arg-buffers don't help, why the descriptor-indexing-extension high values aren't surfaced in the base properties. |
| [`docs/CONCLUSION.md`](docs/CONCLUSION.md) | The full experimental record: every patch value tested, every observation, the kernel panic log, why each escalation didn't help, and what *would* fix this upstream. |
| [`docs/EVIDENCE.md`](docs/EVIDENCE.md) | Probe output for vanilla and each patched dylib, sbox.log excerpts at every failure mode, panic log header. |
| [`moltenvk/descriptor-limits.patch`](moltenvk/descriptor-limits.patch) | The patch itself. Triggers the failure mode described above. Useful only as a starting point for a properly-engineered fix that throttles or schedules around Metal's actual capacity. |
| [`probe/`](probe/) | Standalone probe to read your machine's MoltenVK descriptor limits. Useful regardless of whether you're trying to run s&box. |
| [`wine-wrapper/`](wine-wrapper/) | Mach-O C wrapper for invoking a Wineskin/Sikarugir/Kegworks-derivative wine binary against a custom prefix with a custom MoltenVK on the dyld path. Solves the SIP-strips-DYLD-vars problem and the winetricks `lipo -archs` arch-detection problem. Useful for anyone wanting to swap MoltenVK in any Wineskin-lineage setup. |
| [`launcher/sbox-launcher.sh`](launcher/sbox-launcher.sh) | Bash launcher template with a self-kill watchdog that catches `VK_TIMEOUT` / `QueuePresentAndWait` distress signals before macOS does. Reduces panic risk if you do experiment with the patch. |

## What would actually fix this

In rough order of accessibility:

1. **Facepunch enables `VK_EXT_descriptor_indexing` in s&box.** MoltenVK
   already reports `maxDescriptorSetUpdateAfterBindSampledImages = 1e6`
   in the descriptor-indexing extension's after-bind properties. If
   s&box queried those (or just enabled the extension), the gate
   passes naturally without any MoltenVK patch. This is small, in
   their codebase, and would unblock every Mac player.

2. **MoltenVK upstream surfaces realistic arg-buffer-backed limits in
   the *base* `_properties.limits` when arg-buffers are configured.**
   Discussed in [KhronosGroup/MoltenVK#2220](https://github.com/KhronosGroup/MoltenVK/issues/2220)
   and similar issues. Would need careful pacing/scheduling on
   MoltenVK's end to avoid the GPU-starvation pattern this repo
   demonstrates exists.

3. **Apple ships an arm64-native Source 2 backend or further GPTK
   Metal-renderer support.** Open-ended.

4. **Hex-edit `sbox.exe` to skip the gate check.** Would let the
   engine launch with vanilla MoltenVK reporting 1,280/80 — but the
   engine probably *uses* those limits to size descriptor pools, so
   it might just crash later. Untested. Steam will rewrite the binary
   on update anyway. Mentioned for completeness.

## Status

**Doesn't work as of 2026-05-07 on M4 Pro / macOS 26.4 with Sikarugir
(Wineskin fork) providing wine 10.0.**

If you find a configuration that does work — particularly on different
Apple Silicon (M1, M2 Max, M3, M4 Max), or with a different MVK queue
configuration that actually shares the GPU with the compositor —
please file an issue and tell us. We'd love to be wrong.

If you experiment with the patch despite the warning above, please
file an issue with your panic-full log if it bites you, so others
can correlate failure modes.

## License

MIT. See [LICENSE](LICENSE).

The MoltenVK patch itself is offered for upstream consideration —
KhronosGroup is welcome to incorporate the approach (or a properly
scheduled version of it) under MoltenVK's existing Apache 2.0
license.

## Credits

Diagnostic + experimental record by [@ross631](https://github.com/ross631)
with pairing assistance from Claude (Anthropic).

Built on top of:
- [KhronosGroup/MoltenVK](https://github.com/KhronosGroup/MoltenVK)
- [Sikarugir](https://github.com/Sikarugir-App/Sikarugir) / Wineskin lineage
- [Facepunch / s&box](https://sbox.game)
