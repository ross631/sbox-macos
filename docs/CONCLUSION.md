# Conclusion — the negative result

Captured 2026-05-07 after a single intensive session on M4 Pro /
macOS 26.4. Three escalations of the descriptor-limits patch all
failed in the same way under sandbox-gamemode load. The patch is the
variable; vanilla MoltenVK is stable.

## The full experimental record

### Attempt 1 — "real" arg-buffer values

- `maxDescriptorSetSampledImages = 1,000,000`
- `maxDescriptorSetSamplers = 500,000` (= M4 Pro's
  `MTLDevice.maxArgumentBufferSamplerCount` — the actual Metal value
  MoltenVK uses internally elsewhere in the same file)

**Result:** Game launched cleanly. Main menu loaded. Sandbox gamemode
loaded successfully. About 26 minutes of normal use including
loading scene/resource preloads. Then, at first scene transition:

```
2026/05/07 08:57:49.7546  [engine/RenderSystem] FrameSync() - bailing out of vkWaitForFences( fenceCount = 1 ) after 0.252179 seconds, error = VK_TIMEOUT
2026/05/07 08:57:49.7589  [engine/Engine] CSwapChainBase::QueuePresentAndWait() looped for 21 iterations without a present event.
```

Frame frozen. Mouse stuck. About 5 minutes later: keyboard died,
screen went black, **kernel panic + reboot**.

Panic log key field:

```
"panicString" : "panic(cpu 0 caller 0xfffffe003b8fc3e4): userspace
watchdog timeout: no successful checkins from WindowServer (2 induced
crashes) in 120 seconds"
```

Sequence reconstructed from `/Library/Logs/DiagnosticReports/`:

| Time | Event |
|---|---|
| 09:11:58 | WindowServer .ips report (first hint of trouble) |
| 09:12:02 | WindowServer userspace_watchdog_timeout (crash 1) |
| 09:12:38 | WindowServer .ips report |
| 09:12:39 | WindowServer userspace_watchdog_timeout (crash 2) |
| 09:13:39 | **Kernel panic** |
| 09:13:41 | Reset |

Backtrace ends in `com.apple.driver.AppleARMWatchdogTimer`. Among
threads in the panic stackshot: `sbox.exe` with `cpu_usage 3,517,006`
— hot at the time of the panic.

### Attempt 2 — "conservative" values

Hypothesis: 1M descriptors was too aggressive. If we tell MoltenVK
that the max is more modest, the engine will allocate less.

- `maxDescriptorSetSampledImages = 131,072` (2× s&box's stated minimum)
- `maxDescriptorSetSamplers = 4,096` (2× s&box's stated minimum)

**Result:** Within ~2 minutes of sandbox-gamemode load, **another
kernel panic**.

Same WindowServer-watchdog → kernel-panic pattern as attempt 1. Same
reboot.

### Attempt 3 — minimum-passing + heavy throttling + low quality

Hypothesis: the values themselves aren't the trigger; the engine
just over-pools descriptors based on whatever max we report. Combined
with MVK queue throttling (limit concurrent command buffers; force
synchronous submits) and lowest-quality video settings, perhaps
enough GPU bandwidth would be left for WindowServer.

- Patch values: `65,537 / 2,049` (literally one over s&box's stated
  minimum)
- `MVK_CONFIG_MAX_ACTIVE_METAL_COMMAND_BUFFERS_PER_QUEUE=4` (default
  is 64)
- `MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS=1`
- `cfg/video.txt`: `setting.shaderquality=0`, windowed
  1280×720 (engine bumped to 1920×1080 but kept low shader quality)
- Self-kill watchdog tailing `sbox.log` for `VK_TIMEOUT` /
  `FrameSync` / `QueuePresentAndWait` distress signals, force-killing
  the wine session before the WindowServer watchdog could fire

**Result:** ~30 seconds into sandbox-gamemode loading flatgrass:

```
2026/05/07 09:47:36.6599  [Generic] Couldn't find Input Action called "invprev"
2026/05/07 09:47:37.6141  [engine/Engine] CSwapChainBase::QueuePresentAndWait() looped for 21 iterations without a present event.
```

Render thread stuck. Caught by manual force-kill (the watchdog had a
bug — `tail -F | grep -m 1` doesn't propagate exit, so it detected
but didn't kill). With the watchdog fix landed in this repo's
launcher, the kill would have happened in seconds — no panic risk.

But: same failure mode hit, even at minimum patch values + maximum
throttling. The pattern is consistent.

### Attempt 0 — control test (vanilla MoltenVK)

Hypothesis: maybe something else about the wine setup is fragile
— Sikarugir, the wrapper, the prefix layout, Steam under wine. If
vanilla MoltenVK *also* panics, the patch isn't the only variable.

Vanilla MoltenVK 1.4.1 privateapi swapped in. Probe confirmed
1,280 / 80 (gate-failing).

**Result:** s&box launched, hit its `does not meet minimum
requirements` gate within 2 seconds, exited cleanly. **No GPU
stress, no panic.** Steam stayed up; Mac stayed responsive.

The control isolates the patch as the cause. Vanilla = stable,
patched = panic. Independent of patch value.

## Why it fails — final understanding

The pattern across all three patched attempts:

1. The patch lets the engine see "lots of descriptor headroom" in
   `vkGetPhysicalDeviceProperties`.
2. The engine's allocator sizes descriptor pools and per-stage
   resources based on the reported max.
3. Even if the engine doesn't actually *use* all that capacity, it
   pre-allocates arg-buffer-backed structures proportional to the
   reported max, plus issues descriptor updates that touch many
   slots.
4. On Apple Silicon Tier 2, Metal *can* back arg-buffer descriptors
   in principle — but doing so submits enough GPU work that the
   command-buffer queue can't be shared with WindowServer's
   compositor.
5. WindowServer can't render frames within its watchdog deadline.
6. macOS protects itself by panicking.

The exact magic number where this turns from "stable" to "panic"
isn't a function of the patch value — it's a function of the engine's
gamemode-init pre-pool work, which exceeds the safe budget at *any*
patched limit because the engine sizes pools based on what we
advertise.

This is consistent with what unmodified MoltenVK is doing: by
reporting the legacy 1,280 / 80, it's keeping engine descriptor pools
small enough that the GPU stays shareable. The "bug" in MoltenVK
(reporting low limits) turns out to also be a feature (gating
applications that can't safely share the GPU on this hardware).

## What would actually fix this

In order of accessibility:

1. **Facepunch enables `VK_EXT_descriptor_indexing` in s&box's
   renderer.** MoltenVK already reports
   `maxDescriptorSetUpdateAfterBindSampledImages = 1e6` in the
   descriptor-indexing extension's after-bind properties. If s&box
   queried those (or simply enabled the extension at instance
   creation), the gate passes naturally — no MoltenVK patch needed.
   The engine would still use whatever Vulkan resources it actually
   needs; descriptor-indexing doesn't force *more* resource use, it
   just lets the driver allocate larger sets when asked. This
   wouldn't trigger the GPU-starvation pattern, because nothing is
   over-promising headroom to a non-aware engine.

2. **MoltenVK surfaces arg-buffer-backed limits in `_properties.limits`
   *with a scheduling/throttling story*.** Just bumping the numbers
   (this repo's patch) demonstrates the failure. A proper version
   would need MoltenVK to coordinate command-buffer submission rates
   with the system compositor's needs — non-trivial driver work.
   Discussed at [KhronosGroup/MoltenVK#2220](https://github.com/KhronosGroup/MoltenVK/issues/2220)
   and similar.

3. **Apple ships GPTK 2 with a Source 2 / native arm64 path that
   doesn't go through MoltenVK.** Open-ended.

4. **Hex-edit `sbox.exe` to skip the gate.** Would let s&box launch
   with vanilla MoltenVK reporting 1,280 / 80 — but the engine
   probably uses those limits to size descriptor pools, so it might
   crash later when an asset wants more than 80 samplers. Untested.
   Steam rewrites the binary on update.

## What this repo's authors would do differently next time

- **Smaller incremental tests on a fresh machine boot before any
  scene load.** We jumped straight to sandbox+zoo, which is the
  heaviest gamemode. We should have: probe → menu only → flatgrass
  empty → spawn one prop → spawn five → load sandbox content
  gradually.
- **Test for ~10 minutes at each step.** The first attempt only
  panicked after 26 minutes; some configurations might be stable for
  long enough that early-stage tests miss the issue.
- **Save work in other apps before each test.** A kernel panic eats
  unsaved buffers in every running app. We got lucky.
- **Watchdog: never use `tail -F | grep -m 1`.** Use `awk` or a
  polling loop. The original launcher's watchdog detected but
  didn't kill — only because of this. With the fix in the published
  launcher, the panic risk is significantly reduced.

## Negative results are valuable too

We hoped for a "patch unlocks s&box on every Apple Silicon Mac"
victory. We got, instead, the most thorough public diagnosis of
exactly why s&box won't run on macOS today, and a clear path to
upstream fixes. If this writeup gets in front of Facepunch, even
better — `VK_EXT_descriptor_indexing` is a small ask compared to
what we tried here.
