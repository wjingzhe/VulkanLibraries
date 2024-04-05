/*
* Vulkan Example - High dynamic range rendering
*
* Note: Requires the separate asset pack (see data/README.md)
*
* Copyright by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "VulkanExampleBase.h"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION false

class VulkanExample :public VulkanExampleBase
{
public:
	bool bloom = true;
	bool displaySkybox = true;

	struct
	{
		vks::TextureCubeMap envMap;
	} textures;

	struct
	{
		vks::Buffer matrices;
		vks::Buffer params;
	} uniformBuffers;

	struct UBOVS
	{
		glm::mat4 projection;
		glm::mat4 modelView;
		glm::mat4 inverseModelView;
	} uboVS;

	struct UBOParams
	{
		float exposure = 1.0f;
	} uboParams;


	struct
	{
		VkDescriptorSetLayout models;
		VkDescriptorSetLayout composition;
		VkDescriptorSetLayout bloomFilter;
	} descriptorSetLayouts;

	struct
	{
		VkDescriptorSet object;
		VkDescriptorSet skybox;
		VkDescriptorSet composition;
		VkDescriptorSet bloomFilter;
	} descriptorSets;

	struct
	{
		VkPipelineLayout models;
		VkPipelineLayout composition;
		VkPipelineLayout bloomFilter;
	} pipelineLayouts;

	struct
	{
		VkPipeline skybox;
		VkPipeline reflect;
		VkPipeline composition;
		VkPipeline bloom[2];
	} pipelines;


	// FrameBuffer for offscreen rendering
	struct FrameBufferAttachment
	{
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
		VkFormat format;

		void destroy(VkDevice device)
		{
			vkDestroyImageView(device, view, nullptr);
			vkDestroyImage(device, image, nullptr);
			vkFreeMemory(device, mem, nullptr);
		}
	};

	struct FrameBuffer
	{
		int32_t width, height;
		VkFramebuffer frameBuffer;
		FrameBufferAttachment colorAttachment[2];
		FrameBufferAttachment depth;
		VkRenderPass renderPass;
		VkSampler sampler;
	} offscreenFrameBuffer;

	struct
	{
		int32_t width, height;
		VkFramebuffer frameBuffer;
		FrameBufferAttachment colorAttachment[1];
		VkRenderPass renderPass;
		VkSampler sampler;
	} filterPass;

	struct Models
	{
		vkglTF::Model skybox;
		std::vector<vkglTF::Model> objects;
		int32_t objectIndex = 1;
	} models;

	std::vector<std::string> objectNames;

	VulkanExample():VulkanExampleBase(ENABLE_VALIDATION)
	{
		windowTitle = "High dynamic range rendering";
		camera.cameraType = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -6.0f));
		camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
	}

	~VulkanExample()
	{
		vkDestroyPipeline(device, pipelines.skybox, nullptr);
		vkDestroyPipeline(device, pipelines.reflect, nullptr);
		vkDestroyPipeline(device, pipelines.composition, nullptr);
		vkDestroyPipeline(device, pipelines.bloom[0], nullptr);
		vkDestroyPipeline(device, pipelines.bloom[1], nullptr);

		vkDestroyPipelineLayout(device, pipelineLayouts.models, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayouts.composition, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayouts.bloomFilter, nullptr);

		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.models, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.composition, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.bloomFilter, nullptr);

		vkDestroyRenderPass(device, offscreenFrameBuffer.renderPass, nullptr);
		vkDestroyRenderPass(device, filterPass.renderPass, nullptr);

		vkDestroyFramebuffer(device, offscreenFrameBuffer.frameBuffer, nullptr);
		vkDestroyFramebuffer(device, filterPass.frameBuffer, nullptr);

		vkDestroySampler(device, offscreenFrameBuffer.sampler, nullptr);
		vkDestroySampler(device, filterPass.sampler, nullptr);

		offscreenFrameBuffer.depth.destroy(device);
		offscreenFrameBuffer.colorAttachment[0].destroy(device);
		offscreenFrameBuffer.colorAttachment[1].destroy(device);

		filterPass.colorAttachment[0].destroy(device);

		uniformBuffers.matrices.destroy();
		uniformBuffers.params.destroy();
		textures.envMap.destroy();
	}

	void loadAssets()
	{
		// Load glTF models
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::FlipY;
		models.skybox.loadFromFile(getAssetPath() + "models/cube.gltf", vulkanDevice, queue, glTFLoadingFlags);
		std::vector<std::string> filenames = { "sphere.gltf","teapot.gltf","torusknot.gltf","venus.gltf" };
		objectNames = { "Sphere", "Teapot", "Torusknot", "Venus" };
		models.objects.resize(filenames.size());
		for (size_t i = 0; i < filenames.size(); i++)
		{
			models.objects[i].loadFromFile(getAssetPath() + "models/"+filenames[i],vulkanDevice,queue,glTFLoadingFlags);
		}
		// Load HDR cube map
		textures.envMap.loadFromFile(getAssetPath() + "textures/hdr/uffizi_cube.ktx", VK_FORMAT_R16G16B16A16_SFLOAT, vulkanDevice, queue);
	}

	void updateUniformBuffers()
	{
		uboVS.projection = camera.matrices.perspective;
		uboVS.modelView = camera.matrices.view;
		uboVS.inverseModelView = glm::inverse(camera.matrices.view);
		memcpy(uniformBuffers.matrices.mappedData, &uboVS, sizeof(uboVS));
	}

	void updateParams()
	{
		memcpy(uniformBuffers.params.mappedData, &uboParams, sizeof(uboParams));
	}

	void prepareUniformBuffers()
	{
		// Matrices vertex shader uniform buffer
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.matrices,sizeof(uboVS)));

		// Params
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffers.params, sizeof(uboParams)));

		// Map persistent

		VK_CHECK_RESULT(uniformBuffers.matrices.map());
		VK_CHECK_RESULT(uniformBuffers.params.map());

		updateUniformBuffers();
		updateParams();
	}

	void createAttachment(VkFormat format,VkImageUsageFlagBits usage,FrameBufferAttachment *pAttachment)
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

		assert(aspectMask > 0);

		VkImageCreateInfo imageCreateInfo = vks::initializers::GenImageCreateInfo();
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = format;
		imageCreateInfo.extent.width = offscreenFrameBuffer.width;
		imageCreateInfo.extent.height = offscreenFrameBuffer.height;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageCreateInfo.usage = usage | VK_IMAGE_USAGE_SAMPLED_BIT;
		VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &pAttachment->image));

		VkMemoryAllocateInfo	memAlloc = vks::initializers::GenMemoryAllocateInfo();
		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, pAttachment->image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &pAttachment->mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, pAttachment->image, pAttachment->mem, 0));

		VkImageViewCreateInfo	imageViewCreateInfo = vks::initializers::GenImageViewCreateInfo();
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

	void prepareOffScreenfer()
	{
		//offScreen
		{
			offscreenFrameBuffer.width = width;
			offscreenFrameBuffer.height = height;

			// Color attachments

			// Two floating point color buffers
			createAttachment(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &offscreenFrameBuffer.colorAttachment[0]);
			createAttachment(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &offscreenFrameBuffer.colorAttachment[1]);
			// Depth attachment
			createAttachment(depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &offscreenFrameBuffer.depth);

			// Set up separate renderpass with references to the color and depth attachments
			std::array<VkAttachmentDescription, 3> attachmentDescriptions = {};

			// init attachment properties
			for (uint32_t i = 0; i < 3; i++)
			{
				attachmentDescriptions[i].samples = VK_SAMPLE_COUNT_1_BIT;
				attachmentDescriptions[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
				attachmentDescriptions[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				attachmentDescriptions[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				attachmentDescriptions[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

				if (i == 2)
				{
					attachmentDescriptions[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					attachmentDescriptions[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				}
				else
				{
					attachmentDescriptions[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
					attachmentDescriptions[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				}//if_else
			}//for

			// Formats
			attachmentDescriptions[0].format = offscreenFrameBuffer.colorAttachment[0].format;
			attachmentDescriptions[1].format = offscreenFrameBuffer.colorAttachment[1].format;
			attachmentDescriptions[2].format = offscreenFrameBuffer.depth.format;

			std::vector<VkAttachmentReference>colorReferences;
			colorReferences.push_back({ 0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
			colorReferences.push_back({ 1,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });

			VkAttachmentReference depthReference = {};
			depthReference.attachment = 2;
			depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.pColorAttachments = colorReferences.data();
			subpass.colorAttachmentCount = 2;
			subpass.pDepthStencilAttachment = &depthReference;

			// Use subpass dependencies for attachment layout transitions
			std::array<VkSubpassDependency, 2> dependencies;
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			VkRenderPassCreateInfo renderPassCreateInfo = {};
			renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassCreateInfo.pAttachments = attachmentDescriptions.data();
			renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
			renderPassCreateInfo.subpassCount = 1;
			renderPassCreateInfo.pSubpasses = &subpass;
			renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
			renderPassCreateInfo.pDependencies = dependencies.data();

			VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &offscreenFrameBuffer.renderPass));

			std::array<VkImageView, 3> attachmentViews;
			attachmentViews[0] = offscreenFrameBuffer.colorAttachment[0].view;
			attachmentViews[1] = offscreenFrameBuffer.colorAttachment[1].view;
			attachmentViews[2] = offscreenFrameBuffer.depth.view;

			VkFramebufferCreateInfo framebufferCreateInfo = {};
			framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferCreateInfo.pNext = NULL;
			framebufferCreateInfo.renderPass = offscreenFrameBuffer.renderPass;
			framebufferCreateInfo.attachmentCount = static_cast<uint32_t>(attachmentViews.size());
			framebufferCreateInfo.pAttachments = attachmentViews.data();
			framebufferCreateInfo.width = offscreenFrameBuffer.width;
			framebufferCreateInfo.height = offscreenFrameBuffer.height;
			framebufferCreateInfo.layers = 1;
			VK_CHECK_RESULT(vkCreateFramebuffer(device, &framebufferCreateInfo, nullptr, &offscreenFrameBuffer.frameBuffer));

			// Create sampler to sample from the color attachments
			VkSamplerCreateInfo samplerCreateInfo = vks::initializers::GenSamplerCreateInfo();
			samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
			samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
			samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerCreateInfo.mipLodBias = 0.0f;
			samplerCreateInfo.maxAnisotropy = 1.0f;
			samplerCreateInfo.minLod = 0.0f;
			samplerCreateInfo.maxLod = 1.0f;
			samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT(vkCreateSampler(device, &samplerCreateInfo, nullptr, &offscreenFrameBuffer.sampler));
		}//offScreen


		// Bloom separable filter pass
		{
			filterPass.width = width;
			filterPass.height = height;

			// Color attachments

			// Two floating point color buffers
			createAttachment(VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &filterPass.colorAttachment[0]);

			// Set up separate renderPass with references to the color and depth attachments
			std::array<VkAttachmentDescription, 1> attachmentDescriptions = {};
			// Init attachment properties
			attachmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
			attachmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			attachmentDescriptions[0].format = filterPass.colorAttachment[0].format;

			std::vector<VkAttachmentReference> colorReferences;
			colorReferences.push_back({ 0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });

			VkSubpassDescription subpassDescription = {};
			subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpassDescription.pColorAttachments = colorReferences.data();
			subpassDescription.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());

			// Use subpass dependencies for attachment layout transitions
			std::array < VkSubpassDependency, 2> dependencies;
			dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[0].dstSubpass = 0;
			dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			dependencies[1].srcSubpass = 0;
			dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
			dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
			dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.pAttachments = attachmentDescriptions.data();
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpassDescription;
			renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
			renderPassInfo.pDependencies = dependencies.data();
			VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &filterPass.renderPass));

			// Create sampler to sample from the color attahments
			VkSamplerCreateInfo samplerCreateInfo = vks::initializers::GenSamplerCreateInfo();
			samplerCreateInfo.magFilter = VK_FILTER_NEAREST;
			samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
			samplerCreateInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			samplerCreateInfo.mipLodBias = 0.0f;
			samplerCreateInfo.maxAnisotropy = 1.0f;
			samplerCreateInfo.minLod = 0.0f;
			samplerCreateInfo.maxLod = 1.0f;
			samplerCreateInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
			VK_CHECK_RESULT(vkCreateSampler(device, &samplerCreateInfo, nullptr, &filterPass.sampler));
		} //Bloom
	}//prepareOffScreenfer

	void setupDescriptorSetLayoutAndPipelineLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> gbufferSetLayoutBindings =
		{
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,0),
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,1),
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,2),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayoutInfo = vks::initializers::GenDescriptorSetLayoutCreateInfo(gbufferSetLayoutBindings.data(), static_cast<uint32_t>(gbufferSetLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutInfo, nullptr, &descriptorSetLayouts.models));

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayouts.models,1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.models));


		// Bloom filter
		std::vector<VkDescriptorSetLayoutBinding> bloomSetLayoutBindings =
		{
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,0),
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,1),
		};

		descriptorLayoutInfo = vks::initializers::GenDescriptorSetLayoutCreateInfo(bloomSetLayoutBindings.data(), static_cast<uint32_t>(bloomSetLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutInfo, nullptr, &descriptorSetLayouts.bloomFilter));

		pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayouts.bloomFilter, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.bloomFilter));


		// G-Buffer composition
		std::vector<VkDescriptorSetLayoutBinding> compositionSetLayoutBindings = 
		{
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,0),
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,1),
		};

		descriptorLayoutInfo = vks::initializers::GenDescriptorSetLayoutCreateInfo(compositionSetLayoutBindings.data(), static_cast<uint32_t>(compositionSetLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutInfo, nullptr, &descriptorSetLayouts.composition));

		pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayouts.composition, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.composition));
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::GenPipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::GenPipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::GenPipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::GenPipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::GenPipelineDepthStencilStateCreateInfo(VK_FALSE, VK_FALSE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::GenPipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::GenPipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::GenPipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::GenPipelineCreateInfo(pipelineLayouts.models, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		VkSpecializationInfo specilizationInfo;
		std::array<VkSpecializationMapEntry, 1>specializationMapEntries;

		// Full screen pipelines

		// Empty vertex input state,full screen triangles are generated by the vertex shader
		VkPipelineVertexInputStateCreateInfo emptyInputStateCI = vks::initializers::GenPipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputStateCI;

		// Final full screen composition pass pipeline
		std::vector<VkPipelineColorBlendAttachmentState> blendAttachmentStates =
		{
			vks::initializers::GenPipelineColorBlendAttachmentState(0xf, VK_FALSE),
			vks::initializers::GenPipelineColorBlendAttachmentState(0xf, VK_FALSE),
		};
		pipelineCI.layout = pipelineLayouts.composition;
		pipelineCI.renderPass = renderPass;
		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		colorBlendStateCI.attachmentCount = 2;
		colorBlendStateCI.pAttachments = blendAttachmentStates.data();
		shaderStages[0] = loadShader(getShadersPath() + "hdr/composition.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "hdr/composition.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.composition));

		
		// Bloom pass
		shaderStages[0] = loadShader(getShadersPath() + "hdr/bloom.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "hdr/bloom.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		colorBlendStateCI.attachmentCount = 1;
		colorBlendStateCI.pAttachments = &blendAttachmentState;
		blendAttachmentState.colorWriteMask = 0xF;
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;

		// Set constant parameters via specialization constants
		specializationMapEntries[0] = vks::initializers::GenSpecializationMapEntry(0, 0, sizeof(uint32_t));
		uint32_t dir = 1;
		specilizationInfo = vks::initializers::GenSpecializationInfo(1, specializationMapEntries.data(), sizeof(dir), &dir);
		shaderStages[1].pSpecializationInfo = &specilizationInfo;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.bloom[0]));

		// Second blur pass
		pipelineCI.renderPass = filterPass.renderPass;
		dir = 0;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.bloom[1]));


		// Object rendering pipelines
		// Use vertex input state from glTF model setup
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position,vkglTF::VertexComponent::Normal });

		blendAttachmentState.blendEnable = VK_FALSE;
		pipelineCI.layout = pipelineLayouts.models;
		pipelineCI.renderPass = offscreenFrameBuffer.renderPass;
		colorBlendStateCI.attachmentCount = 2;
		colorBlendStateCI.pAttachments = blendAttachmentStates.data();
		shaderStages[0] = loadShader(getShadersPath() + "hdr/gbuffer.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "hdr/gbuffer.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Set constant parameters via specialization constants
		specializationMapEntries[0] = vks::initializers::GenSpecializationMapEntry(0, 0, sizeof(uint32_t));
		uint32_t shadertype = 0;
		specilizationInfo = vks::initializers::GenSpecializationInfo(1, specializationMapEntries.data(), sizeof(shadertype), &shadertype);
		shaderStages[0].pSpecializationInfo = &specilizationInfo;
		shaderStages[1].pSpecializationInfo = &specilizationInfo;
		
		// Skybox pipeline (background cube)
		rasterizationStateCI.cullMode = VK_CULL_MODE_FRONT_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skybox));

		// Object rendering pipeline
		shadertype = 1;
		// Enable depth test and write
		depthStencilStateCI.depthWriteEnable = VK_TRUE;
		depthStencilStateCI.depthTestEnable = VK_TRUE;
		// Flip cull mode
		rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.reflect));
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,4),
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,6),
		};

		uint32_t numDescriptorSets = 4;
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::GenDescriptorPoolCreateInfo(static_cast<uint32_t>(poolSizes.size()), poolSizes.data(), numDescriptorSets);

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSets()
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.models,1);

		// 3D object descriptor set
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.object));
		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			vks::initializers::GenWriteDescriptorSet(descriptorSets.object,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&uniformBuffers.matrices.descriptorBufferInfo),
			vks::initializers::GenWriteDescriptorSet(descriptorSets.object,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,&textures.envMap.descriptorImageInfo),
			vks::initializers::GenWriteDescriptorSet(descriptorSets.object,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2,&uniformBuffers.params.descriptorBufferInfo),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		// Sky box descriptor Set
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.skybox));
		writeDescriptorSets =
		{
			vks::initializers::GenWriteDescriptorSet(descriptorSets.skybox,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&uniformBuffers.matrices.descriptorBufferInfo),
			vks::initializers::GenWriteDescriptorSet(descriptorSets.skybox,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,&textures.envMap.descriptorImageInfo),
			vks::initializers::GenWriteDescriptorSet(descriptorSets.skybox,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2,&uniformBuffers.params.descriptorBufferInfo),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		
		// Bloom filter
		allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.bloomFilter, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.bloomFilter));
		std::vector<VkDescriptorImageInfo> colorDescriptors =
		{
			vks::initializers::GenDescriptorImageInfo(offscreenFrameBuffer.sampler,offscreenFrameBuffer.colorAttachment[0].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			vks::initializers::GenDescriptorImageInfo(offscreenFrameBuffer.sampler,offscreenFrameBuffer.colorAttachment[1].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
		};

		writeDescriptorSets =
		{
			vks::initializers::GenWriteDescriptorSet(descriptorSets.bloomFilter,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,0,&colorDescriptors[0]),
			vks::initializers::GenWriteDescriptorSet(descriptorSets.bloomFilter,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,&colorDescriptors[1]),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);


		// Composition descriptor set
		allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.composition,1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets.composition));
		colorDescriptors =
		{
			vks::initializers::GenDescriptorImageInfo(offscreenFrameBuffer.sampler,offscreenFrameBuffer.colorAttachment[0].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
			vks::initializers::GenDescriptorImageInfo(offscreenFrameBuffer.sampler,offscreenFrameBuffer.colorAttachment[1].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
		};

		writeDescriptorSets =
		{
			vks::initializers::GenWriteDescriptorSet(descriptorSets.composition,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,0,&colorDescriptors[0]),
			vks::initializers::GenWriteDescriptorSet(descriptorSets.composition,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,&colorDescriptors[1]),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
	}

	void buildCommandBuffersForPreRenderPrmitives() override
	{
		VkCommandBufferBeginInfo cmdBufferBeginInfo = vks::initializers::GenCommandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = { {0.0f,0.0f,0.0f,0.0f} };
		clearValues[1].depthStencil = { 1.0f,0 };

		VkViewport viewport;
		VkRect2D scissor;

		for (size_t i = 0; i < drawCmdBuffers.size(); i++)
		{
			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufferBeginInfo));

			{// G-buffer
				/*
					First pass: Render scene to offscreen framebuffer
				*/

				std::array<VkClearValue, 3> clearValues;
				clearValues[0].color = { {0.0f,0.0f,0.0f,0.0f} };
				clearValues[1].color = { {0.0f,0.0f,0.0f,0.0f} };
				clearValues[2].depthStencil = { 1.0f,0 };

				VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::GenRenderPassBeginInfo();
				renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
				renderPassBeginInfo.renderPass = offscreenFrameBuffer.renderPass;
				renderPassBeginInfo.framebuffer = offscreenFrameBuffer.frameBuffer;
				renderPassBeginInfo.renderArea.extent.width = offscreenFrameBuffer.width;
				renderPassBeginInfo.renderArea.extent.height = offscreenFrameBuffer.height;
				renderPassBeginInfo.clearValueCount = 3;
				renderPassBeginInfo.pClearValues = clearValues.data();

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vks::initializers::GenViewport((float)offscreenFrameBuffer.width, (float)offscreenFrameBuffer.height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::GenRect2D(offscreenFrameBuffer.width, offscreenFrameBuffer.height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				VkDeviceSize offsets[1] = { 0 };

				// SkyBox
				if (displaySkybox)
				{
					vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.models, 0, 1, &descriptorSets.skybox, 0, NULL);
					vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.skybox.vertices.buffer, offsets);
					vkCmdBindIndexBuffer(drawCmdBuffers[i], models.skybox.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
					vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skybox);
					models.skybox.draw(drawCmdBuffers[i]);
				}

				// 3D object
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.models, 0, 1, &descriptorSets.object, 0, NULL);
				vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &models.objects[models.objectIndex].vertices.buffer, offsets);
				vkCmdBindIndexBuffer(drawCmdBuffers[i], models.objects[models.objectIndex].indices.buffer, 0, VK_INDEX_TYPE_UINT32);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.reflect);
				models.objects[models.objectIndex].draw(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}//G-buffer

			/*
				Second render pass : First bloom pass
			*/
			if (bloom)
			{

			}

			/*
				Note: Explict synchronization is not required between the render pass , as this is done implict via sub pass dependencies
			*/

			/*
				Third render pass:scene rendering with applied second bloom pass(when enabled)
			*/
			{
				VkClearValue clearValues[2];
				clearValues[0].color = { {0.0f,0.0f,0.0f,0.0f} };
				clearValues[1].depthStencil = { 1.0f,0 };

				// Final composition
				VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::GenRenderPassBeginInfo();
				renderPassBeginInfo.framebuffer = frameBuffers[i];
				renderPassBeginInfo.renderPass = renderPass;
				renderPassBeginInfo.clearValueCount = 2;
				renderPassBeginInfo.renderArea.extent.width = width;
				renderPassBeginInfo.renderArea.extent.height = height;
				renderPassBeginInfo.pClearValues = clearValues;

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::GenRect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.composition, 0, 1, &descriptorSets.composition, 0, NULL);

				// Scene
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.composition);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				if (bloom)
				{
					
				}

				drawUI(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}//for

	}

	void prepareForRendering() override
	{
		VulkanExampleBase::prepareForRendering();
		loadAssets();
		prepareUniformBuffers();
		prepareOffScreenfer();
		setupDescriptorSetLayoutAndPipelineLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSets();
		buildCommandBuffersForPreRenderPrmitives();
		prepared = true;
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		// Command buffer to be  submitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentCmdBufferIndex];

		// Submit to queue
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();
	}//draw

	virtual void render() override
	{
		if (!prepared)
			return;
		draw();
		if (camera.updated)
			updateUniformBuffers();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay) override
	{
		if (overlay->header("Settings")) {
			if (overlay->comboBox("Object type", &models.objectIndex, objectNames)) {
				updateUniformBuffers();
				buildCommandBuffersForPreRenderPrmitives();
			}
			if (overlay->inputFloat("Exposure", &uboParams.exposure, 0.025f, 3)) {
				updateParams();
			}
			if (overlay->checkBox("Bloom", &bloom)) {
				buildCommandBuffersForPreRenderPrmitives();
			}
			if (overlay->checkBox("Skybox", &displaySkybox)) {
				buildCommandBuffersForPreRenderPrmitives();
			}
		}
	}

private:

};

VULKAN_EXAMPLE_MAIN()