/*
* Vulkan Example - Using input attachments
*
* Copyright (C) 2018-2021 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*
* Summary:
* Input attachments can be used to read attachment contents from a previous sub pass
* at the same pixel position within a single render pass
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION true

class VulkanExample :public VulkanExampleBase
{
public:
	vkglTF::Model scene;

	struct UBOMatrices
	{
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
	} uboMatrices;

	struct UBOParams
	{
		glm::vec2 brightnessContrast = glm::vec2(0.5f, 1.8f);
		glm::vec2 range = glm::vec2(0.6f, 1.0f);
		int32_t attachmentIndex = 1;
	} uboParams;

	struct
	{
		vks::Buffer matrices;
		vks::Buffer params;
	} uniformBuffers;

	struct
	{
		VkPipeline attachmentWrite;
		VkPipeline attachmentRead;
	} pipelines;

	struct
	{
		VkPipelineLayout attachmentLayoutWrite;
		VkPipelineLayout attachmentLayoutRead;
	} pipelineLayouts;

	struct
	{
		VkDescriptorSet attachmentDescriptorSetWrite;
		std::vector<VkDescriptorSet> attachmentDescriptorSetRead;
	} descriptorSets;

	struct
	{
		VkDescriptorSetLayout attachmentDescriptorSetLayoutWrite;
		VkDescriptorSetLayout attachmentDescriptorSetLayoutRead;
	} descriptorSetLayouts;

	struct FrameBufferAttachment
	{
		VkImage image = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VkFormat format;
	};

	struct FrameAttachments
	{
		FrameBufferAttachment color, depth;
	};
	std::vector<FrameAttachments> attachments; //每个swapchain缓冲区对应一组FrameAttachments
	VkExtent2D attachmentSize;

	const VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;

	VulkanExample():VulkanExampleBase(ENABLE_VALIDATION)
	{
		windowTitle = "Input attachments";
		camera.cameraType = Camera::CameraType::firstperson;
		camera.movementSpeed = 2.5f;
		camera.setPosition(glm::vec3(1.65f, 1.75f, -6.15f));
		camera.setRotation(glm::vec3(-12.75f, 380.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		uiOverlay.subpass = 1;
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources
		// Note : Inherited desctructor clean up resources stored in base class
		for (uint32_t i = 0; i < attachments.size(); ++i)
		{
			vkDestroyImageView(device, attachments[i].color.view, nullptr);
			vkDestroyImage(device, attachments[i].color.image, nullptr);
			vkFreeMemory(device, attachments[i].color.memory, nullptr);

			vkDestroyImageView(device, attachments[i].depth.view, nullptr);
			vkDestroyImage(device, attachments[i].depth.image, nullptr);
			vkFreeMemory(device, attachments[i].depth.memory, nullptr);
		}

		vkDestroyPipeline(device, pipelines.attachmentWrite,nullptr);
		vkDestroyPipeline(device, pipelines.attachmentRead, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayouts.attachmentLayoutWrite, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayouts.attachmentLayoutRead, nullptr);

		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.attachmentDescriptorSetLayoutWrite, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.attachmentDescriptorSetLayoutRead, nullptr);

		uniformBuffers.matrices.destroy();
		uniformBuffers.params.destroy();
	}

	void clearAttachment(FrameBufferAttachment* pFrameBufferAttachment)
	{
		vkDestroyImageView(device, pFrameBufferAttachment->view, nullptr);
		vkDestroyImage(device, pFrameBufferAttachment->image, nullptr);
		vkFreeMemory(device, pFrameBufferAttachment->memory, nullptr);
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentCmdBufferIndex];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VulkanExampleBase::submitFrame();
	}

	// Create a frame buffer attachment
	void createAttachment(VkFormat format,VkImageUsageFlags usage,FrameBufferAttachment *pAttachment)
	{
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
			imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		}
		
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
		// VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT flag is reqiured for input attachments;
		imageCI.usage = usage | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
		imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &pAttachment->image));

		VkMemoryAllocateInfo memAlloc = vks::initializers::GenMemoryAllocateInfo();
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, pAttachment->image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &pAttachment->memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, pAttachment->image, pAttachment->memory, 0));

		VkImageViewCreateInfo imageViewCreateInfo = vks::initializers::GenImageViewCreateInfo();
		imageViewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		imageViewCreateInfo.format = format;
		imageViewCreateInfo.subresourceRange = {};
		imageViewCreateInfo.subresourceRange.aspectMask = aspectMask;
		imageViewCreateInfo.subresourceRange.baseMipLevel = 0;
		imageViewCreateInfo.subresourceRange.levelCount = 1;
		imageViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		imageViewCreateInfo.subresourceRange.layerCount = 1;
		imageViewCreateInfo.image = pAttachment->image;
		VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCreateInfo, nullptr, &pAttachment->view));
	}

	// Override render pass setup from base class
	void setupRenderPass() override
	{
		attachmentSize = { width,height };

		attachments.resize(swapChain.imageCount);
		for (auto i = 0; i < attachments.size(); i++)
		{
			createAttachment(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &attachments[i].color);
			createAttachment(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &attachments[i].depth);
		}


		std::array<VkAttachmentDescription, 3> attachmentDescriptions{};

		// swap chain image color attachment will be transitioned to present layout
		attachmentDescriptions[0].format = swapChain.colorFormat;
		attachmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		// Input attachment
		// These will be written in the first subpass,transitioned to input attachments and then read in the second subpass

		// Color
		attachmentDescriptions[1].format = colorFormat;
		attachmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		// Depth
		attachmentDescriptions[2].format = depthFormat;
		attachmentDescriptions[2].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescriptions[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescriptions[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;


		std::array<VkSubpassDescription, 2> subpassDescriptions{};
		/*
			First subpass
			Fill the color and depth attachments
		*/
		VkAttachmentReference colorReference = { 1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };//这里指定Attachment Index为1，对应SceneColor
		VkAttachmentReference depthReference = { 2,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };//这里指定Attachment Index为2，对应Depth

		subpassDescriptions[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescriptions[0].colorAttachmentCount = 1;
		subpassDescriptions[0].pColorAttachments = &colorReference;
		subpassDescriptions[0].pDepthStencilAttachment = &depthReference;

		/*
			Second subpass
			Input attachment read and swap chain color attachment write
		*/
		// Color reference (target) for this sub pass is the swap chain color attachment
		VkAttachmentReference colorReferenceSwapChain = { 0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };//这里指定Attachment Index为0，对应用于present的swap chain

		subpassDescriptions[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescriptions[1].colorAttachmentCount = 1;
		subpassDescriptions[1].pColorAttachments = &colorReferenceSwapChain;

		// Color and depth attachment written to in first sub pass will be used as input attachments to be read in the fragment shader
		VkAttachmentReference inputReference[2];
		inputReference[0] = { 1,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };
		inputReference[1] = { 2,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL };

		// Use the attachments filled in the first pass as input attachment
		subpassDescriptions[1].inputAttachmentCount = 2;
		subpassDescriptions[1].pInputAttachments = inputReference;

		/*
			Subpass dependencies for layout transitions
		*/
		std::array<VkSubpassDependency, 3> dependencies;

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

		dependencies[2].srcSubpass = 0;
		dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[2].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
		dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[2].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
		dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		VkRenderPassCreateInfo renderPassCreateInfo{};
		renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
		renderPassCreateInfo.pAttachments = attachmentDescriptions.data();
		renderPassCreateInfo.subpassCount = static_cast<uint32_t>(subpassDescriptions.size());
		renderPassCreateInfo.pSubpasses = subpassDescriptions.data();
		renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassCreateInfo.pDependencies = dependencies.data();
		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &renderPass));

	}

	void updateAttachmentReadDescriptors(uint32_t index)
	{
		// Image descriptors for the input attachments read by the shader
		std::vector<VkDescriptorImageInfo> descriptorImageInfos =
		{
			vks::initializers::GenDescriptorImageInfo(VK_NULL_HANDLE,attachments[index].color.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			vks::initializers::GenDescriptorImageInfo(VK_NULL_HANDLE,attachments[index].depth.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
		};

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = 
		{
			// Binding 0 :Color input attachment
			vks::initializers::GenWriteDescriptorSet(descriptorSets.attachmentDescriptorSetRead[index],VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,0,&descriptorImageInfos[0]),
			// Binding 1: Depth input attachment
			vks::initializers::GenWriteDescriptorSet(descriptorSets.attachmentDescriptorSetRead[index],VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,1,&descriptorImageInfos[1]),
			// Binding 2: Display paramters uniform buffer
			vks::initializers::GenWriteDescriptorSet(descriptorSets.attachmentDescriptorSetRead[index],VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2,&uniformBuffers.params.descriptorBufferInfo),
		};

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	// Overrride framebuffer setup from bass class
	void setupFrameBuffer() override
	{
		// If the window is resized,all the framebuffer/attachments used in our composition passes need to be recreated
		// 这时候渲染管线资源是已经创建过了，需要重新更新描述符集的到管线布局上
		if (attachmentSize.width != width || attachmentSize.height != height)
		{
			attachmentSize = { width,height };

			for (auto i = 0; i < attachments.size(); i++)
			{
				clearAttachment(&attachments[i].color);
				clearAttachment(&attachments[i].depth);

				createAttachment(colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &attachments[i].color);
				createAttachment(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &attachments[i].depth);

				// Since the framebuffers/attachments are referred in the descriptor sets, these need to be updated too
				updateAttachmentReadDescriptors(i);
			}
		}//if

		VkImageView views[3];

		VkFramebufferCreateInfo frameBufferCreateInfo{};
		frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferCreateInfo.renderPass = renderPass;
		frameBufferCreateInfo.attachmentCount = 3;
		frameBufferCreateInfo.pAttachments = views;
		frameBufferCreateInfo.width = width;
		frameBufferCreateInfo.height = height;
		frameBufferCreateInfo.layers = 1;
		
		frameBuffers.resize(swapChain.imageCount);
		for (uint32_t i = 0; i < frameBuffers.size(); i++)
		{
			views[0] = swapChain.buffers[i].view;
			views[1] = attachments[i].color.view;
			views[2] = attachments[i].depth.view;

			VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &frameBuffers[i]));
		}//for
	}//setupFrameBuffer

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		scene.loadFromFile(getAssetPath() + "models/treasure_smooth.gltf", vulkanDevice, queue, glTFLoadingFlags);
	}

	void updateUniformBuffers()
	{
		uboMatrices.projection = camera.matrices.perspective;
		uboMatrices.view = camera.matrices.view;
		uboMatrices.model = glm::mat4(1.0f);

		memcpy(uniformBuffers.matrices.mappedData, &uboMatrices, sizeof(uboMatrices));
		memcpy(uniformBuffers.params.mappedData, &uboParams, sizeof(uboParams));
	}

	void prepareUniformBuffers()
	{
		// uboMatrices
		vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.matrices, sizeof(uboMatrices));

		// uboParams
		vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.params, sizeof(uboParams));

		VK_CHECK_RESULT(uniformBuffers.matrices.map());
		VK_CHECK_RESULT(uniformBuffers.params.map());

		updateUniformBuffers();
	}

	void setupDescriptors()
	{
		/*
		 Pool
		*/
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,attachments.size() + 1),//3缓冲，每个管线1个UBO，共2条管线
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, attachments.size() + 1),//根本没用到
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,attachments.size() * 2 + 1),//3缓冲，每个管线至多同时2个InputAttachment， wapChain.imageCount * 2
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::GenDescriptorPoolCreateInfo(static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), attachments.size() + 1);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		/*
			Attachment descriptorSet write
		*/
		{
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBingdings = { vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,0) };

			VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBingdings);
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayouts.attachmentDescriptorSetLayoutWrite));

			VkDescriptorSetAllocateInfo allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.attachmentDescriptorSetLayoutWrite, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.attachmentDescriptorSetWrite));

			VkWriteDescriptorSet writeDescriptorSet = vks::initializers::GenWriteDescriptorSet(descriptorSets.attachmentDescriptorSetWrite, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers.matrices.descriptorBufferInfo);
			vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
		}

		/*
			Attachment descriptorSet read
		*/
		{
			std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
			{
				// Binding 0: Color input attachment
				vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,VK_SHADER_STAGE_FRAGMENT_BIT,0),
				// Binding 1: Depth input attachment
				vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,VK_SHADER_STAGE_FRAGMENT_BIT,1),
				// Binding 2: Display parameters uniform buffer
				vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_FRAGMENT_BIT,2),
			};
			VkDescriptorSetLayoutCreateInfo descritptorSetLayoutCI = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings);
			VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descritptorSetLayoutCI, nullptr, &descriptorSetLayouts.attachmentDescriptorSetLayoutRead));

			descriptorSets.attachmentDescriptorSetRead.resize(attachments.size());
			for (auto i = 0; i < descriptorSets.attachmentDescriptorSetRead.size(); i++)
			{
				VkDescriptorSetAllocateInfo allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.attachmentDescriptorSetLayoutRead, 1);
				VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.attachmentDescriptorSetRead[i]));
				updateAttachmentReadDescriptors(i);
			}
		}//read


		// PipelineLayout
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayouts.attachmentDescriptorSetLayoutWrite, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.attachmentLayoutWrite));

		VkPipelineLayoutCreateInfo pipelineLayoutCI = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayouts.attachmentDescriptorSetLayoutRead, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCI, nullptr, &pipelineLayouts.attachmentLayoutRead));
	}

	void preparePipelines()
	{
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::GenPipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::GenPipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::GenPipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::GenPipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::GenPipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::GenPipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::GenPipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::GenPipelineDynamicStateCreateInfo(dynamicStateEnables);

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::GenPipelineCreateInfo();
		pipelineCI.renderPass = renderPass;
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;

		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		/*
			Attachments write
		*/
		// Pipeline will be used in first sub pass
		pipelineCI.subpass = 0; //index
		pipelineCI.layout = pipelineLayouts.attachmentLayoutWrite;
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position,vkglTF::VertexComponent::Color,vkglTF::VertexComponent::Normal });

		shaderStages[0] = loadShader(getShaderPath() + "inputattachments/attachmentwrite.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "inputattachments/attachmentwrite.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.attachmentWrite));
		
		/*
			Attachment read
		*/
		// Pipeline will be used in second sub pass
		pipelineCI.subpass = 1;
		pipelineCI.layout = pipelineLayouts.attachmentLayoutRead;

		VkPipelineVertexInputStateCreateInfo emptyVertexInputStateCI{};
		emptyVertexInputStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

		pipelineCI.pVertexInputState = &emptyVertexInputStateCI;
		colorBlendStateCI.attachmentCount = 1;
		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		depthStencilStateCI.depthWriteEnable = VK_FALSE;
		
		shaderStages[0] = loadShader(getShaderPath() + "inputattachments/attachmentread.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "inputattachments/attachmentread.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.attachmentRead));
	}

	void buildCommandBuffersForPreRenderPrmitives()
	{
		VkCommandBufferBeginInfo cmdBufferBeginInfo = vks::initializers::GenCommandBufferBeginInfo();

		VkClearValue clearValues[3];
		clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
		clearValues[1].color = { { 0.0f, 0.0f, 0.2f, 0.0f } };
		clearValues[2].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::GenRenderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 3;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); i++)
		{
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufferBeginInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::GenRect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			VkDeviceSize offset[1] = { 0 };

			/*
			 First sub pass
			 Fills the attachments
			*/
			{
				vks::debugmarker::beginRegion(drawCmdBuffers[i], "Subpass 0: Writing attachments", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.attachmentWrite);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.attachmentLayoutWrite, 0, 1, &descriptorSets.attachmentDescriptorSetWrite, 0, NULL);
				scene.draw(drawCmdBuffers[i]);

				vks::debugmarker::endRegion(drawCmdBuffers[i]);
			}

			/*
			 Second sub pass
			 Render a full screen quad, reading from the previously written attachments via input attachments
			*/
			{
				vks::debugmarker::beginRegion(drawCmdBuffers[i], "Subpass 1: Reading attachments", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

				vkCmdNextSubpass(drawCmdBuffers[i], VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.attachmentRead);
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.attachmentLayoutRead, 0, 1, &descriptorSets.attachmentDescriptorSetRead[i], 0, NULL);
				scene.draw(drawCmdBuffers[i]);

				vks::debugmarker::endRegion(drawCmdBuffers[i]);
			}

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));

		}//for
	}

	void prepareForRendering()
	{
		VulkanExampleBase::prepareForRendering();
		loadAssets();
		prepareUniformBuffers();
		setupDescriptors();
		preparePipelines();
		buildCommandBuffersForPreRenderPrmitives();
		prepared = true;
	}

	virtual void render() override
	{
		if (!prepared)
			return;
		draw();
		if (camera.updated) {
			updateUniformBuffers();
		}
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay) override
	{
		if (overlay->header("Settings")) {
			overlay->text("Input attachment");
			if (overlay->comboBox("##attachment", &uboParams.attachmentIndex, { "color", "depth" })) {
				updateUniformBuffers();
			}
			switch (uboParams.attachmentIndex) {
			case 0:
				overlay->text("Brightness");
				if (overlay->sliderFloat("##b", &uboParams.brightnessContrast[0], 0.0f, 2.0f)) {
					updateUniformBuffers();
				}
				overlay->text("Contrast");
				if (overlay->sliderFloat("##c", &uboParams.brightnessContrast[1], 0.0f, 4.0f)) {
					updateUniformBuffers();
				}
				break;
			case 1:
				overlay->text("Visible range");
				if (overlay->sliderFloat("min", &uboParams.range[0], 0.0f, uboParams.range[1])) {
					updateUniformBuffers();
				}
				if (overlay->sliderFloat("max", &uboParams.range[1], uboParams.range[0], 1.0f)) {
					updateUniformBuffers();
				}
				break;
			}
		}
	}

private:

};

VULKAN_EXAMPLE_MAIN()