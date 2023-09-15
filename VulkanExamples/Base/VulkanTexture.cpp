

#include "VulkanTexture.h"
#include <algorithm>

namespace vks
{
	void Texture::updateDescriptor()
	{
		descriptor.sampler = sampler;
		descriptor.imageView = view;
		descriptor.imageLayout = imageLayout;
	}

	void Texture::destroy()
	{
		vkDestroyImageView(device->logicalDevice, view, nullptr);
		vkDestroyImage(device->logicalDevice, image, nullptr);
		if (sampler)
		{
			vkDestroySampler(device->logicalDevice, sampler, nullptr);
		}
		vkFreeMemory(device->logicalDevice, deviceMemory, nullptr);
	}

	ktxResult Texture::loadKTXFile(std::string fileName, ktxTexture ** target)
	{
		ktxResult result = KTX_SUCCESS;

#if defined(__ANDROID__)
			AAsset* asset = AAssetManager_open(androidApp->activity->assetManager, filename.c_str(), AASSET_MODE_STREAMING);
		if (!asset) {
			vks::tools::exitFatal("Could not load texture from " + filename + "\n\nThe file may be part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download the latest version.", -1);
		}
		size_t size = AAsset_getLength(asset);
		assert(size > 0);
		ktx_uint8_t *textureData = new ktx_uint8_t[size];
		AAsset_read(asset, textureData, size);
		AAsset_close(asset);
		result = ktxTexture_CreateFromMemory(textureData, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, target);
		delete[] textureData;
#else
		if (!vks::tools::fileExists(fileName)) {
			vks::tools::exitFatal("Could not load texture from " + fileName + "\n\nThe file may be part of the additional asset pack.\n\nRun \"download_assets.py\" in the repository root to download the latest version.", -1);
		}
		result = ktxTexture_CreateFromNamedFile(fileName.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, target);
#endif		
		return result;
	}

	void Texture2D::loadFromFile(std::string fileName, VkFormat format, vks::VulkanDevice * device, VkQueue copyQueue, VkImageUsageFlags imageUsageFlags, VkImageLayout imageLayout, bool forceLinear)
	{
		ktxTexture* pKtxTexture;
		ktxResult result = loadKTXFile(fileName, &pKtxTexture);
		assert(result == KTX_SUCCESS);

		this->device = device;
		this->width = pKtxTexture->baseWidth;
		this->height = pKtxTexture->baseHeight;
		this->mipLevels = pKtxTexture->numLevels;

		ktx_uint8_t* ktxTextureData = ktxTexture_GetData(pKtxTexture);
		ktx_size_t ktxTextureSize = ktxTexture_GetSize(pKtxTexture);

		//Get device properties for the requested texture format
		VkFormatProperties formatProperties;
		vkGetPhysicalDeviceFormatProperties(device->physicalDevice, format, &formatProperties);

		//Only use linear tiling if requested (and supported by the device)
		// Support for linear tiling is mostly limited,so prefer to use optimal tiling instead
		// On most implementations linear tiling will only support a very limited amount
		// of formats and features(mip maps, cube maps,arrays,etc.)
		VkBool32 useStaging = !forceLinear;
		
		VkMemoryAllocateInfo memAllocInfo = vks::initializers::GenMemoryAllocateInfo();
		VkMemoryRequirements memReqs;

		// Use a separate command buffer for texture loading
		VkCommandBuffer copyCmd = device->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		if (useStaging)
		{
			//Create a host-visible staging buffer that contains the raw image data
			VkBuffer stagingBuffer;
			VkDeviceMemory stagingMemory;

			VkBufferCreateInfo bufferCreateInfo = vks::initializers::GenBufferCreateInfo();
			bufferCreateInfo.size = ktxTextureSize;
			//This buffer is used as a transfer source for the buffer copy
			bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

			VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

			//Get memory requirements for the staging buffer(alignment,memory type bits)
			vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);

			memAllocInfo.allocationSize = memReqs.size;
			//Get memory type index for a host visible buffer
			memAllocInfo.memoryTypeIndex = device->GetMemoryType(memReqs.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
			VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

			//Copy texture data into staging buffer
			uint8_t * data;
			VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
			memcpy(data, ktxTextureData, ktxTextureSize);
			vkUnmapMemory(device->logicalDevice, stagingMemory);

			// Setup buffer copy regions for each mip level
			std::vector<VkBufferImageCopy> bufferCopyRegions;

			for (uint32_t i = 0;i<mipLevels;++i)
			{
				ktx_size_t offset;

				KTX_error_code result = ktxTexture_GetImageOffset(pKtxTexture, i, 0, 0, &offset);
				assert(result == KTX_SUCCESS);

				VkBufferImageCopy bufferCopyRegion = {};
				bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				bufferCopyRegion.imageSubresource.mipLevel = i;
				bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
				bufferCopyRegion.imageSubresource.layerCount = 1;
				bufferCopyRegion.imageExtent.width = (std::max)(1u, pKtxTexture->baseWidth >> i);
				bufferCopyRegion.imageExtent.height = (std::max)(1u, pKtxTexture->baseHeight >> i);
				bufferCopyRegion.imageExtent.depth = 1;
				bufferCopyRegion.bufferOffset = offset;

				bufferCopyRegions.push_back(bufferCopyRegion);
			}

			//Create optimal tiled target image
			VkImageCreateInfo imageCreateInfo = vks::initializers::GenImageCreateInfo();
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.mipLevels = mipLevels;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			imageCreateInfo.extent = { width,height,1 };
			imageCreateInfo.usage = imageUsageFlags|VK_IMAGE_USAGE_TRANSFER_DST_BIT;//Ensure that the TRANSFER_DST bit is set staging
			VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));

			vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);
			memAllocInfo.allocationSize = memReqs.size;
			memAllocInfo.memoryTypeIndex = device->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
			VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

			VkImageSubresourceRange subresourceRange = {};
			subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = mipLevels;
			subresourceRange.layerCount = 1;

			// Image barrier for optimal image(target)
			// Optimal image will be used as destination for the copy
			vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);

			//Copy mip levels from staging buffer
			vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());

			//Change texture image layout to shader read after all mip levels have been copied
			this->imageLayout = imageLayout;
			vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, imageLayout, subresourceRange);

			device->FlushCommandBuffer(copyCmd, copyQueue);

			//�Ѿ���ɣ�����������
			//Clean up staging resources
			vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
			vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);
		}
		else
		{
			//Prefer using optimal tiling, as linear tiling may support only a small set of features
			// depending on implementation(e.g. no mip maps, only one layer,etc.)

			//Checck if this support is supported for linear tiling
			assert(formatProperties.linearTilingFeatures&VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
			
			VkImage mappableImage;
			VkDeviceMemory mappableMemory;

			VkImageCreateInfo imageCreateInfo = vks::initializers::GenImageCreateInfo();
			imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
			imageCreateInfo.format = format;
			imageCreateInfo.extent = { width,height,1 };
			imageCreateInfo.mipLevels = 1;
			imageCreateInfo.arrayLayers = 1;
			imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			imageCreateInfo.tiling = VK_IMAGE_TILING_LINEAR;
			imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			//Load mip map level 0 to linear tiling image
			VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &mappableImage));

			//Get memory requirements for this image like size and alignment
			vkGetImageMemoryRequirements(device->logicalDevice, mappableImage, &memReqs);
			// Set memory allocation size to required memory size
			memAllocInfo.allocationSize = memReqs.size;

			// Get memory type that can be mapped to host memory
			memAllocInfo.memoryTypeIndex = device->GetMemoryType(memReqs.memoryTypeBits,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

			//Allocate host memory for image
			VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &mappableMemory));
			//Bind allocated image for use
			VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, mappableImage, mappableMemory, 0));

			//Get sub resource layout mip map count,array layer.etc.
			VkImageSubresource subRes = {};
			subRes.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			subRes.mipLevel = 0;

			VkSubresourceLayout subResLayout;
			void* data;

			//Get sub resources layout includes row pitch,size offsets,etc
			vkGetImageSubresourceLayout(device->logicalDevice, mappableImage, &subRes, &subResLayout);

			//Map image memory
			VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, mappableMemory, 0, memReqs.size, 0, &data));

			//Copy image data into memory
			memcpy(data, ktxTextureData, memReqs.size);

			vkUnmapMemory(device->logicalDevice, mappableMemory);

			// Linear tiled images don't need to be staged
			// and can be directly used as textures
			image = mappableImage;
			deviceMemory = mappableMemory;
			this->imageLayout = imageLayout;

			// Setup image memory barrier
			vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, imageLayout);
			device->FlushCommandBuffer(copyCmd, copyQueue);
		}

		ktxTexture_Destroy(pKtxTexture);

		//Create a default sampler
		VkSamplerCreateInfo samplerCreateInfo = {};
		samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.mipLodBias = 0.0f;
		samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerCreateInfo.minLod = 0.0f;
		// Max level-of-detail should match mip level count
		samplerCreateInfo.maxLod = (useStaging) ? (float)mipLevels : 0.0f;
		// Only enable anisotropic filtering if enabled on the device
		samplerCreateInfo.maxAnisotropy = device->m_enabledDeviceFeatures.samplerAnisotropy ? device->properties.limits.maxSamplerAnisotropy : 1.0f;
		samplerCreateInfo.anisotropyEnable = device->m_enabledDeviceFeatures.samplerAnisotropy;
		samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr, &sampler));

		// Create image view
		// Textures are not directly accessed by the shaders and are abstracted by image views containing additional
		// information and sub resource ranges
		VkImageViewCreateInfo viewCreateInfo = {};
		viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCreateInfo.format = format;
		viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A };

		// Linear tiling usually won't support mip maps
		// Only set mip map count if optimal tiling is used
		viewCreateInfo.subresourceRange.layerCount = (useStaging) ? mipLevels : 1;
		viewCreateInfo.image = image;
		VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr, &view));

		//Update descriptor image info member that can be used for setting up descriptor sets
		updateDescriptor();

	}	//Texture2D::loadFromFile

	void Texture2D::fromBuffer(void * buffer, VkDeviceSize bufferSize, VkFormat format, uint32_t texWidth, uint32_t texHeight, vks::VulkanDevice * device, VkQueue copyQueue, VkFilter filter, VkImageUsageFlags imageUsageFlags, VkImageLayout imageLayout)
	{
		assert(buffer);

		this->device = device;
		width = texWidth;
		height = texHeight;
		mipLevels = 1;

		VkMemoryAllocateInfo memAllocInfo = vks::initializers::GenMemoryAllocateInfo();
		VkMemoryRequirements memReqs;

		// Use a separate command buffer for texture loading
		VkCommandBuffer copyCmd = device->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// Create a host-visible staging buffer that contains the raw image data
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkBufferCreateInfo bufferCreateInfo = vks::initializers::GenBufferCreateInfo();
		bufferCreateInfo.size = bufferSize;
		//This buffer is used as a transfer source for the buffer copy
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

		//Get memory requirements for the staging buffer(alignment,memory type bits)
		vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);

		memAllocInfo.allocationSize = memReqs.size;
		//Get memory type index for a host visible buffer
		memAllocInfo.memoryTypeIndex = device->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
		VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

		// Copy texture data into staging buffer
		uint8_t *data;
		VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
		memcpy(data, buffer, bufferSize);
		vkUnmapMemory(device->logicalDevice, stagingMemory);

		VkBufferImageCopy bufferCopyRegion = {};
		bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		bufferCopyRegion.imageSubresource.mipLevel = 0;
		bufferCopyRegion.imageSubresource.baseArrayLayer = 0;
		bufferCopyRegion.imageSubresource.layerCount = 1;
		bufferCopyRegion.imageExtent.width = width;
		bufferCopyRegion.imageExtent.height = height;
		bufferCopyRegion.imageExtent.depth = 1;
		bufferCopyRegion.bufferOffset = 0;

		// Create optimal tiled target image
		VkImageCreateInfo imageCreateInfo = vks::initializers::GenImageCreateInfo();
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = mipLevels;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { width,height,1 };
		imageCreateInfo.usage = imageUsageFlags;
		//Ensure that the TRANSFER_DST bit is set for staging
		if ( !(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) )
		{
			imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}
		VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));

		vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);

		memAllocInfo.allocationSize = memReqs.size;

		memAllocInfo.memoryTypeIndex = device->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = mipLevels;
		subresourceRange.layerCount = 1;

		// Image barrier for optimal image (target)
		// Optimal image will be used as destination for the copy
		vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);

		// Copy mip levels from staging buffer
		vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bufferCopyRegion);

		// Change texture image layout to shader read after all mip levels have been copied
		this->imageLayout = imageLayout;
		vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, imageLayout, subresourceRange);

		device->FlushCommandBuffer(copyCmd, copyQueue);

		//Clean up staging resources
		vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
		vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

		//Create sampler
		VkSamplerCreateInfo samplerCreateInfo = {};
		samplerCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		samplerCreateInfo.magFilter = filter;
		samplerCreateInfo.minFilter = filter;
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerCreateInfo.minLod = 0.0f;
		samplerCreateInfo.maxLod = 0.0f;
		samplerCreateInfo.maxAnisotropy = 1.0f;
		VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr, &sampler));

		//Create image view
		VkImageViewCreateInfo viewCreateInfo = {};
		viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewCreateInfo.pNext = NULL;
		viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCreateInfo.format = format;
		viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A };
		viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1 };
		viewCreateInfo.image = image;
		VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr, &view));

		//Update descriptor image info member that can be used for setting up descriptor sets
		updateDescriptor();
	}

	void Texture2DArray::loadFromFile(std::string fileName, VkFormat format, vks::VulkanDevice * device, VkQueue copyQueue, VkImageUsageFlags imageUsageFlags, VkImageLayout imageLayout)
	{
		ktxTexture* pKtxTexture;
		ktxResult result = loadKTXFile(fileName, &pKtxTexture);
		assert(result == KTX_SUCCESS);

		this->device = device;
		width = pKtxTexture->baseWidth;
		height = pKtxTexture->baseHeight;
		layerCount = pKtxTexture->numLayers;
		mipLevels = pKtxTexture->numLevels;

		ktx_uint8_t *pKtxTextureData = ktxTexture_GetData(pKtxTexture);
		ktx_size_t ktxTextureSize = ktxTexture_GetSize(pKtxTexture);

		VkMemoryAllocateInfo memAllocInfo = vks::initializers::GenMemoryAllocateInfo();
		VkMemoryRequirements memReqs;

		//Create a host-visible staging buffer that contains the raw image data
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;

		VkBufferCreateInfo bufferCreateInfo = vks::initializers::GenBufferCreateInfo();
		bufferCreateInfo.size = ktxTextureSize;
		//This buffer is used as a transfer source for the buffer copy
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.usage = VK_SHARING_MODE_EXCLUSIVE;

		VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

		//Get memory requirements for the staging buffer(alignment, memory type bits)
		vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);

		memAllocInfo.allocationSize = memReqs.size;
		// Get memory type index for a host visible buffer
		memAllocInfo.memoryTypeIndex = device->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &stagingMemory));
		VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));


		//Copy texture data into staging buffer
		uint8_t *data;
		VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void**)&data));
		memcpy(data, pKtxTextureData, ktxTextureSize);
		vkUnmapMemory(device->logicalDevice, stagingMemory);

		// Setup buffer copy regions for each layer including all of its miplevels
		std::vector<VkBufferImageCopy> bufferCopyRegions;
		for (uint32_t layer = 0;layer<layerCount;layer++)
		{
			for (uint32_t level = 0;level<mipLevels;level++)
			{
				ktx_size_t offset;
				KTX_error_code result = ktxTexture_GetImageOffset(pKtxTexture, level, layer, 0, &offset);
				assert(result == KTX_SUCCESS);

				VkBufferImageCopy bufferCopyRegion = {};
				bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				bufferCopyRegion.imageSubresource.mipLevel = level;
				bufferCopyRegion.imageSubresource.baseArrayLayer = layer;
				bufferCopyRegion.imageSubresource.layerCount = 1;
				bufferCopyRegion.imageExtent.width = pKtxTexture->baseWidth >> level;
				bufferCopyRegion.imageExtent.height = pKtxTexture->baseHeight >> level;
				bufferCopyRegion.imageExtent.depth = 1;
				bufferCopyRegion.bufferOffset = offset;

				bufferCopyRegions.push_back(bufferCopyRegion);
			}
		}

		//Create optimal tiled target image
		VkImageCreateInfo imageCreateInfo = vks::initializers::GenImageCreateInfo();
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { width,height,1 };
		imageCreateInfo.usage = imageUsageFlags;

		// Ensure that the TRANSFER_DST bit is set for staging
		if ( !(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) )
		{
			imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}
		imageCreateInfo.arrayLayers = layerCount;
		imageCreateInfo.mipLevels = mipLevels;

		VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));

		vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);

		memAllocInfo.allocationSize = memReqs.size;
		memAllocInfo.memoryTypeIndex = device->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocInfo, nullptr, &deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

		// Use a separate command buffer for texture loading
		VkCommandBuffer copyCmd = device->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// Image barrier for optimal image (target)
		// Set initial layout for all array layers (faces) of the optimal (target) tiled texture
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = mipLevels;
		subresourceRange.layerCount = layerCount;

		vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);

		// Copy the layers and mip levels from the staging buffer to the optimal tiled image
		vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());

		this->imageLayout = imageLayout;
		vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, imageLayout, subresourceRange);

		device->FlushCommandBuffer(copyCmd, copyQueue);

		// Create sampler
		VkSamplerCreateInfo samplerCreateInfo = vks::initializers::GenSamplerCreateInfo();
		samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.addressModeV = samplerCreateInfo.addressModeU;
		samplerCreateInfo.addressModeW = samplerCreateInfo.addressModeU;
		samplerCreateInfo.mipLodBias = 0.0f;
		samplerCreateInfo.maxAnisotropy = device->m_enabledDeviceFeatures.samplerAnisotropy ? device->properties.limits.maxSamplerAnisotropy : 1.0f;
		samplerCreateInfo.anisotropyEnable = device->m_enabledDeviceFeatures.samplerAnisotropy;
		samplerCreateInfo.compareOp = VK_COMPARE_OP_NEVER;
		samplerCreateInfo.minLod = 0.0f;
		samplerCreateInfo.maxLod = (float)mipLevels;
		samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr, &sampler));

		// Create image view
		VkImageViewCreateInfo viewCreateInfo = vks::initializers::GenImageViewCreateInfo();
		viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewCreateInfo.format = format;
		viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A };
		viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1 };
		viewCreateInfo.subresourceRange.layerCount = layerCount;
		viewCreateInfo.subresourceRange.levelCount = mipLevels;
		viewCreateInfo.image = image;
		VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr, &view));

		// Clean up staging resources
		ktxTexture_Destroy(pKtxTexture);
		vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
		vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

		// Update descriptor image info member that can be used for setting up descriptor sets
		updateDescriptor();
	}//loadFromFile

	void TextureCubeMap::loadFromFile(std::string fileName, VkFormat format, vks::VulkanDevice * device, VkQueue copyQueue, VkImageUsageFlags imageUsageFlags, VkImageLayout imageLayout)
	{
		ktxTexture* pKtxTexture;
		ktxResult result = loadKTXFile(fileName, &pKtxTexture);
		assert(result == KTX_SUCCESS);

		this->device = device;
		width = pKtxTexture->baseWidth;
		height = pKtxTexture->baseHeight;
		mipLevels = pKtxTexture->numLevels;

		ktx_uint8_t * pKtxTextureData = ktxTexture_GetData(pKtxTexture);
		ktx_size_t ktxTexureSize = ktxTexture_GetSize(pKtxTexture);

		VkMemoryAllocateInfo memAllocateInfo = vks::initializers::GenMemoryAllocateInfo();
		VkMemoryRequirements memReqs;

		// Create a host-visible staging buffer that contains the raw image data
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingMemory;
		VkBufferCreateInfo bufferCreateInfo = vks::initializers::GenBufferCreateInfo();
		bufferCreateInfo.size = ktxTexureSize;
		// This buffer is used as a transfer source for the buffer copy
		bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VK_CHECK_RESULT(vkCreateBuffer(device->logicalDevice, &bufferCreateInfo, nullptr, &stagingBuffer));

		// Get memory requirements for the staging buffer(alignments,memory type bits)
		vkGetBufferMemoryRequirements(device->logicalDevice, stagingBuffer, &memReqs);
		memAllocateInfo.allocationSize = memReqs.size;
		// Get memory type index for a host visible buffer
		memAllocateInfo.memoryTypeIndex = device->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocateInfo, nullptr, &stagingMemory));
		VK_CHECK_RESULT(vkBindBufferMemory(device->logicalDevice, stagingBuffer, stagingMemory, 0));

		// Copy texture data into staging buffer
		uint8_t *data;
		VK_CHECK_RESULT(vkMapMemory(device->logicalDevice, stagingMemory, 0, memReqs.size, 0, (void **)&data));
		memcpy(data, pKtxTextureData, ktxTexureSize);
		vkUnmapMemory(device->logicalDevice, stagingMemory);

		// Setup buffer copy regions for each face including all of its mip levels.
		std::vector<VkBufferImageCopy> bufferCopyRegions;

		for (uint32_t face = 0;face<6;++face)
		{
			for (uint32_t level = 0;level<mipLevels;++level)
			{
				ktx_size_t offset;
				KTX_error_code result = ktxTexture_GetImageOffset(pKtxTexture, level, 0, face, &offset);
				assert(result == KTX_SUCCESS);

				VkBufferImageCopy bufferCopyRegion = {};
				bufferCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				bufferCopyRegion.imageSubresource.mipLevel = level;
				bufferCopyRegion.imageSubresource.baseArrayLayer = face;
				bufferCopyRegion.imageSubresource.layerCount = 1;
				bufferCopyRegion.imageExtent.width = pKtxTexture->baseWidth >> level;
				bufferCopyRegion.imageExtent.height = pKtxTexture->baseHeight >> level;
				bufferCopyRegion.imageExtent.depth = 1;
				bufferCopyRegion.bufferOffset = offset;

				bufferCopyRegions.push_back(bufferCopyRegion);
			}//for
		}//for

		// Create optimal tiled target image
		VkImageCreateInfo imageCreateInfo = vks::initializers::GenImageCreateInfo();
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.mipLevels = mipLevels;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageCreateInfo.extent = { width,height,1 };
		imageCreateInfo.usage = imageUsageFlags;

		// Ensure that the TRANSFER_DST bit is set for staging
		if (!(imageCreateInfo.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT))
		{
			imageCreateInfo.usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}

		//Cube faces count as array layers in Vulkan
		imageCreateInfo.arrayLayers = 6;
		// This flag is required for cube map images
		imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

		VK_CHECK_RESULT(vkCreateImage(device->logicalDevice, &imageCreateInfo, nullptr, &image));

		vkGetImageMemoryRequirements(device->logicalDevice, image, &memReqs);
		memAllocateInfo.allocationSize = memReqs.size;
		memAllocateInfo.memoryTypeIndex = device->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		VK_CHECK_RESULT(vkAllocateMemory(device->logicalDevice, &memAllocateInfo, nullptr, &deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device->logicalDevice, image, deviceMemory, 0));

		// Use a separate command buffer for texture loading
		VkCommandBuffer copyCmd = device->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);

		// Image barrier for optimal image (target)
		// Set initial layout for all array layers (faces) of the otimal (target) tiled texture
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = mipLevels;
		subresourceRange.layerCount = 6;

		vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);

		// Copy the cube map faces from the staging buffer to the optimal tiled image
		vkCmdCopyBufferToImage(copyCmd, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());

		// Change texture image layout to shader read after all faces have been copied
		this->imageLayout = imageLayout;
		vks::tools::setImageLayout(copyCmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, imageLayout, subresourceRange);

		device->FlushCommandBuffer(copyCmd, copyQueue);

		VkSamplerCreateInfo samplerCreateInfo = vks::initializers::GenSamplerCreateInfo();
		samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.minFilter = VK_FILTER_LINEAR;
		samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		samplerCreateInfo.mipLodBias = 0.0f;
		samplerCreateInfo.maxAnisotropy = device->m_enabledDeviceFeatures.samplerAnisotropy ? device->properties.limits.maxSamplerAnisotropy : 1.0f;
		samplerCreateInfo.anisotropyEnable = device->m_enabledDeviceFeatures.samplerAnisotropy;
		samplerCreateInfo.minLod = 0.0f;
		samplerCreateInfo.maxLod = (float)mipLevels;
		samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device->logicalDevice, &samplerCreateInfo, nullptr, &sampler));

		// Create image view
		VkImageViewCreateInfo viewCreateInfo = vks::initializers::GenImageViewCreateInfo();
		viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		viewCreateInfo.format = format;
		viewCreateInfo.components = { VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A };
		viewCreateInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1 };
		viewCreateInfo.subresourceRange.layerCount = 6;
		viewCreateInfo.subresourceRange.levelCount = mipLevels;
		viewCreateInfo.image = image;
		VK_CHECK_RESULT(vkCreateImageView(device->logicalDevice, &viewCreateInfo, nullptr, &view));

		// Clean up staging resources
		ktxTexture_Destroy(pKtxTexture);
		vkFreeMemory(device->logicalDevice, stagingMemory, nullptr);
		vkDestroyBuffer(device->logicalDevice, stagingBuffer, nullptr);

		// Update descriptor image info member that can be used for setting up descriptor sets
		updateDescriptor();
	}

}