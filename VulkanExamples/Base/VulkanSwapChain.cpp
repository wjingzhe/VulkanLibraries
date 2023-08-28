#include "VulkanSwapChain.h"

/** @brief Creates the platform specific surface abstraction of the native platform window used for presentation */
#if defined(VK_USE_PLATFORM_WIN32_KHR)
void VulkanSwapChain::initSurface(void* platformHandle, void* platformWindow)
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
void VulkanSwapChain::initSurface(ANativeWindow* window)
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
void VulkanSwapChain::initSurface(IDirectFB* dfb, IDirectFBSurface* window)
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
void VulkanSwapChain::initSurface(wl_display *display, wl_surface *window)
#elif defined(VK_USE_PLATFORM_XCB_KHR)
void VulkanSwapChain::initSurface(xcb_connection_t* connection, xcb_window_t window)
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
void VulkanSwapChain::initSurface(void* view)
#elif (defined(_DIRECT2DISPLAY) || defined(VK_USE_PLATFORM_HEADLESS_EXT))
void VulkanSwapChain::initSurface(uint32_t width, uint32_t height)
#endif
{
	VkResult err = VK_SUCCESS;

	// Create the os-specific surface
#if defined(VK_USE_PLATFORM_WIN32_KHR)
	VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.hinstance = (HINSTANCE)platformHandle;
	surfaceCreateInfo.hwnd = (HWND)platformWindow;
	err = vkCreateWin32SurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	VkAndroidSurfaceCreateInfoKHR surfaceCreateInfo = {};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.window = window;
	err = vkCreateAndroidSurfaceKHR(instance, &surfaceCreateInfo, NULL, &surface);
#elif defined(VK_USE_PLATFORM_IOS_MVK)
	VkIOSSurfaceCreateInfoMVK surfaceCreateInfo = {};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK;
	surfaceCreateInfo.pNext = NULL;
	surfaceCreateInfo.flags = 0;
	surfaceCreateInfo.pView = view;
	err = vkCreateIOSSurfaceMVK(instance, &surfaceCreateInfo, nullptr, &surface);
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
	VkMacOSSurfaceCreateInfoMVK surfaceCreateInfo = {};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_MACOS_SURFACE_CREATE_INFO_MVK;
	surfaceCreateInfo.pNext = NULL;
	surfaceCreateInfo.flags = 0;
	surfaceCreateInfo.pView = view;
	err = vkCreateMacOSSurfaceMVK(instance, &surfaceCreateInfo, NULL, &surface);
#elif defined(_DIRECT2DISPLAY)
	createDirect2DisplaySurface(width, height);
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
	VkDirectFBSurfaceCreateInfoEXT surfaceCreateInfo = {};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_DIRECTFB_SURFACE_CREATE_INFO_EXT;
	surfaceCreateInfo.dfb = dfb;
	surfaceCreateInfo.surface = window;
	err = vkCreateDirectFBSurfaceEXT(instance, &surfaceCreateInfo, nullptr, &surface);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	VkWaylandSurfaceCreateInfoKHR surfaceCreateInfo = {};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.display = display;
	surfaceCreateInfo.surface = window;
	err = vkCreateWaylandSurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	VkXcbSurfaceCreateInfoKHR surfaceCreateInfo = {};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
	surfaceCreateInfo.connection = connection;
	surfaceCreateInfo.window = window;
	err = vkCreateXcbSurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface);
#elif defined(VK_USE_PLATFORM_HEADLESS_EXT)
	VkHeadlessSurfaceCreateInfoEXT surfaceCreateInfo = {};
	surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
	PFN_vkCreateHeadlessSurfaceEXT fpCreateHeadlessSurfaceEXT = (PFN_vkCreateHeadlessSurfaceEXT)vkGetInstanceProcAddr(instance, "vkCreateHeadlessSurfaceEXT");
	if (!fpCreateHeadlessSurfaceEXT) {
		vks::tools::exitFatal("Could not fetch function pointer for the headless extension!", -1);
	}
	err = fpCreateHeadlessSurfaceEXT(instance, &surfaceCreateInfo, nullptr, &surface);
#endif

	if (err!=VK_SUCCESS)
	{
		vks::tools::exitFatal("Could not create surface!", err);
	}

	// Get available queue family properties
	uint32_t queueCount;
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, NULL);
	assert(queueCount >= 1);

	std::vector<VkQueueFamilyProperties> queueProps(queueCount);
	vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueCount, queueProps.data());

	//Iterate over each to learn whether it supports presenting
	// Find a queue with present support
	// Will be used to present the swap chain images to the windowing system
	std::vector<VkBool32>supportsPresent(queueCount);
	for (uint32_t i = 0;i<queueCount;++i)
	{
		fpGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &supportsPresent[i]);
	}

	// Search for a graphics and a present queue in the array of queue
	// families, try to find one that supports both
	uint32_t graphicsQueueNodeIndex = UINT32_MAX;
	uint32_t presentQueueNodexIndex = UINT32_MAX;
	for (uint32_t i = 0;i<queueCount;++i)
	{
		if ((queueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)!=0)
		{
			if (graphicsQueueNodeIndex == UINT32_MAX)
			{
				graphicsQueueNodeIndex = i;
			}

			if (supportsPresent[i] == VK_TRUE)
			{
				graphicsQueueNodeIndex = i;
				presentQueueNodexIndex = i;
				break;
			}//应该把这段逻辑提前
		}
	}//for

	if (presentQueueNodexIndex == UINT32_MAX)
	{
		// if there's no queue that supports both present and graphics
		// try to find a separate present queue
		for (uint32_t i = 0;i<queueCount;++i)
		{
			if (supportsPresent[i] == VK_TRUE)
			{
				presentQueueNodexIndex = i;
				break;
			}
		}//for
	}//if

	// Exit if either a graphics or a presenting queue hasn't been found
	if (graphicsQueueNodeIndex == UINT32_MAX  || presentQueueNodexIndex == UINT32_MAX)
	{
		vks::tools::exitFatal("Could not find a graphics and/or presenting queue!", -1);
	}

	//todo : Add support for separate graphics and presenting queue
	if (graphicsQueueNodeIndex!=presentQueueNodexIndex)
	{
		vks::tools::exitFatal("Separate graphics and presenting queues are not supported yet!", -1);
	}

	queueNodeIndex = graphicsQueueNodeIndex;


	//Get list of supported surface formats
	uint32_t formatCount;
	VK_CHECK_RESULT(fpGetPhysicalDeviceSurfaceFormatKHR(physicalDevice, surface, &formatCount, NULL));
	assert(formatCount > 0);

	std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
	VK_CHECK_RESULT(fpGetPhysicalDeviceSurfaceFormatKHR(physicalDevice, surface, &formatCount, surfaceFormats.data()));

	//If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
	//there is no preferred format, so we assume VK_FORMAT_B8G8R8A8_UNORM
	if ((formatCount ==1) &&(surfaceFormats[0].format == VK_FORMAT_UNDEFINED))
	{
		colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
		colorSpace = surfaceFormats[0].colorSpace;
	}
	else
	{
		// iterate over the list of available surface format and
		// check for the presence of VK_FORMAT_B8G8R8A8_UNORM
		bool found_B8G8R8A8_UNORM = false;
		for (auto && tempSurfaceFormat:surfaceFormats)
		{
			if (tempSurfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM)
			{
				colorFormat = tempSurfaceFormat.format;
				colorSpace = tempSurfaceFormat.colorSpace;
				found_B8G8R8A8_UNORM = true;
				break;
			}
		}//for

		//in case VK_FORMAT_B8G8R8A8_UNORM is not available
		// select the first available color format
		if (!found_B8G8R8A8_UNORM)
		{
			colorFormat = surfaceFormats[0].format;
			colorSpace = surfaceFormats[0].colorSpace;
		}
	}//if_else

}

// Set instance,physical and logical device to use for the swapchain and get all required funtion pointers
void VulkanSwapChain::connect(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device)
{
	this->instance = instance;
	this->physicalDevice = physicalDevice;
	this->device = device;

	fpGetPhysicalDeviceSurfaceSupportKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceSupportKHR"));
	fpGetPhysicalDeviceSurfaceCapabilitiesKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
	fpGetPhysicalDeviceSurfaceFormatKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
	fpGetPhysicalDeviceSurfacePresentModesKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfacePresentModesKHR"));
	
	fpCreateSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR>(vkGetInstanceProcAddr(instance, "vkCreateSwapchainKHR"));
	fpDestroySwapchainKHR = reinterpret_cast<PFN_vkDestroySwapchainKHR>(vkGetInstanceProcAddr(instance, "vkDestroySwapchainKHR"));
	fpGetSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(vkGetInstanceProcAddr(instance, "vkGetSwapchainImagesKHR"));
	fpAcquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(vkGetInstanceProcAddr(instance, "vkAcquireNextImageKHR"));
	fpQueuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(vkGetInstanceProcAddr(instance, "vkQueuePresentKHR"));

}

//Create the swapchain and get its images with given width and height
void VulkanSwapChain::create(uint32_t * width, uint32_t * height, bool vsync)
{
	//Store the current swap chain handle so we can use it later on to ease up recreation
	VkSwapchainKHR oldSwapchain = this->swapChain;

	//Get physical device surface properties and formats
	VkSurfaceCapabilitiesKHR surfaceCaps;
	VK_CHECK_RESULT(fpGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, this->surface, &surfaceCaps));

	//Get available present modes
	uint32_t presentModeCount;
	VK_CHECK_RESULT(fpGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, this->surface, &presentModeCount, NULL));
	assert(presentModeCount > 0);

	std::vector<VkPresentModeKHR> presentModes(presentModeCount);
	VK_CHECK_RESULT(fpGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, this->surface, &presentModeCount, presentModes.data()));

	VkExtent2D swapchainExtent = {};
	//if width (and height) equals the special value 0xFFFFFFFF,the size of the surface will set by swapchain
	if (surfaceCaps.currentExtent.width == (uint32_t)-1)
	{
		//if the size is undefined,the size is set to the size of the images requested.
		swapchainExtent.width = *width;
		swapchainExtent.height = *height;
	}
	else
	{
		swapchainExtent = surfaceCaps.currentExtent;
		*width = surfaceCaps.currentExtent.width;
		*height = surfaceCaps.currentExtent.height;
	}

	// Select a present mode for the swapchain

	// The VK_PRESENT_MODE_FIFO_KHR mode must always be present as per spec
	// This mode waits for the vertical blank("v-sync")
	VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

	//If v-sync is not requested,try to find a mailbox mode
	// It's the lowest latency non-tearing present mode available
	if (!vsync)
	{
		for (size_t i=0;i<presentModeCount;++i)
		{
			if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
				break;
			}

			if (presentModes[i]==VK_PRESENT_MODE_IMMEDIATE_KHR)
			{
				swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
			}

		}//for
	}

	//设置缓冲区数量
	uint32_t desiredNumberOfSwapchainImages = surfaceCaps.minImageCount + 1;
	if( (surfaceCaps.maxImageCount>0) && (desiredNumberOfSwapchainImages>surfaceCaps.maxImageCount))
	{
		desiredNumberOfSwapchainImages = surfaceCaps.maxImageCount;
	}

	//Find the transformation of the surface
	VkSurfaceTransformFlagBitsKHR preTransform;
	if (surfaceCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
	{
		preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	}
	else
	{
		preTransform = surfaceCaps.currentTransform;
	}

	//Find a supported composite alpha format(not all devices support alpha opaque)
	VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	// Simply select the first composite alpha format available
	std::vector<VkCompositeAlphaFlagBitsKHR> compositeAlphaFlags =
	{
		VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
		VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
	};

	for (auto& tempCompositeAlphaFlag:compositeAlphaFlags)
	{
		if (surfaceCaps.supportedCompositeAlpha&tempCompositeAlphaFlag)
		{
			compositeAlpha = tempCompositeAlphaFlag;
			break;
		}
	}

	VkSwapchainCreateInfoKHR swapchainCI = {};
	swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainCI.surface = surface;
	swapchainCI.minImageCount = desiredNumberOfSwapchainImages;
	swapchainCI.imageFormat = colorFormat;
	swapchainCI.imageColorSpace = colorSpace;
	swapchainCI.imageExtent = { swapchainExtent.width,swapchainExtent.height };
	swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainCI.preTransform = (VkSurfaceTransformFlagBitsKHR)preTransform;
	swapchainCI.imageArrayLayers = 1;
	swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainCI.queueFamilyIndexCount = 0;
	swapchainCI.presentMode = swapchainPresentMode;
	//Setting old swapChain to the saved handle of the previous swapchain aids in resource reuse and makes sure that we can still present already acquired images
	swapchainCI.oldSwapchain = oldSwapchain;
	//Setting clipped to VK_TRUE allows the implementation to discard rendering outside of the surface area
	swapchainCI.clipped = VK_TRUE;
	swapchainCI.compositeAlpha = compositeAlpha;

	//Enable transfer source on swap chain images if supported
	if (surfaceCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
	{
		swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	}

	//Enable transfer destination on swap chain images if supported
	if (surfaceCaps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
	{
		swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}

	VK_CHECK_RESULT(fpCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapChain));

	//if an existing swap chain is re-created, destroy the old swap chain
	// This also cleans up all the presentable images view
	if (oldSwapchain!=VK_NULL_HANDLE)
	{
		for (uint32_t i = 0;i<imageCount;++i)
		{
			vkDestroyImageView(device, buffers[i].view, nullptr);
		}
		fpDestroySwapchainKHR(device, oldSwapchain, nullptr);
	}

	VK_CHECK_RESULT(fpGetSwapchainImagesKHR(device, swapChain, &imageCount, NULL));

	//Get the swap chain images
	images.resize(imageCount);
	VK_CHECK_RESULT(fpGetSwapchainImagesKHR(device, swapChain, &imageCount, images.data()));

	// Get the swap chain buffers containing the image and image view
	buffers.resize(imageCount);
	for (uint32_t i = 0;i<imageCount;++i)
	{
		VkImageViewCreateInfo colorAttachmentView = {};
		colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		colorAttachmentView.pNext = NULL;
		colorAttachmentView.format = colorFormat;
		colorAttachmentView.components = 
		{
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_B,
			VK_COMPONENT_SWIZZLE_A,
		};

		colorAttachmentView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorAttachmentView.subresourceRange.baseMipLevel = 0;
		colorAttachmentView.subresourceRange.levelCount = 1;
		colorAttachmentView.subresourceRange.baseArrayLayer = 0;
		colorAttachmentView.subresourceRange.layerCount = 1;
		colorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorAttachmentView.flags = 0;
		colorAttachmentView.image = images[i];

		buffers[i].image = images[i];

		VK_CHECK_RESULT(vkCreateImageView(device, &colorAttachmentView, nullptr, &buffers[i].view));
	}//for

}

// Acquires the next image in the swap chain
VkResult VulkanSwapChain::acquireNextImage(VkSemaphore presentCompleteSemaphore, uint32_t * imageIndex)
{
	return fpAcquireNextImageKHR(device, swapChain, UINT64_MAX, presentCompleteSemaphore, (VkFence)nullptr, imageIndex);
}

VkResult VulkanSwapChain::queuePresent(VkQueue queue, uint32_t imageIndex, VkSemaphore waitSemaphore)
{
	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.pNext = NULL;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapChain;
	presentInfo.pImageIndices = &imageIndex;

	//check if a wait semaphore has been specified to wait for before presenting the image
	if (waitSemaphore!=VK_NULL_HANDLE)
	{
		presentInfo.pWaitSemaphores = &waitSemaphore;
		presentInfo.waitSemaphoreCount = 1;
	}

	return fpQueuePresentKHR(queue, &presentInfo);
}

void VulkanSwapChain::cleanup()
{
	if (swapChain!=VK_NULL_HANDLE)
	{
		for (uint32_t i = 0;i<imageCount;++i)
		{
			vkDestroyImageView(device, buffers[i].view, nullptr);
		}
	}//

	if (surface!=VK_NULL_HANDLE)
	{
		fpDestroySwapchainKHR(device, swapChain, nullptr);
		vkDestroySurfaceKHR(instance, surface, nullptr);
	}

	surface = VK_NULL_HANDLE;
	swapChain = VK_NULL_HANDLE;

}

#if defined(_DIRECT2DISPLAY)
/**
* Create direct to display surface
*/
void VulkanSwapChain::createDirect2DisplaySurface(uint32_t width, uint32_t height)
{
	uint32_t displayPropertyCount;

	// Get display property
	vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &displayPropertyCount, NULL);
	VkDisplayPropertiesKHR* pDisplayProperties = new VkDisplayPropertiesKHR[displayPropertyCount];
	vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &displayPropertyCount, pDisplayProperties);

	// Get plane property
	uint32_t planePropertyCount;
	vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physicalDevice, &planePropertyCount, NULL);
	VkDisplayPlanePropertiesKHR* pPlaneProperties = new VkDisplayPlanePropertiesKHR[planePropertyCount];
	vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physicalDevice, &planePropertyCount, pPlaneProperties);

	VkDisplayKHR display = VK_NULL_HANDLE;
	VkDisplayModeKHR displayMode;
	VkDisplayModePropertiesKHR* pModeProperties;
	bool foundMode = false;

	for (uint32_t i = 0; i < displayPropertyCount; ++i)
	{
		display = pDisplayProperties[i].display;
		uint32_t modeCount;
		vkGetDisplayModePropertiesKHR(physicalDevice, display, &modeCount, NULL);
		pModeProperties = new VkDisplayModePropertiesKHR[modeCount];
		vkGetDisplayModePropertiesKHR(physicalDevice, display, &modeCount, pModeProperties);

		for (uint32_t j = 0; j < modeCount; ++j)
		{
			const VkDisplayModePropertiesKHR* mode = &pModeProperties[j];

			if (mode->parameters.visibleRegion.width == width && mode->parameters.visibleRegion.height == height)
			{
				displayMode = mode->displayMode;
				foundMode = true;
				break;
			}
		}
		if (foundMode)
		{
			break;
		}
		delete[] pModeProperties;
	}

	if (!foundMode)
	{
		vks::tools::exitFatal("Can't find a display and a display mode!", -1);
		return;
	}

	// Search for a best plane we can use
	uint32_t bestPlaneIndex = UINT32_MAX;
	VkDisplayKHR* pDisplays = NULL;
	for (uint32_t i = 0; i < planePropertyCount; i++)
	{
		uint32_t planeIndex = i;
		uint32_t displayCount;
		vkGetDisplayPlaneSupportedDisplaysKHR(physicalDevice, planeIndex, &displayCount, NULL);
		if (pDisplays)
		{
			delete[] pDisplays;
		}
		pDisplays = new VkDisplayKHR[displayCount];
		vkGetDisplayPlaneSupportedDisplaysKHR(physicalDevice, planeIndex, &displayCount, pDisplays);

		// Find a display that matches the current plane
		bestPlaneIndex = UINT32_MAX;
		for (uint32_t j = 0; j < displayCount; j++)
		{
			if (display == pDisplays[j])
			{
				bestPlaneIndex = i;
				break;
			}
		}
		if (bestPlaneIndex != UINT32_MAX)
		{
			break;
		}
	}

	if (bestPlaneIndex == UINT32_MAX)
	{
		vks::tools::exitFatal("Can't find a plane for displaying!", -1);
		return;
	}

	VkDisplayPlaneCapabilitiesKHR planeCap;
	vkGetDisplayPlaneCapabilitiesKHR(physicalDevice, displayMode, bestPlaneIndex, &planeCap);
	VkDisplayPlaneAlphaFlagBitsKHR alphaMode = (VkDisplayPlaneAlphaFlagBitsKHR)0;

	if (planeCap.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR)
	{
		alphaMode = VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR;
	}
	else if (planeCap.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR)
	{
		alphaMode = VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR;
	}
	else if (planeCap.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR)
	{
		alphaMode = VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR;
	}
	else if (planeCap.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR)
	{
		alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
	}

	VkDisplaySurfaceCreateInfoKHR surfaceInfo{};
	surfaceInfo.sType = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
	surfaceInfo.pNext = NULL;
	surfaceInfo.flags = 0;
	surfaceInfo.displayMode = displayMode;
	surfaceInfo.planeIndex = bestPlaneIndex;
	surfaceInfo.planeStackIndex = pPlaneProperties[bestPlaneIndex].currentStackIndex;
	surfaceInfo.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	surfaceInfo.globalAlpha = 1.0;
	surfaceInfo.alphaMode = alphaMode;
	surfaceInfo.imageExtent.width = width;
	surfaceInfo.imageExtent.height = height;

	VkResult result = vkCreateDisplayPlaneSurfaceKHR(instance, &surfaceInfo, NULL, &surface);
	if (result != VK_SUCCESS) {
		vks::tools::exitFatal("Failed to create surface!", result);
	}

	delete[] pDisplays;
	delete[] pModeProperties;
	delete[] pDisplayProperties;
	delete[] pPlaneProperties;
}
#endif 