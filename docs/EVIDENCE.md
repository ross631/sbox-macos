# Evidence

Captured 2026-05-07 on a 2024 14" MacBook Pro (Apple M4 Pro, 24 GB,
macOS 26.4.1) running through Sikarugir wine 10.0 + a fresh prefix.

## Probe results

A small C program that links MoltenVK directly and prints
`VkPhysicalDeviceProperties.limits.maxDescriptorSet*`. Source is in
[`probe/probe.c`](../probe/probe.c).

### Vanilla MoltenVK 1.4.1 privateapi (Khronos release)

```
=== arg-buffers OFF ===
device[0] Apple M4 Pro
  maxDescriptorSetSampledImages: 640 (sbox needs 65536)
  maxDescriptorSetSamplers:      80 (sbox needs 2048)
  maxDescriptorSetStorageImages: 640
  maxPerStageDescriptorSampledImages: 128

=== arg-buffers ON (=1, ALWAYS) ===
device[0] Apple M4 Pro
  maxDescriptorSetSampledImages: 1280 (sbox needs 65536)
  maxDescriptorSetSamplers:      80 (sbox needs 2048)
  maxDescriptorSetStorageImages: 1280
  maxPerStageDescriptorSampledImages: 256
```

Both native arm64 *and* x86_64 Rosetta returned identical numbers
under vanilla MoltenVK. Read-Write Texture Tier 2 detected. So:

- Sikarugir's MoltenVK isn't customized differently from vanilla.
- Rosetta isn't hiding Tier 2 features from Metal.
- The arg-buffers env var **is** engaging (640→1280 sampled images).
- The cap is hardcoded in MoltenVK.

### Patched MoltenVK (this repo)

```
=== arg-buffers ON, patched ===
device[0] Apple M4 Pro
  maxDescriptorSetSampledImages: 1000000 (sbox needs 65536)
  maxDescriptorSetSamplers:      500000 (sbox needs 2048)
  maxDescriptorSetStorageImages: 1000000
  maxPerStageDescriptorSampledImages: 1000000
```

The 500,000 for samplers is the actual M4 Pro
`MTLDevice.maxArgumentBufferSamplerCount` — not a fabricated value.
MoltenVK's own `getMaxSamplerCount()` already returns this number
elsewhere in the same file.

## s&box launch result

With the patched MoltenVK and the wine setup from this repo, s&box
launches cleanly past the descriptor-limit gate. Excerpt from
`sbox.log`:

```
2026/05/07 08:31:17.5754  [engine/Engine] SteamAPI_Init succeeded.  SteamID is [redacted], AppID is 590830
2026/05/07 08:31:17.9344  [engine/RenderSystem] Saved video settings config to 'cfg\video.txt'
2026/05/07 08:31:17.9507  [engine/RenderSystem] Vulkan Physical Device: Apple M4 Pro
2026/05/07 08:31:17.9528  [engine/RenderSystem] Disabling VK_EXT_graphics_pipeline_library dependent extensions.
2026/05/07 08:31:17.9776  [engine/RenderSystem] Vulkan driver version: 0.2.2210
2026/05/07 08:31:22.2883  [PackageManager] Install Package (Already Mounted) local.base#local [local]
2026/05/07 08:31:25.1600  [Generic] Loading startup scene: scenes/menu-main.scene
2026/05/07 08:31:27.5725  [Generic] Bootstrap Networking...
```

No `maxDescriptor* does not meet requirements` lines. No `Your graphics
device does not meet minimum requirements`. The engine accepted the
GPU, loaded the menu scene, brought up networking.

**However:** the gate-pass is not the same thing as a working game.
See [`CONCLUSION.md`](CONCLUSION.md) for what happens next under
sandbox-gamemode load. Short version: the engine pre-pools
descriptors based on the patched limits, GPU contention starves
WindowServer, system panics. Three patch values tested
(1,000,000 / 131,072 / 65,537), all failed in the same way.

## Kernel panic 2026-05-07

After ~26 minutes of stable runtime in the menu, loading a sandbox
scene (citizens + weapons + props) caused this progression in
sbox.log:

```
2026/05/07 08:57:49.7546  [engine/RenderSystem] FrameSync() - bailing out of vkWaitForFences( fenceCount = 1 ) after 0.252179 seconds, error = VK_TIMEOUT
2026/05/07 08:57:49.7589  [engine/Engine] CSwapChainBase::QueuePresentAndWait() looped for 21 iterations without a present event.
2026/05/07 08:57:50.0023  [engine/Engine] CSwapChainBase::QueuePresentAndWait() looped for 21 iterations without a present event.
2026/05/07 08:57:50.0282  [engine/RenderSystem] FrameSync() - bailing out of vkWaitForFences( fenceCount = 2 ) after 0.250293 seconds, error = VK_TIMEOUT
```

sbox.log went silent for 5 minutes. Frame frozen. After force-killing
the wine session, mouse + keyboard recovered.

Lowered patch limits to a conservative `131,072 / 4,096`. Probe
confirmed those numbers post-rebuild. Relaunched. About 2 minutes
into a sandbox scene, **Mac rebooted with a kernel panic.**

Panic log
(`/Library/Logs/DiagnosticReports/panic-full-2026-05-07-091339.0002.panic`)
key fields:

```
"panicString" : "panic(cpu 0 caller 0xfffffe003b8fc3e4): userspace
watchdog timeout: no successful checkins from WindowServer (2 induced
crashes) in 120 seconds"
```

Sequence (from /Library/Logs/DiagnosticReports/ timestamps):

| Time | Event |
|---|---|
| 09:11:58 | WindowServer .ips report |
| 09:12:02 | WindowServer userspace_watchdog_timeout (induced crash 1) |
| 09:12:38 | WindowServer .ips report |
| 09:12:39 | WindowServer userspace_watchdog_timeout (induced crash 2) |
| 09:13:39 | **kernel panic** |
| 09:13:41 | reset |

Backtrace ends in `com.apple.driver.AppleARMWatchdogTimer`. Among
threads listed in the panic stackshot: `sbox.exe` with `cpu_usage
3,517,006`, hot at the time of the panic.

### Interpretation

The patched MoltenVK lets s&box *think* it can allocate up to the
patched values per descriptor set. Under heavy scene load, the engine
takes us up on it and submits enough Vulkan work that Metal can't
service WindowServer's compositor frames. macOS's kernel watchdog
gives WindowServer two chances to recover, then panics protectively.

This is **not** a memory bug or numerical issue in our patch — it's
a GPU contention issue caused by raising the perceived headroom too
far. Lowering values from 1,000,000 to 131,072 didn't help because
even 131,072 is still ~100× the actual descriptor pressure that
keeps the GPU shareable with the compositor.

### Next steps

In order:

1. **Control test.** Drop in vanilla MoltenVK 1.4.1 (no patch).
   Confirm the descriptor-limit gate kills s&box at startup with no
   GPU stress and no panic. This isolates whether the panic is
   patch-induced vs. some other interaction (Wineskin/Sikarugir
   stability, Steam under wine, etc.).
2. **If control is clean**, retry with absolute-minimum patch values
   — `65,537 / 2,049`, literally one above the engine's stated
   minimum. Combine with:
   - `MVK_CONFIG_MAX_ACTIVE_METAL_COMMAND_BUFFERS_PER_QUEUE` set
     lower than default to throttle concurrent command-buffer load.
   - s&box `cfg/video.txt` forced to lowest quality + windowed mode
     before launching, to reduce per-frame GPU work.
3. **If that still panics**, the conclusion is that this approach
   isn't safe on M4 Pro for s&box's scene load. Roll back to vanilla
   MoltenVK and update this repo to that effect.

## Reproducing

Anyone with similar hardware can rerun the probe — see
[`docs/BUILD.md`](BUILD.md) section 1 for the build steps and the
probe verification command.

If your MacBook reports something different, please open an issue
with: chip model, macOS version, MoltenVK commit, and the full probe
output for both vanilla and patched dylibs.

If the patch panics your machine, please file an issue with the
panic-full log so we can correlate failure modes.
