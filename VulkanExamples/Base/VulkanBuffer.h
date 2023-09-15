#pragma once


#include <vector>
#include "vulkan/vulkan.h"
#include "VulkanTools.h"

namespace vks
{
	struct Buffer
	{
		VkDevice device;
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory deviceMemory = VK_NULL_HANDLE;
		VkDescriptorBufferInfo descriptorBufferInfo;
		VkDeviceSize size = 0;
		VkDeviceSize alignment = 0;
		void* mappedData = nullptr;
		VkBufferUsageFlags bufferUsageFlags;
		VkMemoryPropertyFlags memoryPropertyFlags;

		VkResult map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

		void unmap();

		VkResult bind(VkDeviceSize offset = 0);

		void setupDescriptor(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

		void copyFromData(void* data, VkDeviceSize size);

		VkResult flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

		VkResult invalidate(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

		void destroy();
	};

}//vks