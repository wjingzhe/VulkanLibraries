#include "VulkanBuffer.h"

namespace vks
{

	VkResult Buffer::map(VkDeviceSize size, VkDeviceSize offset)
	{
		return vkMapMemory(device, deviceMemory, offset, size, 0, &mappedData);
	}

	void Buffer::unmap()
	{
		if (mappedData)
		{
			vkUnmapMemory(device, deviceMemory);
			mappedData = nullptr;
		}
	}

	VkResult Buffer::bind(VkDeviceSize offset)
	{
		return vkBindBufferMemory(device,buffer,deviceMemory,offset);
	}

	void Buffer::setupDescriptor(VkDeviceSize size, VkDeviceSize offset)
	{
		descriptorBufferInfo.offset = offset;
		descriptorBufferInfo.buffer = buffer;
		descriptorBufferInfo.range = size;
	}

	void Buffer::copyFromData(void * data, VkDeviceSize size)
	{
		assert(mappedData);
		memcpy(mappedData, data, size);
	}

	VkResult Buffer::flush(VkDeviceSize size, VkDeviceSize offset)
	{
		VkMappedMemoryRange mappedRange = {};
		mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		mappedRange.memory = deviceMemory;
		mappedRange.offset = offset;
		mappedRange.size = size;

		return vkFlushMappedMemoryRanges(device,1,&mappedRange);
	}

	VkResult Buffer::invalidate(VkDeviceSize size, VkDeviceSize offset)
	{
		VkMappedMemoryRange mappedRange = {};
		mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
		mappedRange.memory = deviceMemory;
		mappedRange.offset = offset;
		mappedRange.size = size;

		return vkInvalidateMappedMemoryRanges(device, 1, &mappedRange);
	}

	void Buffer::destroy()
	{
		if (buffer)
		{
			vkDestroyBuffer(device, buffer, nullptr);
		}

		if (deviceMemory)
		{
			vkFreeMemory(device, deviceMemory, nullptr);
		}
	}


}// namespace vks


