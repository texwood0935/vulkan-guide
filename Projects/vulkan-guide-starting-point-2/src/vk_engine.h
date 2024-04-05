// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <set>
#include <vk_types.h>

class VulkanEngine {
public:

	bool _isInitialized{ false };
	int _frameNumber {0};
	bool stop_rendering{ false };
	VkExtent2D _windowExtent{ 1700 , 900 };

	struct SDL_Window* _window{ nullptr };

	static VulkanEngine& Get();

	//initializes everything in the engine
	void init();

	//shuts down the engine
	void cleanup();

	//draw loop
	void draw();

	//run main loop
	void run();
private:
	void InitVulkan();
	
	void CreateInstance();
	uint32_t GetSupportExtensions(std::vector<std::string>& extensions);
	bool CheckSupportLayers();

	void PickupPhysicalDevice();
	void CreateLogicalDevice();
	struct QueueFamilyIndices FindQueueFamily(enum QueueType Type);
	//-----------------
	//std::unique_ptr<VkInstance> VkIns = nullptr;
	VkInstance VkIns;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;
	VkQueue graphicsQueue;
};
