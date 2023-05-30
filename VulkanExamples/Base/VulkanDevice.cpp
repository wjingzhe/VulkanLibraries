#include "VulkanDevice.h"
#include <unordered_set>

namespace vks
{
	VulkanDevice::VulkanDevice(VkPhysicalDevice physicalDevice)
	{
		assert(physicalDevice);
		this->physicalDevice = physicalDevice;

		vkGetPhysicalDeviceProperties(physicalDevice, &properties);

		vkGetPhysicalDeviceFeatures(physicalDevice, &features);

		vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

		uint32_t queueFamilyCount;
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
		assert(queueFamilyCount > 0);
		queueFamilyProperties.resize(queueFamilyCount);
		vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

		uint32_t extensionCount = 0;
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
		if (extensionCount>0)
		{
			std::vector<VkExtensionProperties> extensionProperties(extensionCount);
			if (vkEnumerateDeviceExtensionProperties(physicalDevice,nullptr, &extensionCount,extensionProperties.data()) == VK_SUCCESS)//jingz
			{
				for (auto extension:extensionProperties)
				{
					supportedExtensions.push_back(extension.extensionName);
				}//for
			}//if
		}

	}//VulkanDevice
	
	VulkanDevice::~VulkanDevice()
	{
		if (commandPool)
		{
			vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
		}

		if (logicalDevice)
		{
			vkDestroyDevice(logicalDevice, nullptr);
		}

	}

	uint32_t VulkanDevice::GetMemoryType(uint32_t typeBits, VkMemoryPropertyFlags propertyFlags, VkBool32 * memTypeFound) const
	{
		for (uint32_t i =0;i<memoryProperties.memoryHeapCount;++i)
		{
			if ((typeBits & 1)==1)
			{
				if ((memoryProperties.memoryTypes[i].propertyFlags & propertyFlags) == propertyFlags)
				{
					if (memTypeFound)
					{
						*memTypeFound = true;
					}

					return i;
				}
			}
		}//for


		if (memTypeFound)
		{
			*memTypeFound = false;
			return 0;
		}
		else
		{
			throw std::runtime_error("Could not find a matching memory type");
		}
	}

	uint32_t VulkanDevice::GetQueueFamilyIndex(VkQueueFlagBits queueFlags) const
	{
		if (queueFlags & VK_QUEUE_COMPUTE_BIT)
		{
			for (uint32_t i = 0;i<static_cast<uint32_t>(queueFamilyProperties.size());i++)
			{
				if ((queueFamilyProperties[i].queueFlags & queueFlags) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)==0) )
				{
					return i;
				}
			}

		}//compute queue
		
		if (queueFlags&VK_QUEUE_TRANSFER_BIT)
		{
			for (uint32_t i = 0;i<static_cast<uint32_t>(queueFamilyProperties.size());++i)
			{
				if(
					(queueFamilyProperties[i].queueFlags&queueFlags)
					&&
					(((queueFamilyProperties[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)==0 )&& ((queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT)==0))
					)
				{
					return i;
				}
			}
		}//transfer queue

		//for other queue types or if no separate compute queue is present,return the first one to support the requested flags
		for (uint32_t i = 0;i<static_cast<uint32_t>(queueFamilyProperties.size());++i)
		{
			if (queueFamilyProperties[i].queueFlags & queueFlags)
			{
				return i;
			}
		}

		throw std::runtime_error("Could  not find a matching queue family index");
	}

	VkResult VulkanDevice::CreateLogicalDevice(VkPhysicalDeviceFeatures enabledFeatures, std::vector<const char*> enabledExtensions, void * pNextChain, bool useSwapChain, VkQueueFlags requestedQueueTypes)
	{
		// Desired queues need to be requested upon logical device creation
		// Due to differing queue family configurations of Vulkan implementations this can be a bit tricky, especially if the application
		// requests different queue types
		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};

		// Get queue family indices for the requested queue family types
		// Note that the indices may overlap depending on the implementation

		const float defaultQueuePriority(0.0f);

		//Graphics queue
		if (requestedQueueTypes & VK_QUEUE_GRAPHICS_BIT)
		{
			queueFamilyIndices.graphicIndex = GetQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT);
			VkDeviceQueueCreateInfo queueInfo{};
			queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueInfo.queueFamilyIndex = queueFamilyIndices.graphicIndex;
			queueInfo.queueCount = 1;
			queueInfo.pQueuePriorities = &defaultQueuePriority;
			queueCreateInfos.push_back(queueInfo);
		}
		else
		{
			queueFamilyIndices.graphicIndex = 0;
		}

		if (requestedQueueTypes & VK_QUEUE_COMPUTE_BIT)
		{
			queueFamilyIndices.computeIndex = GetQueueFamilyIndex(VK_QUEUE_COMPUTE_BIT);
			if (queueFamilyIndices.computeIndex!=queueFamilyIndices.graphicIndex)
			{
				// If compute family index differs, we need an additional queue create info for the compute queue
				VkDeviceQueueCreateInfo queueInfo{};
				queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueInfo.queueFamilyIndex = queueFamilyIndices.computeIndex;
				queueInfo.queueCount = 1;
				queueInfo.pQueuePriorities = &defaultQueuePriority;
				queueCreateInfos.push_back(queueInfo);
			}
		}
		else
		{
			queueFamilyIndices.computeIndex = queueFamilyIndices.graphicIndex;
		}

		if (requestedQueueTypes&VK_QUEUE_TRANSFER_BIT)
		{
			queueFamilyIndices.transferIndex = GetQueueFamilyIndex(VK_QUEUE_TRANSFER_BIT);
			if ((queueFamilyIndices.transferIndex != queueFamilyIndices.graphicIndex) && (queueFamilyIndices.transferIndex != queueFamilyIndices.computeIndex))
			{
				// If compute family index differs, we need an additional queue create info for the compute queue
				VkDeviceQueueCreateInfo queueInfo{};
				queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
				queueInfo.queueFamilyIndex = queueFamilyIndices.transferIndex;
				queueInfo.queueCount = 1;
				queueInfo.pQueuePriorities = &defaultQueuePriority;
				queueCreateInfos.push_back(queueInfo);
			}
			else
			{
				queueFamilyIndices.transferIndex = queueFamilyIndices.graphicIndex;
			}
		}

		//Swapchain Extension

		//Create the logical device representation
		std::vector<const char*>deviceExtensions(enabledExtensions);
		if (useSwapChain)
		{
			deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
		}

		VkDeviceCreateInfo deviceCreateInfo = {};
		deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		deviceCreateInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
		deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
		deviceCreateInfo.pEnabledFeatures = &enabledFeatures;

		//if a pNext(Chain) has been passed, we need to add it to the device creation info
		VkPhysicalDeviceFeatures2 physicalDeviceFeatures2{};
		if (pNextChain)
		{
			physicalDeviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
			physicalDeviceFeatures2.features = enabledFeatures;
			physicalDeviceFeatures2.pNext = pNextChain;
			deviceCreateInfo.pEnabledFeatures = nullptr;
			deviceCreateInfo.pNext = &physicalDeviceFeatures2;
		}

		//Debug Extension

		//Enable the debug marker extension if it is present (likely meaning a debugging tool is present)
		if (IsExtensionSupported(VK_EXT_DEBUG_MARKER_EXTENSION_NAME))
		{
			deviceExtensions.push_back(VK_EXT_DEBUG_MARKER_EXTENSION_NAME);
			enableDebugMarkers = true;
		}

		if (deviceExtensions.size() > 0)
		{
			for (const char* enabledExtension:deviceExtensions)
			{
				if (!IsExtensionSupported(enabledExtension))
				{
					std::cerr << "Enabled device extension \"" << enabledExtension << "\" is not present at device level\n";
				}
			}//for

			deviceCreateInfo.enabledExtensionCount = (uint32_t)deviceExtensions.size();
			deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

		}

		this->m_enabledFeatures = enabledFeatures;

		VkResult result = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &logicalDevice);
		if (result!=VK_SUCCESS)
		{
			return result;
		}

		//create a default command pool for graphics command buffers
		commandPool = CreateCommandPool(queueFamilyIndices.graphicIndex);

		return result;
	}

	VkResult VulkanDevice::CreateBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, VkDeviceSize size, VkBuffer * buffer, VkDeviceMemory * memory, void * data)
	{
		VkBufferCreateInfo bufferCreateInfo = vks::initializers::GenBufferCreateInfo(usageFlags, size);
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;//¶À¼Ò·ÃÎÊµÄ
		VK_CHECK_RESULT(vkCreateBuffer(logicalDevice, &bufferCreateInfo, nullptr, buffer));

		// Create the memory backing up the buffer handle
		VkMemoryRequirements memReqs;
		VkMemoryAllocateInfo memAllocInfo = vks::initializers::GenMemoryAllocateInfo();
		vkGetBufferMemoryRequirements(logicalDevice, *buffer, &memReqs);
		memAllocInfo.allocationSize = memReqs.size;
		// Find a memory type index that fits the properties of the buffer
		memAllocInfo.memoryTypeIndex = GetMemoryType(memReqs.memoryTypeBits, memoryPropertyFlags);

		VkMemoryAllocateFlagsInfoKHR allocFlagsInfo{};
		if (usageFlags&VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
		{
			allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
			allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
			memAllocInfo.pNext = &allocFlagsInfo;
		}
		VK_CHECK_RESULT(vkAllocateMemory(logicalDevice, &memAllocInfo, nullptr, memory));

		if (data != nullptr)
		{
			void* mappedData;
			VK_CHECK_RESULT(vkMapMemory(logicalDevice, *memory, 0, size, 0, &mappedData));
			memcpy(mappedData, data, size);
			//f host coherency hasn't requested, do a manual flush to make writes visible
			if ( (memoryPropertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)==0 )
			{
				VkMappedMemoryRange mappedRange = vks::initializers::GenMappedMemoryRange();
				mappedRange.memory = *memory;
				mappedRange.offset = 0;
				mappedRange.size = size;
				vkFlushMappedMemoryRanges(logicalDevice, 1, &mappedRange);
			}

			vkUnmapMemory(logicalDevice, *memory);
		}

		//Attach the memory to the buffer object
		VK_CHECK_RESULT(vkBindBufferMemory(logicalDevice, *buffer, *memory, 0));

		return VK_SUCCESS;
	}

	VkResult VulkanDevice::CreateBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, vks::Buffer * buffer, VkDeviceSize size, void * data)
	{
		buffer->device = logicalDevice;

		//Create the buffer handle
		VkBufferCreateInfo bufferCreateInfo = vks::initializers::GenBufferCreateInfo(usageFlags, size);
		VK_CHECK_RESULT(vkCreateBuffer(logicalDevice,&bufferCreateInfo,nullptr,&buffer->buffer));

		//Create the memory backing up the buffer handle
		VkMemoryRequirements memReqs;
		vkGetBufferMemoryRequirements(logicalDevice, buffer->buffer, &memReqs);

		VkMemoryAllocateInfo memAlloc = vks::initializers::GenMemoryAllocateInfo();
		memAlloc.allocationSize = memReqs.size;
		//Find  a memory type index that fits the properties of the buffer
		memAlloc.memoryTypeIndex = GetMemoryType(memReqs.memoryTypeBits, memoryPropertyFlags);
		//If the buffer has VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT set we also need to enable the appropriate flag during allocation
		VkMemoryAllocateFlagsInfoKHR allocFlagsInfo{};
		if (usageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
		{
			allocFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO_KHR;
			allocFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
			memAlloc.pNext = &allocFlagsInfo;
		}
		VK_CHECK_RESULT(vkAllocateMemory(logicalDevice, &memAlloc, nullptr, &buffer->deviceMemory));

		buffer->alignment = memReqs.alignment;
		buffer->size = size;
		buffer->bufferUsageFlags = usageFlags;
		buffer->memoryPropertyFlags = memoryPropertyFlags;

		//if a pointer to the buffer data has been passed, map the buffer and copy over the data
		if (data!=nullptr)
		{
			VK_CHECK_RESULT(buffer->map());
			memcpy(buffer->mappedData, data, size);
			if ((memoryPropertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
			{
				buffer->flush();
			}

			buffer->unmap();
		}

		//Initialize a default descriptor that covers the whole buffer size
		buffer->setupDescriptor();

		//Attach the memroy to the buffer object
		return buffer->bind();
	}

	void VulkanDevice::CopyBuffer(vks::Buffer * src, vks::Buffer * dst, VkQueue queue, VkBufferCopy * copyRegion)
	{
		assert(dst->size <= src->size);
		assert(src->buffer);
		VkCommandBuffer copyCmd = CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY,true);
		VkBufferCopy bufferCopy{};
		if (copyRegion==nullptr)
		{
			bufferCopy.size = src->size;
		}
		else
		{
			bufferCopy = *copyRegion;
		}

		vkCmdCopyBuffer(copyCmd, src->buffer, dst->buffer, 1, &bufferCopy);

		FlushCommandBuffer(copyCmd, queue);
		
	}

	VkCommandPool VulkanDevice::CreateCommandPool(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags createFlags)
	{
		VkCommandPoolCreateInfo cmdPoolCreateInfo = {};
		cmdPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cmdPoolCreateInfo.queueFamilyIndex = queueFamilyIndex;
		cmdPoolCreateInfo.flags = createFlags;
		VkCommandPool cmdPool;
		VK_CHECK_RESULT(vkCreateCommandPool(logicalDevice, &cmdPoolCreateInfo, nullptr, &cmdPool));

		return cmdPool;
	}

	VkCommandBuffer VulkanDevice::CreateCommandBuffer(VkCommandBufferLevel level, VkCommandPool pool, bool begin)
	{
		return CreateCommandBuffer(level,commandPool,begin);
	}

	VkCommandBuffer VulkanDevice::CreateCommandBuffer(VkCommandBufferLevel level, bool begin)
	{
		VkCommandBufferAllocateInfo cmdBufferCreateInfo = vks::initializers::GenCommandBufferAllocateInfo(commandPool, level, 1);
		VkCommandBuffer cmdBuffer;
		VK_CHECK_RESULT(vkAllocateCommandBuffers(logicalDevice, &cmdBufferCreateInfo, &cmdBuffer));

		//If requested, also start recording for the new Command Buffer
		if (begin)
		{
			VkCommandBufferBeginInfo cmdBeginInfo = vks::initializers::GenCommandBufferBeginInfo();
			VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer,&cmdBeginInfo));
		}

		return cmdBuffer;
	}

	void VulkanDevice::FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, VkCommandPool pool, bool free)
	{
		if (commandBuffer==VK_NULL_HANDLE)
		{
			return;
		}

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		VkSubmitInfo submitInfo = vks::initializers::GenSubmitInfo();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		//create fence to ensure that the command buffer has finished executing
		VkFenceCreateInfo fenceInfo = vks::initializers::GenFenceCreateInfo(VK_FLAGS_NONE);
		VkFence fence;
		VK_CHECK_RESULT(vkCreateFence(logicalDevice, &fenceInfo, nullptr, &fence));

		//Submit to the queue
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, fence));
		//Wait for the fence to signal that command buffer has finished executing
		VK_CHECK_RESULT(vkWaitForFences(logicalDevice, 1, &fence, VK_TRUE, DEFAULT_FENCE_TIMEOUT));

		vkDestroyFence(logicalDevice, fence, nullptr);
		if (free)
		{
			vkFreeCommandBuffers(logicalDevice, pool, 1, &commandBuffer);
		}
	}

	void VulkanDevice::FlushCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue, bool free)
	{
		return FlushCommandBuffer(commandBuffer, queue, commandPool, free);
	}

	// Check if an extension is supported by the physical device
	bool VulkanDevice::IsExtensionSupported(std::string extension)
	{
		return std::find(supportedExtensions.begin(),supportedExtensions.end(),extension)!=supportedExtensions.end();
	}

	//Select the best-fit depth format for this device from a list of possible depth (and stencil) formats
	VkFormat VulkanDevice::GetSupportedDepthFormat(bool checkSamplingSupport)
	{
		//All depth formats may be optional, so we need to find a suitable depth format to use
		std::vector<VkFormat> depthFormats = { VK_FORMAT_D32_SFLOAT_S8_UINT,VK_FORMAT_D32_SFLOAT,VK_FORMAT_D24_UNORM_S8_UINT,VK_FORMAT_D16_UNORM_S8_UINT,VK_FORMAT_D16_UNORM };
		for (auto& format:depthFormats)
		{
			VkFormatProperties formatProperties;
			vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);

			//format must support depth stencil attachment for optimal tiling
			if (formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
			{
				if (checkSamplingSupport)
				{
					if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
					{
						continue;
					}
				}
				return format;
			}//VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
		}

		throw std::runtime_error("Could not find a matching depth format");
	}


}//namespace vks


