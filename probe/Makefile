# Build a Vulkan probe linked against a libMoltenVK.dylib in the same
# directory. Reports the descriptor-set limits relevant to s&box's launch
# gate. Run with our patched dylib in place to confirm the limits moved.
#
# Usage:
#   1. Drop libMoltenVK.dylib + Vulkan headers into this dir, or pass
#      paths via MVK_DIR / VULKAN_HEADERS_DIR.
#   2. make
#   3. ./probe              # uses ./libMoltenVK.dylib via @loader_path
#
# Toggle MVK_CONFIG_USE_METAL_ARGUMENT_BUFFERS=1/0 to see the difference.

MVK_DIR              ?= .
VULKAN_HEADERS_DIR   ?= ./MoltenVK/MoltenVK/include
CFLAGS               ?= -O2 -Wall -arch x86_64 -arch arm64
FRAMEWORKS           = -framework Metal -framework Foundation -framework QuartzCore -framework IOKit -framework IOSurface

probe: probe.c
	$(CC) $(CFLAGS) -I$(VULKAN_HEADERS_DIR) probe.c -o probe \
	    -L$(MVK_DIR) -lMoltenVK -Wl,-rpath,@loader_path $(FRAMEWORKS)

clean:
	rm -f probe

.PHONY: clean
