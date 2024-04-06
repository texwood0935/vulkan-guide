// vulkan_guide.h : Include file for standard system include files,
// or project specific include files.

#pragma once

#include <set>
#include <vk_types.h>

struct QueueFamilyIndices
{
	std::optional<uint32_t> QueueIndex;

	std::optional<uint32_t> PresentIndex;

	bool IsQueueIndexComplete() { return QueueIndex.has_value(); }
	bool IsPresentIndexComplete() { return PresentIndex.has_value(); }
	bool IsComplete() { return QueueIndex.has_value() && PresentIndex.has_value(); }
};

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
	void CreateInstance_Vkb();
	uint32_t GetSupportExtensions(std::vector<std::string>& extensions);

	uint32_t GetSupportExtensions_SDL(std::vector<const char*>& extensions);
	
	bool CheckSupportLayers();

	void PickupPhysicalDevice();
	void CreateLogicalDevice();
	void FindQueueFamily(enum QueueType Type);

	void CreateSwapChain(uint32_t width, uint32_t height);
	void DestroySwapChain();
	//-----------------
	//std::unique_ptr<VkInstance> VkIns = nullptr;
	VkInstance VkIns;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT _debug_messenger;
	
	VkQueue graphicsQueue;
	VkQueue presentQueue;

	VkSurfaceKHR surface;
	VkSwapchainKHR swapChain;
	VkFormat swapChainImageFormat;
	std::vector<VkImage> _swapchainImages;
	std::vector<VkImageView> _swapchainImageViews;
	VkExtent2D _swapchainExtent;

	struct QueueFamilyIndices indices;
};
