﻿//> includes
#include "vk_engine.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <vk_initializers.h>
#include <vk_types.h>

#include <chrono>
#include <iostream>
#include <thread>

#include "VkBootstrap.h"
#include "vk_images.h"

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

        vkDeviceWaitIdle(Device);
        for(int32_t i=0;i<FRAME_OVERLAP;i++)
        {
            vkDestroyCommandPool(Device,frame[i].commandPool,nullptr);

            //destroy sync objects
            vkDestroyFence(Device, frame[i].RenderFence, nullptr);
            vkDestroySemaphore(Device, frame[i].RenderSemaphore, nullptr);
            vkDestroySemaphore(Device ,frame[i].SwapChainSemaphore, nullptr);
        }
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
    VK_CHECK(vkWaitForFences(Device, 1, &GetCurrentFrame().RenderFence, true, 1000000000))
    VK_CHECK(vkResetFences(Device, 1, &GetCurrentFrame().RenderFence))
    
    uint32_t swapchainImageIndex;
    VK_CHECK(vkAcquireNextImageKHR(Device, swapChain, 1000000000, GetCurrentFrame().SwapChainSemaphore, nullptr, &swapchainImageIndex))

    //naming it cmd for shorter writing
    VkCommandBuffer cmd = GetCurrentFrame().mainCommmandBuffer;

    // now that we are sure that the commands finished executing, we can safely
    // reset the command buffer to begin recording again.
    VK_CHECK(vkResetCommandBuffer(cmd, 0))

    //begin the command buffer recording. We will use this command buffer exactly once, so we want to let vulkan know that
    VkCommandBufferBeginInfo cmdBeginInfo = {};//vkinit::command_buffer_begin_info(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    cmdBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cmdBeginInfo.pNext = nullptr;
    cmdBeginInfo.pInheritanceInfo = nullptr;
    cmdBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    //start the command buffer recording
    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo))

    //make the swapchain image into writeable mode before rendering
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    //make a clear-color from frame number. This will flash with a 120 frame period.
    VkClearColorValue clearValue;
    float flash = abs(sin(_frameNumber / 120.f));
    clearValue = { { 0.0f, 0.0f, flash, 1.0f } };

    VkImageSubresourceRange clearRange = vkinit::image_subresource_range(VK_IMAGE_ASPECT_COLOR_BIT);

    //clear image
    vkCmdClearColorImage(cmd, _swapchainImages[swapchainImageIndex], VK_IMAGE_LAYOUT_GENERAL, &clearValue, 1, &clearRange);

    //make the swapchain image into presentable mode
    vkutil::transition_image(cmd, _swapchainImages[swapchainImageIndex],VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

    //finalize the command buffer (we can no longer add commands, but it can now be executed)
    VK_CHECK(vkEndCommandBuffer(cmd))

    //prepare the submission to the queue. 
    //we want to wait on the _presentSemaphore, as that semaphore is signaled when the swapchain is ready
    //we will signal the _renderSemaphore, to signal that rendering has finished

    VkCommandBufferSubmitInfo cmdinfo = vkinit::command_buffer_submit_info(cmd);	
	
    VkSemaphoreSubmitInfo waitInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT_KHR,GetCurrentFrame().SwapChainSemaphore);
    VkSemaphoreSubmitInfo signalInfo = vkinit::semaphore_submit_info(VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT, GetCurrentFrame().RenderSemaphore);	
	
    VkSubmitInfo2 submit = vkinit::submit_info(&cmdinfo,&signalInfo,&waitInfo);	

    //submit command buffer to the queue and execute it.
    // _renderFence will now block until the graphic commands finish execution
    VK_CHECK(vkQueueSubmit2(graphicsQueue, 1, &submit, GetCurrentFrame().RenderFence))

    //prepare present
    // this will put the image we just rendered to into the visible window.
    // we want to wait on the _renderSemaphore for that, 
    // as its necessary that drawing commands have finished before the image is displayed to the user
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = nullptr;
    presentInfo.pSwapchains = &swapChain;
    presentInfo.swapchainCount = 1;

    presentInfo.pWaitSemaphores = &GetCurrentFrame().RenderSemaphore;
    presentInfo.waitSemaphoreCount = 1;

    presentInfo.pImageIndices = &swapchainImageIndex;

    VK_CHECK(vkQueuePresentKHR(graphicsQueue, &presentInfo))

    //increase the number of frames drawn
    _frameNumber++;
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
    InitCommands();
    InitSyncStructures();
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

    indices.GraphicIndex = vkbDevice.get_queue_index(vkb::QueueType::graphics).value();
    indices.PresentIndex = vkbDevice.get_queue_index(vkb::QueueType::present).value();
    graphicsQueue = vkbDevice.get_queue(vkb::QueueType::graphics).value();
    presentQueue = vkbDevice.get_queue(vkb::QueueType::present).value();

    //FindQueueFamily(QueueType::Graphics);
    //FindQueueFamily(QueueType::Surface);
    //vkGetDeviceQueue(Device, indices.GraphicIndex.value(), 0, &graphicsQueue);
    //vkGetDeviceQueue(Device, indices.PresentIndex.value(), 0, &presentQueue);
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
        indices.GraphicIndex.value(), indices.PresentIndex.value()
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
    vkGetDeviceQueue(Device, indices.GraphicIndex.value(), 0, &graphicsQueue);
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
                indices.GraphicIndex = i;
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
    indices.GraphicIndex.value(), indices.PresentIndex.value()
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

void VulkanEngine::InitCommands()
{
    VkCommandPoolCreateInfo commandPoolInfo = {};
    commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolInfo.pNext = nullptr;
    commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    commandPoolInfo.queueFamilyIndex = indices.GraphicIndex.value();
    for(int32_t i=0; i<FRAME_OVERLAP;i++)
    {
        VK_CHECK(vkCreateCommandPool(Device, &commandPoolInfo, nullptr, &frame[i].commandPool))

        VkCommandBufferAllocateInfo cmdAllocInfo = {};
        cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAllocInfo.pNext = nullptr;
        cmdAllocInfo.commandPool = frame[i].commandPool;
        cmdAllocInfo.commandBufferCount = 1;
        cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        VK_CHECK(vkAllocateCommandBuffers(Device, &cmdAllocInfo, &frame[i].mainCommmandBuffer))
    }
}

void VulkanEngine::InitSyncStructures()
{
    VkFenceCreateInfo fenceCreateInfo = {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.pNext = nullptr;
    fenceCreateInfo.flags =  VK_FENCE_CREATE_SIGNALED_BIT;

    VkSemaphoreCreateInfo SemaphoreCreateInfo = {};
    SemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    SemaphoreCreateInfo.pNext = nullptr;
    SemaphoreCreateInfo.flags = 0;
    for (int32_t i=0;i<FRAME_OVERLAP;i++)
    {
        VK_CHECK(vkCreateFence(Device, &fenceCreateInfo, nullptr, &frame[i].RenderFence))
        
        VK_CHECK(vkCreateSemaphore(Device, &SemaphoreCreateInfo, nullptr, &frame[i].RenderSemaphore))
        VK_CHECK(vkCreateSemaphore(Device, &SemaphoreCreateInfo, nullptr, &frame[i].SwapChainSemaphore))
    }
}
