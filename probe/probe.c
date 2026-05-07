// Probe MoltenVK: enumerate physical devices and print the two descriptor-set
// limits s&box gates on. Compile with the Vulkan headers + dylib that ship in
// the MoltenVK release tarball.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_USE_PLATFORM_MACOS_MVK
#include <vulkan/vulkan.h>

int main(void) {
    VkApplicationInfo app = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "mvk-probe",
        .apiVersion = VK_API_VERSION_1_2,
    };
    VkInstanceCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app,
    };
    VkInstance inst;
    if (vkCreateInstance(&ici, NULL, &inst) != VK_SUCCESS) {
        fprintf(stderr, "vkCreateInstance failed\n");
        return 1;
    }
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst, &n, NULL);
    if (!n) { fprintf(stderr, "no physical devices\n"); return 2; }
    VkPhysicalDevice *devs = calloc(n, sizeof(*devs));
    vkEnumeratePhysicalDevices(inst, &n, devs);
    for (uint32_t i = 0; i < n; i++) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(devs[i], &p);
        printf("device[%u] %s\n", i, p.deviceName);
        printf("  apiVersion: %u.%u.%u\n",
               VK_VERSION_MAJOR(p.apiVersion),
               VK_VERSION_MINOR(p.apiVersion),
               VK_VERSION_PATCH(p.apiVersion));
        printf("  maxDescriptorSetSampledImages: %u (sbox needs 65536)\n",
               p.limits.maxDescriptorSetSampledImages);
        printf("  maxDescriptorSetSamplers:      %u (sbox needs 2048)\n",
               p.limits.maxDescriptorSetSamplers);
        printf("  maxDescriptorSetStorageImages: %u\n",
               p.limits.maxDescriptorSetStorageImages);
        printf("  maxPerStageDescriptorSampledImages: %u\n",
               p.limits.maxPerStageDescriptorSampledImages);
    }
    free(devs);
    vkDestroyInstance(inst, NULL);
    return 0;
}
