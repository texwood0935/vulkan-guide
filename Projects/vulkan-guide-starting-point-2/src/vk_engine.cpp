//> includes
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_initializers.h>
#include <vk_types.h>

#include <chrono>
#include <iostream>
#include <thread>

#include "VkBootstrap.h"

enum QueueType
{
    Graphics
};

struct QueueFamilyIndices
{
    std::optional<uint32_t> QueueIndex;

    bool IsComplete() { return QueueIndex.has_value(); }
};

VulkanEngine* loadedEngine = nullptr;

VulkanEngine& VulkanEngine::Get() { return *loadedEngine; }
void VulkanEngine::init()
{
    // only one engine initialization is allowed with the application.
    assert(loadedEngine == nullptr);
    loadedEngine = this;

    // We initialize SDL and create a window with it.
    SDL_Init(SDL_INIT_VIDEO);

    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_VULKAN);

    _window = SDL_CreateWindow(
        "Vulkan Engine",
        SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED,
        _windowExtent.width,
        _windowExtent.height,
        window_flags);

    //----------------
    InitVulkan();
    //----------------
    
    // everything went fine
    _isInitialized = true;
}

void VulkanEngine::cleanup()
{
    if (_isInitialized) {

        SDL_DestroyWindow(_window);
        vkDestroyDevice(Device, nullptr);
        vkDestroyInstance(VkIns, nullptr);
    }

    // clear engine pointer
    loadedEngine = nullptr;
}

void VulkanEngine::draw()
{
    // nothing yet
}

void VulkanEngine::run()
{
    SDL_Event e;
    bool bQuit = false;

    // main loop
    while (!bQuit) {
        // Handle events on queue
        while (SDL_PollEvent(&e) != 0) {
            // close the window when user alt-f4s or clicks the X button
            if (e.type == SDL_QUIT)
                bQuit = true;

            if (e.type == SDL_WINDOWEVENT) {
                if (e.window.event == SDL_WINDOWEVENT_MINIMIZED) {
                    stop_rendering = true;
                }
                if (e.window.event == SDL_WINDOWEVENT_RESTORED) {
                    stop_rendering = false;
                }
            }
        }

        // do not draw if we are minimized
        if (stop_rendering) {
            // throttle the speed to avoid the endless spinning
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        draw();
    }
}
//-------------------------
void VulkanEngine::InitVulkan()
{
    CreateInstance();
    PickupPhysicalDevice();
    CreateLogicalDevice();
}

void VulkanEngine::CreateInstance()
{
    VkApplicationInfo AppInfo = {};
    AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    AppInfo.pApplicationName = "First Vulkan Engine";
    AppInfo.applicationVersion = VK_MAKE_VERSION(1,0,0);
    AppInfo.pEngineName = "Vulkan Engine";
    AppInfo.engineVersion = VK_MAKE_VERSION(1,0,0);
    AppInfo.apiVersion = VK_API_VERSION_1_3;
    AppInfo.pNext = nullptr;

    VkInstanceCreateInfo CreateInfo = {};
    CreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    CreateInfo.pApplicationInfo = &AppInfo;
    CreateInfo.pNext = nullptr;
    CreateInfo.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    
    std::vector<std::string> extensions;
    GetSupportExtensions(extensions);
    std::vector<const char*> exts;
    for(int32_t i=0;i<extensions.size();i++)
    {
        exts.push_back(extensions[i].data());
    }
    CreateInfo.enabledExtensionCount = exts.size();
    CreateInfo.ppEnabledExtensionNames = exts.data();
    
    std::vector<const char*> layers = {"VK_LAYER_KHRONOS_validation"};
    if(CheckSupportLayers())
    {
        CreateInfo.enabledLayerCount = 1;
        CreateInfo.ppEnabledLayerNames = layers.data();
    }
    VkResult result = vkCreateInstance(&CreateInfo, nullptr, &VkIns);
    if(result != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create vk instance!");
    }
}

uint32_t VulkanEngine::GetSupportExtensions(std::vector<std::string>& extensions)
{
    VkResult result = VK_INCOMPLETE;
    uint32_t count = 0;

    if(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr) != VK_SUCCESS)
    {
        return 0;
    }
    std::vector<VkExtensionProperties> extensionProperties(count);
    
    result = vkEnumerateInstanceExtensionProperties(nullptr, &count, extensionProperties.data());
    if(result != VK_SUCCESS)
    {
        return 0;
    }
    for(auto& extension:extensionProperties)
    {
        extensions.push_back(extension.extensionName);
    }
    return count;
}

bool VulkanEngine::CheckSupportLayers()
{
    VkResult result = VK_INCOMPLETE;
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> availableLayers(count);
    vkEnumerateInstanceLayerProperties(&count, availableLayers.data());
    for(auto& layer:availableLayers)
    {
        if(strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0)
        {
            return true;
        }
    }
    return false;
}

void VulkanEngine::PickupPhysicalDevice()
{
    uint32_t physicalDevicesCount = 0;
    VkResult result = vkEnumeratePhysicalDevices(VkIns, &physicalDevicesCount, nullptr);
    if(result == VK_SUCCESS)
    {
        std::cout << "vkEnumeratePhysicalDevices: " << physicalDevicesCount <<std::endl;
    }
    std::vector<VkPhysicalDevice> devices(physicalDevicesCount);
    vkEnumeratePhysicalDevices(VkIns, &physicalDevicesCount, devices.data());
    if(devices.size() > 0 )
        physicalDevice = devices[0];
}

void VulkanEngine::CreateLogicalDevice()
{
    QueueFamilyIndices indices = FindQueueFamily(QueueType::Graphics);
    if(!indices.IsComplete())
    {
        throw std::runtime_error("failed to create Graphics Queue!");
    }

    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = indices.QueueIndex.value();
    queueCreateInfo.queueCount = 1;
    float queuePriority = 1.0f;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    std::vector<std::string> extensions;
    //GetSupportExtensions(extensions);
    std::vector<const char*> exts;
    //for(int32_t i=0;i<extensions.size();i++)
    //{
    //    exts.push_back(extensions[i].data());
    //}
    exts.push_back("VK_KHR_swapchain");
    createInfo.enabledExtensionCount = exts.size();
    createInfo.ppEnabledExtensionNames = exts.data();
    
    std::vector<const char*> layers = {"VK_LAYER_KHRONOS_validation"};
    if(CheckSupportLayers())
    {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = layers.data();
    }
    VkResult result = vkCreateDevice(physicalDevice, &createInfo, nullptr, &Device);
    if(result != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create vk logic device!");
    }
    vkGetDeviceQueue(Device, indices.QueueIndex.value(), 0, &graphicsQueue);
}

QueueFamilyIndices VulkanEngine::FindQueueFamily(QueueType Type)
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
    QueueFamilyIndices result;
    uint32_t i=0;
    for(const auto& queueFamily : queueFamilies)
    {
        switch (Type)
        {
        case QueueType::Graphics:
            if(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                result.QueueIndex = i;
                break;
            }
        }
        i++;
    }
    return result;
}
