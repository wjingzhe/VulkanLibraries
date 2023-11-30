/*
* Vulkan Example - Using subpasses for G-Buffer compositing
*
* Copyright (C) 2016-2017 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*
* Summary:
* Implements a deferred rendering setup with a forward transparency pass using sub passes
*
* Sub passes allow reading from the previous framebuffer (in the same render pass) at
* the same pixel position.
*
* This is a feature that was especially designed for tile-based-renderers
* (mostly mobile GPUs) and is a new optimization feature in Vulkan for those GPU types.
*
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION true

#define NUM_LIGHTS 64

class VulkanExample :public VulkanExampleBase
{
public:
	VulkanExample():VulkanExampleBase(ENABLE_VALIDATION)
	{
		windowTitle = "Subpasses";
		camera.cameraType = Camera::CameraType::firstperson;
		camera.movementSpeed = 5.0f;
#ifndef __ANDROID__
		camera.rotationSpeed = 0.25f;
#endif // !__ANDROID__
		camera.setPosition(glm::vec3(-3.2f, 1.0f, 5.9f));
		camera.setRotation(glm::vec3(0.5f, 210.0f, 0.0f));
		camera.setPerspective(60.f, (float)width / (float)height, 0.1f, 256.0f);
		uiOverlay.subpass = 2;
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources
		// Note: inherited destructor cleans up resources stored in base class
		vkDestroyPipeline(device, pipelines.offscreen, nullptr);
		vkDestroyPipeline(device, pipelines.composition, nullptr);
		vkDestroyPipeline(device, pipelines.tranparent, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayouts.offscreen, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayouts.composition, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayouts.transparent, nullptr);

		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.composition, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.transparent, nullptr);

		clearAttachment(&attachments.positionAttachment);
		clearAttachment(&attachments.normalAttachment);
		clearAttachment(&attachments.albedoAttachment);

		textures.glass.destroy();
		uniformBuffers.mvpBuffer.destroy();
		uniformBuffers.lights.destroy();
	}

	// Enable physical device features required for this example
	virtual void getEnabledFeatures() override
	{
		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy) {
			curEnabledDeviceFeatures.samplerAnisotropy = VK_TRUE;
		}
	};

public:
	struct FrameBufferAttachment
	{
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory deviceMemory = VK_NULL_HANDLE;
		VkImageView imageView = VK_NULL_HANDLE;
		VkFormat format;
	};

	struct Attachments
	{
		FrameBufferAttachment positionAttachment, normalAttachment, albedoAttachment;
		int32_t width;
		int32_t height;
	} attachments;

	struct
	{
		vkglTF::Model scene;
		vkglTF::Model tranparent;
	} models;

	struct
	{
		vks::Texture2D glass;
	} textures;

	struct Light
	{
		glm::vec4 position;
		glm::vec3 color;
		float radius;
	};

	struct
	{
		glm::vec4 viewPos;
		Light lights[NUM_LIGHTS];
	} uboLights;

	struct
	{
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
	} uboMvp;

	struct
	{
		vks::Buffer mvpBuffer;
		vks::Buffer lights;
	} uniformBuffers;

	struct 
	{
		VkDescriptorSetLayout scene;
		VkDescriptorSetLayout composition;
		VkDescriptorSetLayout transparent;
	} descriptorSetLayouts;

	struct
	{
		VkDescriptorSet sceneDescriptorSet;
		VkDescriptorSet compositionDescriptorSet;
		VkDescriptorSet transparentDescriptorSet;
	} descriptorSets;

	struct
	{
		VkPipelineLayout offscreen;
		VkPipelineLayout composition;
		VkPipelineLayout transparent;
	} pipelineLayouts;

	struct
	{
		VkPipeline offscreen;
		VkPipeline composition;
		VkPipeline tranparent;
	} pipelines;

	void clearAttachment(FrameBufferAttachment * pAttachment)
	{
		vkDestroyImageView(device,pAttachment->imageView,nullptr);
		vkDestroyImage(device, pAttachment->image, nullptr);
		vkFreeMemory(device, pAttachment->deviceMemory, nullptr);
	}

	// Create a frame buffer attachment
	void createAttachment(VkFormat format,VkImageUsageFlags usage,FrameBufferAttachment * pAttachment)
	{
		if (pAttachment->image!=VK_NULL_HANDLE)
		{
			clearAttachment(pAttachment);
		}

		VkImageAspectFlags aspectMask = 0;
		VkImageLayout imageLayout;

		pAttachment->format = format;

		if (usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		if (usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
		}

		assert(aspectMask > 0);

		VkImageCreateInfo imageCI = vks::initializers::GenImageCreateInfo();
		imageCI.imageType = VK_IMAGE_TYPE_2D;
		imageCI.format = format;
		imageCI.extent.width = width;
		imageCI.extent.height = height;
		imageCI.extent.depth = 1;
		imageCI.mipLevels = 1;
		imageCI.arrayLayers = 1;
		imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
		// VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT flag is required for input attachments
		imageCI.usage = usage | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VkMemoryAllocateInfo memAlloc = vks::initializers::GenMemoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &pAttachment->image));
		vkGetImageMemoryRequirements(device, pAttachment->image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &pAttachment->deviceMemory));
		VK_CHECK_RESULT(vkBindImageMemory(device, pAttachment->image, pAttachment->deviceMemory, 0));

		VkImageViewCreateInfo  imageViewCI = vks::initializers::GenImageViewCreateInfo();
		imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCI.format = format;
		imageViewCI.subresourceRange = {};
		imageViewCI.subresourceRange.aspectMask = aspectMask;
		imageViewCI.subresourceRange.baseMipLevel = 0;
		imageViewCI.subresourceRange.levelCount = 1;
		imageViewCI.subresourceRange.baseArrayLayer = 0;
		imageViewCI.subresourceRange.layerCount = 1;
		imageViewCI.image = pAttachment->image;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCI, nullptr, &pAttachment->imageView));
	}

	// Create color attachments for the G-Buffer components
	void createGBufferAttachments()
	{
		createAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &attachments.positionAttachment);	// (World space) Positions
		createAttachment(VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &attachments.normalAttachment);	// (World space) Normals
		createAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &attachments.albedoAttachment); // Albedo (Color)
	}

	// Override render pass setup from base class
	void setupRenderPass() override
	{
		attachments.width = width;
		attachments.height = height;

		createGBufferAttachments();

		std::array<VkAttachmentDescription, 5> attachmentDescriptions{};

		// Swap Chain Color attachments
		attachmentDescriptions[0].format = swapChain.colorFormat;
		attachmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		// Deferred attachments
		// Position
		attachmentDescriptions[1].format = this->attachments.positionAttachment.format;
		attachmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		// Normals
		attachmentDescriptions[2].format = this->attachments.normalAttachment.format;
		attachmentDescriptions[2].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescriptions[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescriptions[2].finalLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		// Albedo
		attachmentDescriptions[3].format = this->attachments.albedoAttachment.format;
		attachmentDescriptions[3].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescriptions[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescriptions[3].finalLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL;
		// Depth attachment
		attachmentDescriptions[4].format = depthFormat;
		attachmentDescriptions[4].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescriptions[4].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescriptions[4].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		// Three subpasses
		std::array<VkSubpassDescription, 3>subpassesDescriptions{};

		VkAttachmentReference depthReference = { 4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		// First subpass: Fill G-Buffer components
		//---------------------------------------------------------------------
		{
			VkAttachmentReference colorReference[4];
			colorReference[0] = { 0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			colorReference[1] = { 1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			colorReference[2] = { 2,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			colorReference[3] = { 3,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };

			subpassesDescriptions[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpassesDescriptions[0].colorAttachmentCount = 4;
			subpassesDescriptions[0].pColorAttachments = colorReference;
			subpassesDescriptions[0].pDepthStencilAttachment = &depthReference;
		}

		// Second subpass: Final composition (using G-Buffer components)
		//-------------------------------------------------------------------------
		{
			VkAttachmentReference colorReference = { 0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			VkAttachmentReference inputReference[3];
			inputReference[0] = { 1,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			inputReference[1] = { 2,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
			inputReference[2] = { 3,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

			subpassesDescriptions[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpassesDescriptions[1].colorAttachmentCount = 1;
			subpassesDescriptions[1].pColorAttachments = &colorReference;
			subpassesDescriptions[1].pDepthStencilAttachment = &depthReference;
			// Use the color attachments filled in the first pass as input attachments
			subpassesDescriptions[1].inputAttachmentCount = 3;
			subpassesDescriptions[1].pInputAttachments = inputReference;
		}

		// Third subpass: Forward transparency
		{
			VkAttachmentReference colorReference = { 0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
			VkAttachmentReference inputReference = { 1,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };//把Position buffer当作sceneColor用了

			subpassesDescriptions[2].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpassesDescriptions[2].colorAttachmentCount = 1;
			subpassesDescriptions[2].pColorAttachments = &colorReference;
			subpassesDescriptions[2].pDepthStencilAttachment = &depthReference;
			subpassesDescriptions[2].inputAttachmentCount = 1;
			subpassesDescriptions[2].pInputAttachments = &inputReference;
		}

		// Subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 4> dependencies;

		// 从present等阶段转化为G-Buffer绘制对象
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// This dependency transitions the input attachment from color attachment to shader read
		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = 1;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		//转化到可绘制Transparenncy，只要符合转化条件即可，不需要两个pass完全同步
		dependencies[2].srcSubpass = 1;
		dependencies[2].dstSubpass = 2;
		dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[3].srcSubpass = 2;
		dependencies[3].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[3].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[3].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT| VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;//jingz todo 何时Read何时Write
		dependencies[3].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[3].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassCreateInfo = {};
		renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
		renderPassCreateInfo.pAttachments = attachmentDescriptions.data();
		renderPassCreateInfo.subpassCount = static_cast<uint32_t>(subpassesDescriptions.size());
		renderPassCreateInfo.pSubpasses = subpassesDescriptions.data();
		renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassCreateInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &renderPass));
	}

	// Override framebuffer setup from base class,will automatically be called upon setup and if a window is resized
	void setupFrameBuffer() override
	{
		//if the window is resized, all the framebuffer/attachments used in our composition passes need to be recreated
		if (attachments.width != width || attachments.height != height)
		{
			attachments.width = width;
			attachments.height = height;
			createGBufferAttachments();

			// Since the framebuffers/attachments are referred in the descriptor sets,these need tto be updated too
			std::vector<VkDescriptorImageInfo> tempDescriptorImageInfos =
			{
				vks::initializers::GenDescriptorImageInfo(VK_NULL_HANDLE,attachments.positionAttachment.imageView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
				vks::initializers::GenDescriptorImageInfo(VK_NULL_HANDLE,attachments.normalAttachment.imageView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
				vks::initializers::GenDescriptorImageInfo(VK_NULL_HANDLE,attachments.albedoAttachment.imageView,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			};

			// Composition pass
			std::vector<VkWriteDescriptorSet> writeDescriptorSets;
			for (size_t i = 0; i < tempDescriptorImageInfos.size(); i++)
			{
				writeDescriptorSets.push_back(
					vks::initializers::GenWriteDescriptorSet(descriptorSets.compositionDescriptorSet, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, i, &tempDescriptorImageInfos[i])
				);
			}//for

			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

			// Tranparent Forward pass //顶点UOB的binding是0，FS对应的是1，和其他pass不一样，每个pipeline自己有自己的下标
			writeDescriptorSets = { vks::initializers::GenWriteDescriptorSet(descriptorSets.transparentDescriptorSet,VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT ,1,&tempDescriptorImageInfos[0]), };
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}//if resized

		VkImageView attachments[5];

		VkFramebufferCreateInfo frameBufferCreateInfo{};
		frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferCreateInfo.renderPass = renderPass;
		frameBufferCreateInfo.attachmentCount = 5;
		frameBufferCreateInfo.pAttachments = attachments;
		frameBufferCreateInfo.width = width;
		frameBufferCreateInfo.height = height;
		frameBufferCreateInfo.layers = 1;

		// Create frame buffer for every swap chain image
		frameBuffers.resize(swapChain.imageCount);
		for (uint32_t i = 0; i < frameBuffers.size(); i++)
		{
			attachments[0] = swapChain.buffers[i].view;
			attachments[1] = this->attachments.positionAttachment.imageView;
			attachments[2] = this->attachments.normalAttachment.imageView;
			attachments[3] = this->attachments.albedoAttachment.imageView;
			attachments[4] = depthStencil.view;
			VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &frameBuffers[i]));
		}//for
	}//setupFrameBuffer

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.scene.loadFromFile(getAssetPath() + "models/samplebuilding.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.tranparent.loadFromFile(getAssetPath() + "models/samplebuilding_glass.gltf", vulkanDevice, queue, glTFLoadingFlags);
		textures.glass.loadFromFile(getAssetPath() + "textures/colored_glass_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	void initLights()
	{
		std::vector<glm::vec3> colors =
		{
			glm::vec3(1.0f, 1.0f, 1.0f),
			glm::vec3(1.0f, 0.0f, 0.0f),
			glm::vec3(0.0f, 1.0f, 0.0f),
			glm::vec3(0.0f, 0.0f, 1.0f),
			glm::vec3(1.0f, 1.0f, 0.0f),
		};

		std::default_random_engine rndGen(benchmark.active ? 0 : (unsigned)time(nullptr));
		std::uniform_real_distribution<float> rndDist(-1.0f, 1.0f);
		std::uniform_int_distribution<uint32_t> rndCol(0, static_cast<uint32_t>(colors.size() - 1));

		for (auto& light : uboLights.lights)
		{
			light.position = glm::vec4(rndDist(rndGen) * 6.0f, 0.25f + std::abs(rndDist(rndGen)) * 4.0f, rndDist(rndGen) * 6.0f, 1.0f);
			light.color = colors[rndCol(rndGen)];
			light.radius = 1.0f + std::abs(rndDist(rndGen));
		}//for
	}//initLights

	void updateUniformBufferDeferredMatrices()
	{
		uboMvp.projection = camera.matrices.perspective;
		uboMvp.view = camera.matrices.view;
		uboMvp.model = glm::mat4(1.0f);

		VK_CHECK_RESULT(uniformBuffers.mvpBuffer.map());
		memcpy(uniformBuffers.mvpBuffer.mappedData, &uboMvp, sizeof(uboMvp));
		uniformBuffers.mvpBuffer.unmap();
	}

	// Update fragment shader light position uniform block
	void updateUniformBufferDeferredLights()
	{
		// Current view position
		uboLights.viewPos = glm::vec4(camera.position, 0.0f)*glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);

		VK_CHECK_RESULT(uniformBuffers.lights.map());
		memcpy(uniformBuffers.lights.mappedData, &uboLights, sizeof(uboLights));
		uniformBuffers.lights.unmap();
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Deferred vertex shader
		vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.mvpBuffer, sizeof(uboMvp));

		// Deferred composition fragment shader
		vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.lights, sizeof(uboLights));

		// Update Uniform Buffer data
		updateUniformBufferDeferredMatrices();
		updateUniformBufferDeferredLights();
	}

	void setupDescriptorSetLayoutAndPipelineLayout()
	{
		// Deferred shading layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0: vertex shader uniform buffer
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,0),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayouts.scene));

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayouts.scene, 1);

		// OffScreen (scene) rendering pipeline layout
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.offscreen));
	}

	void preparePipelines()
	{
		std::array < VkPipelineColorBlendAttachmentState, 4>blendAttachmentStates
		{
			vks::initializers::GenPipelineColorBlendAttachmentState(0xf,VK_FALSE),
			vks::initializers::GenPipelineColorBlendAttachmentState(0xf,VK_FALSE),
			vks::initializers::GenPipelineColorBlendAttachmentState(0xf,VK_FALSE),
			vks::initializers::GenPipelineColorBlendAttachmentState(0xf,VK_FALSE),
		};
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::GenPipelineColorBlendStateCreateInfo(static_cast<uint32_t>(blendAttachmentStates.size()), blendAttachmentStates.data());

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::GenPipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::GenPipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::GenPipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::GenPipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::GenPipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = {VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::GenPipelineDynamicStateCreateInfo(dynamicStateEnables);

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderSttages;
		shaderSttages[0] = loadShader(getShaderPath() + "subpasses/gbuffer.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderSttages[1] = loadShader(getShaderPath() + "subpasses/gbuffer.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		// Final fullscreen pass pipeline
		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::GenPipelineCreateInfo(pipelineLayouts.offscreen, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderSttages.size());
		pipelineCI.pStages = shaderSttages.data();
		pipelineCI.subpass = 0;
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position,vkglTF::VertexComponent::Color,vkglTF::VertexComponent::Normal,vkglTF::VertexComponent::UV });

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI,nullptr, &pipelines.offscreen));
	}//preparePipelines

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,4),
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,4),
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,4),
		};

		VkDescriptorPoolCreateInfo descriptorPoolCI = vks::initializers::GenDescriptorPoolCreateInfo(static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), 4);

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCI, nullptr, &descriptorPool));
	}

	void setupDescriptorSetAndUpdate()
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.scene, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.sceneDescriptorSet));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			vks::initializers::GenWriteDescriptorSet(descriptorSets.sceneDescriptorSet,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&uniformBuffers.mvpBuffer.descriptorBufferInfo),
		};

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
	}

	// Create the Vulkan objects used in the composition pass (descritptor sets,pipleines,etc.)
	void prepareCompositionPass()
	{
		// Descriptor set layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0: Position input attachment
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,VK_SHADER_STAGE_FRAGMENT_BIT,0),
			// Binding 1: Normal input attachment
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,VK_SHADER_STAGE_FRAGMENT_BIT,1),
			// Binding 2: Albedo input attachment
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,VK_SHADER_STAGE_FRAGMENT_BIT,2),
			// Binding 3: Light positions
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_FRAGMENT_BIT,3),
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayoutCI = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayouts.composition));

		// Pipeline layout
		VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayouts.composition, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayouts.composition));

		// Descriptor Sets
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.composition, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.compositionDescriptorSet));

		/*
		update descriptor set
		*/
		// Image descriptors for the offscreen color attachments
		VkDescriptorImageInfo texDescriptorPosition = vks::initializers::GenDescriptorImageInfo(VK_NULL_HANDLE, attachments.positionAttachment.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		VkDescriptorImageInfo texDescriptorNormal = vks::initializers::GenDescriptorImageInfo(VK_NULL_HANDLE, attachments.normalAttachment.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
		VkDescriptorImageInfo texDescriptorAlbedo = vks::initializers::GenDescriptorImageInfo(VK_NULL_HANDLE, attachments.albedoAttachment.imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0: Position texture target
			vks::initializers::GenWriteDescriptorSet(descriptorSets.compositionDescriptorSet,VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,0,&texDescriptorPosition,1),
			// Binding 1: Normals texture target
			vks::initializers::GenWriteDescriptorSet(descriptorSets.compositionDescriptorSet,VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,1,&texDescriptorNormal,1),
			// Binding 2: Albedo texture target
			vks::initializers::GenWriteDescriptorSet(descriptorSets.compositionDescriptorSet,VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,2,&texDescriptorAlbedo,1),
			// Binding 3: Fragment shader lights
			vks::initializers::GenWriteDescriptorSet(descriptorSets.compositionDescriptorSet,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,3,&uniformBuffers.lights.descriptorBufferInfo	,1),
		};

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		// Pipeline
		VkPipelineInputAssemblyStateCreateInfo inputAssembleStateCI = vks::initializers::GenPipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::GenPipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::GenPipelineDepthStencilStateCreateInfo(VK_TRUE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::GenPipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::GenPipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::GenPipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::GenPipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::GenPipelineDynamicStateCreateInfo(dynamicStateEnables);
		VkPipelineVertexInputStateCreateInfo emptyInputStateCI{};
		emptyInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0] = loadShader(getShaderPath() + "subpasses/composition.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "subpasses/composition.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		// Use specialization constants to pass number of lights to the shader
		VkSpecializationMapEntry specializationEntry{};
		specializationEntry.constantID = 0;
		specializationEntry.offset = 0;
		specializationEntry.size = sizeof(uint32_t);

		uint32_t specializationData = NUM_LIGHTS;
		VkSpecializationInfo specializationInfo;
		specializationInfo.mapEntryCount = 1;
		specializationInfo.pMapEntries = &specializationEntry;
		specializationInfo.dataSize = sizeof(specializationData);
		specializationInfo.pData = &specializationData;

		shaderStages[1].pSpecializationInfo = &specializationInfo;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::GenPipelineCreateInfo(pipelineLayouts.composition, renderPass, 0);
		pipelineCI.pVertexInputState = &emptyInputStateCI;
		pipelineCI.pInputAssemblyState = &inputAssembleStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		// Index of the subpass that is this pipeline will be uesed in
		pipelineCI.subpass = 1;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.composition));

		/*
			Transparent (forward) pipeline
		*/
		// Descriptor set layout
		setLayoutBindings =
		{
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,0),
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,VK_SHADER_STAGE_FRAGMENT_BIT,1),
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,2),
		};

		descriptorLayoutCI = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayouts.transparent));

		// Pipleine layout
		pipelineLayoutCI = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayouts.transparent, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayouts.transparent));

		// Descriptor sets
		allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.transparent, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.transparentDescriptorSet));

		writeDescriptorSets =
		{
			vks::initializers::GenWriteDescriptorSet(descriptorSets.transparentDescriptorSet,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&uniformBuffers.mvpBuffer.descriptorBufferInfo),
			vks::initializers::GenWriteDescriptorSet(descriptorSets.transparentDescriptorSet,VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,1,&texDescriptorPosition),
			vks::initializers::GenWriteDescriptorSet(descriptorSets.transparentDescriptorSet,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2,&textures.glass.descriptorImageInfo),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		// Enable blending
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position,vkglTF::VertexComponent::Color,vkglTF::VertexComponent::Normal ,vkglTF::VertexComponent::UV });
		pipelineCI.layout = pipelineLayouts.transparent;
		pipelineCI.subpass = 2;

		shaderStages[0] = loadShader(getShaderPath() + "subpasses/transparent.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "subpasses/transparent.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.tranparent));
	}//prepareCompositionPass

	void buildCommandBuffersForPreRenderPrmitives()
	{
		VkCommandBufferBeginInfo cmdBufferBeginInfo = vks::initializers::GenCommandBufferBeginInfo();

		VkClearValue clearValues[5];
		clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[3].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[4].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::GenRenderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 5;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufferBeginInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::GenRect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			VkDeviceSize offsets[1] = { 0 };

			// First sub pass
			// Render the components of the scene to the G-Buffer attachments
			{
				vks::debugmarker::beginRegion(drawCmdBuffers[i], "Subpass 0: Deferred G-Buffer creation", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.offscreen);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.offscreen, 0, 1, &descriptorSets.sceneDescriptorSet, 0, NULL);
				models.scene.draw(drawCmdBuffers[i]);

				vks::debugmarker::endRegion(drawCmdBuffers[i]);
			}// First sub pass

			// Second sub pass
			// This subpass will use the G-Buffer components that have been filled in the first subpass as input attachment for the final compositing
			{
				vks::debugmarker::beginRegion(drawCmdBuffers[i], "Subpass 1: Deferred composition", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

				vkCmdNextSubpass(drawCmdBuffers[i], VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.composition);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.composition, 0, 1, &descriptorSets.compositionDescriptorSet, 0, NULL);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0,0);

				vks::debugmarker::endRegion(drawCmdBuffers[i]);
			}// Second sub pass

			// Third subpass
			// Render transparent geometry using a forward pass that compares against depth generated during G-Buffer fill
			{
				vks::debugmarker::beginRegion(drawCmdBuffers[i], "Subpass 2: Forward transparency", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

				vkCmdNextSubpass(drawCmdBuffers[i], VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.tranparent);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.transparent, 0, 1, &descriptorSets.transparentDescriptorSet, 0, NULL);
				models.tranparent.draw(drawCmdBuffers[i]);

				vks::debugmarker::endRegion(drawCmdBuffers[i]);

			}// Third subpass

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}//for
	}//buildCommandBuffersForPreRenderPrmitives

	void prepareForRendering() override
	{
		VulkanExampleBase::prepareForRendering();
		loadAssets();
		initLights();
		prepareUniformBuffers();
		setupDescriptorSetLayoutAndPipelineLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSetAndUpdate();
		prepareCompositionPass();
		buildCommandBuffersForPreRenderPrmitives();
		prepared = true;
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		// Command buffer to be submitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentCmdBufferIndex];

		// Submit to queue
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();
	}

	virtual void render() override
	{
		if (!prepared)
			return;
		draw();
		if (camera.updated) {
			updateUniformBufferDeferredMatrices();
			updateUniformBufferDeferredLights();
		}
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay) override
	{
		if (overlay->header("Subpasses")) {
			overlay->text("0: Deferred G-Buffer creation");
			overlay->text("1: Deferred composition");
			overlay->text("2: Forward transparency");
		}
		if (overlay->header("Settings")) {
			if (overlay->button("Randomize lights")) {
				initLights();
				updateUniformBufferDeferredLights();
			}
		}
	}

private:

};

VULKAN_EXAMPLE_MAIN()