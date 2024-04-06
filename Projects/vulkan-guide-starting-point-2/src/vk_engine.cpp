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
    Graphics,
    Surface
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

        vkDestroySurfaceKHR(VkIns, surface, nullptr);
        DestroySwapChain();
        vkDestroyDevice(Device, nullptr);
        vkDestroyInstance(VkIns, nullptr);
        SDL_DestroyWindow(_window);
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
    CreateInstance_Vkb();
    //CreateInstance();
    //PickupPhysicalDevice();
    //CreateLogicalDevice();
    CreateSwapChain(_windowExtent.width, _windowExtent.height);
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
    
    std::vector<const char*> exts;
    std::vector<std::string> exts_strings;
    GetSupportExtensions(exts_strings);
    for(int32_t i=0;i<exts_strings.size();i++)
    {
        exts.push_back(exts_strings[i].data());
    }
    //GetSupportExtensions_SDL(exts);
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

void VulkanEngine::CreateInstance_Vkb()
{
    vkb::InstanceBuilder builder;

    //make the vulkan instance, with basic debug features
    auto inst_ret = builder.set_app_name("Example Vulkan Application")
        .request_validation_layers(true)
        .use_default_debug_messenger()
        .require_api_version(1, 3, 0)
        .build();

    vkb::Instance vkb_inst = inst_ret.value();

    //grab the instance 
    VkIns = vkb_inst.instance;
    _debug_messenger = vkb_inst.debug_messenger;

    SDL_Vulkan_CreateSurface(_window, VkIns, &surface);

    //vulkan 1.3 features
    VkPhysicalDeviceVulkan13Features features{};
    features.dynamicRendering = true;
    features.synchronization2 = true;

    //vulkan 1.2 features
    VkPhysicalDeviceVulkan12Features features12{};
    features12.bufferDeviceAddress = true;
    features12.descriptorIndexing = true;

    //use vkbootstrap to select a gpu. 
    //We want a gpu that can write to the SDL surface and supports vulkan 1.3 with the correct features
    vkb::PhysicalDeviceSelector selector{ vkb_inst };
    vkb::PhysicalDevice physDevice = selector
        .set_minimum_version(1, 3)
        .set_required_features_13(features)
        .set_required_features_12(features12)
        .set_surface(surface)
        .select()
        .value();


    //create the final vulkan device
    vkb::DeviceBuilder deviceBuilder{ physDevice };

    vkb::Device vkbDevice = deviceBuilder.build().value();

    // Get the VkDevice handle used in the rest of a vulkan application
    Device = vkbDevice.device;
    physicalDevice = physDevice.physical_device;

    FindQueueFamily(QueueType::Graphics);
    FindQueueFamily(QueueType::Surface);
    vkGetDeviceQueue(Device, indices.QueueIndex.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(Device, indices.PresentIndex.value(), 0, &presentQueue);
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

uint32_t VulkanEngine::GetSupportExtensions_SDL(std::vector<const char*>& extensions)
{
    uint32_t count = 0;
    SDL_Vulkan_GetInstanceExtensions(_window, &count, nullptr);
    extensions.resize(count);
    SDL_Vulkan_GetInstanceExtensions(_window, &count, extensions.data());
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
    SDL_Vulkan_CreateSurface(_window, VkIns, &surface);
    
    FindQueueFamily(QueueType::Graphics);
    FindQueueFamily(QueueType::Surface);
    if(!indices.IsComplete())
    {
        throw std::runtime_error("failed to create Queue!");
    }
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.QueueIndex.value(), indices.PresentIndex.value()
    };
    std::vector<VkDeviceQueueCreateInfo> QueueCreateInfos;
    float queuePriority = 1.0f;
    for(auto& QueueIndex: uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = QueueIndex;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        QueueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = QueueCreateInfos.data();
    createInfo.queueCreateInfoCount = QueueCreateInfos.size();
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
    vkGetDeviceQueue(Device, indices.PresentIndex.value(), 0, &presentQueue);
}

void VulkanEngine::FindQueueFamily(QueueType Type)
{
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
    uint32_t i=0;
    for(const auto& queueFamily : queueFamilies)
    {
        switch (Type)
        {
        case QueueType::Graphics:
            if(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                indices.QueueIndex = i;
                break;
            }
        case QueueType::Surface:
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
            if(presentSupport)
                indices.PresentIndex = i;
        }
        i++;
    }
}

void VulkanEngine::CreateSwapChain(uint32_t width, uint32_t height)
{
    vkb::SwapchainBuilder swapchainBuilder{ physicalDevice, Device, surface ,
    indices.QueueIndex.value(), indices.PresentIndex.value()
    };

    swapChainImageFormat = VK_FORMAT_B8G8R8A8_UNORM;

    vkb::Swapchain vkbSwapchain = swapchainBuilder
        //.use_default_format_selection()
        .set_desired_format(VkSurfaceFormatKHR{ .format = swapChainImageFormat, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR })
        //use vsync present mode
        .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
        .set_desired_extent(width, height)
        .add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build()
        .value();

    _swapchainExtent = vkbSwapchain.extent;
    //store swapchain and its related images
    swapChain = vkbSwapchain.swapchain;
    _swapchainImages = vkbSwapchain.get_images().value();
    _swapchainImageViews = vkbSwapchain.get_image_views().value();
}

void VulkanEngine::DestroySwapChain()
{
    vkDestroySwapchainKHR(Device, swapChain, nullptr);

    // destroy swapchain resources
    for (int i = 0; i < _swapchainImageViews.size(); i++) {

        vkDestroyImageView(Device, _swapchainImageViews[i], nullptr);
    }
}

