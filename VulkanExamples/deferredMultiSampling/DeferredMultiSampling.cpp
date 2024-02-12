/*
* Vulkan Example - Multi sampling with explicit resolve for deferred shading example
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "VulkanExampleBase.h"
#include "VulkanFrameBuffer.hpp"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION false

#if defined(__ANDROID__)
//Use max screen dimension as defined framebuffer size
#define FB_DIM std::max(width,height)
#else
#define FB_DIM 2048
#endif // defined(__ANDROID__)

class VulkanExample:public VulkanExampleBase
{
public:
	int32_t debugDisplayTarget = 0;
	bool useMSAA = true;
	bool useSampleShading = true;
	VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;


	struct
	{
		struct
		{
			vks::Texture2D colorMap;
			vks::Texture2D normalMap;
		} model;

		struct
		{
			vks::Texture2D colorMap;
			vks::Texture2D normalMap;
		}background;
	} textures;

	struct
	{
		vkglTF::Model model;
		vkglTF::Model background;
	} models;

	struct
	{
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
		glm::vec4 instancePos[3];
	} uboOffscreenVS;

	struct Light
	{
		glm::vec4 position;
		glm::vec3 color;
		float radius;
	};

	struct
	{
		Light lights[6];
		glm::vec4 viewPos;
		int32_t debugDisplayTarget = 0;
	}uboComposition;

	struct
	{
		vks::Buffer offscreen;
		vks::Buffer composition;
	} uniformBuffers;

	struct
	{
		VkPipeline deferred; // Deferred lighting calculation
		VkPipeline deferredNoMSAA; // Deferred lighting calculation with explicit MSAA resolve
		VkPipeline offscreen; //(Offscreen) sceen rendering(fill G-Buffers)
		VkPipeline offscreenSampleShading; // (Offscreen) scene rendering (fill G-Buffers) with sample shading rate enabled
	} pipelines;
	VkPipelineLayout pipelineLayout;

	struct
	{
		VkDescriptorSet model;
		VkDescriptorSet background;
	} graphicPassDescriptorSets;

	VkDescriptorSet commonDescriptorSet;
	VkDescriptorSetLayout commonDescriptorSetLayout;

	vks::Framebuffer* offscreenframeBuffers;

	VkCommandBuffer offscreenCmdBuffer = VK_NULL_HANDLE;

	//Semaphore used to synchronize between offscreen and final scene rendering
	VkSemaphore offscreenSemaphore = VK_NULL_HANDLE;

	VulkanExample():VulkanExampleBase(ENABLE_VALIDATION)
	{
		windowTitle = "Multi sampled deferred shading";
		camera.cameraType = Camera::CameraType::firstperson;
		camera.movementSpeed = 5.0f;

#ifndef __ANDROID__
		camera.rotationSpeed = 0.25f;
#endif // !__ANDROID__
		camera.position = { 2.15f,0.3f,-8.75f };
		camera.setRotation(glm::vec3(-0.75f, 12.5f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		paused = true;
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources
		// Note: Inherited destructor cleans up resources stored in base class

		// Frame buffers
		if (offscreenframeBuffers)
		{
			delete offscreenframeBuffers;
		}

		vkDestroyPipeline(device, pipelines.deferred, nullptr);
		vkDestroyPipeline(device, pipelines.deferredNoMSAA, nullptr);
		vkDestroyPipeline(device, pipelines.offscreen, nullptr);
		vkDestroyPipeline(device, pipelines.offscreenSampleShading, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, commonDescriptorSetLayout, nullptr);

		// Uniform buffers
		uniformBuffers.offscreen.destroy();
		uniformBuffers.composition.destroy();

		textures.model.colorMap.destroy();
		textures.model.normalMap.destroy();
		textures.background.colorMap.destroy();
		textures.background.normalMap.destroy();

		vkDestroySemaphore(device, offscreenSemaphore, nullptr);
	}

	// Enable physical device features required for this example
	virtual void getEnabledFeatures()
	{
		// Enable sample rate shading filtering if supported
		if (deviceFeatures.sampleRateShading)
		{
			curEnabledDeviceFeatures.sampleRateShading = VK_TRUE;
		}

		// Enable anisotropic filtering if supported
		if (deviceFeatures.samplerAnisotropy)
		{
			curEnabledDeviceFeatures.samplerAnisotropy = VK_TRUE;
		}
	}

	VkSampleCountFlagBits getMaxUsableSampleCount()
	{
		VkSampleCountFlags counts = std::min(deviceProperties.limits.framebufferColorSampleCounts, deviceProperties.limits.framebufferDepthSampleCounts);
		if (counts &VK_SAMPLE_COUNT_64_BIT)
		{
			return VK_SAMPLE_COUNT_64_BIT;
		}
		if (counts &VK_SAMPLE_COUNT_32_BIT)
		{
			return VK_SAMPLE_COUNT_32_BIT;
		}
		if (counts &VK_SAMPLE_COUNT_16_BIT)
		{
			return VK_SAMPLE_COUNT_16_BIT;
		}
		if (counts &VK_SAMPLE_COUNT_8_BIT)
		{
			return VK_SAMPLE_COUNT_8_BIT;
		}
		if (counts &VK_SAMPLE_COUNT_4_BIT)
		{
			return VK_SAMPLE_COUNT_4_BIT;
		}
		if (counts &VK_SAMPLE_COUNT_2_BIT)
		{
			return VK_SAMPLE_COUNT_2_BIT;
		}
		return VK_SAMPLE_COUNT_1_BIT;
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;

		models.model.loadFromFile(getAssetPath() + "models/armor/armor.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.background.loadFromFile(getAssetPath() + "models/deferred_box.gltf", vulkanDevice, queue, glTFLoadingFlags);
		
		textures.model.colorMap.loadFromFile(getAssetPath() + "models/armor/colormap_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.model.normalMap.loadFromFile(getAssetPath() + "models/armor/normalmap_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.background.colorMap.loadFromFile(getAssetPath() + "textures/stonefloor02_color_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
		textures.background.normalMap.loadFromFile(getAssetPath() + "textures/stonefloor02_normal_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	// Prepare the framebuffer for offscreen rendering with multiple attachments used as render targets inside the fragment shaders
	void deferredSetup()
	{
		offscreenframeBuffers = new vks::Framebuffer(vulkanDevice);

		offscreenframeBuffers->width = FB_DIM;
		offscreenframeBuffers->height = FB_DIM;

		// Four attachments (3 color,1 depth)
		vks::AttachmentCreateInfo attachmentInfo = {};
		attachmentInfo.width = FB_DIM;
		attachmentInfo.height = FB_DIM;
		attachmentInfo.layerCount = 1;
		attachmentInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		attachmentInfo.imageSampleCount = sampleCount;

		// Color attachments
		// Attachment 0:(World space) Positions
		attachmentInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		offscreenframeBuffers->AddAttachment(attachmentInfo);

		// Attachment 1: (World space) Normals
		attachmentInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		offscreenframeBuffers->AddAttachment(attachmentInfo);

		// Attachment 2: Albedo( color)
		attachmentInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
		offscreenframeBuffers->AddAttachment(attachmentInfo);

		// Depth attachment
		// Find a suitable depth format
		VkFormat attDepthFormat;
		VkBool32 validDepthFormatResult = vks::tools::getSupportedDepthFormat(physicalDevice, &attDepthFormat);
		assert(validDepthFormatResult);

		attachmentInfo.format = attDepthFormat;
		attachmentInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		offscreenframeBuffers->AddAttachment(attachmentInfo);

		// Create sampler to sample from the color attachments
		VK_CHECK_RESULT(offscreenframeBuffers->CreateSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));

		// Create default renderpass for the framebuffer
		VK_CHECK_RESULT(offscreenframeBuffers->CreateRenderPass());
	}

	void updateUniformBufferOffscreen()
	{
		uboOffscreenVS.projection = camera.matrices.perspective;
		uboOffscreenVS.view = camera.matrices.view;
		uboOffscreenVS.model = glm::mat4(1.0f);
		memcpy(uniformBuffers.offscreen.mappedData, &uboOffscreenVS, sizeof(uboOffscreenVS));
	}

	void updateUniformBufferDeferredLights()
	{
		// White
		uboComposition.lights[0].position = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
		uboComposition.lights[0].color = glm::vec3(1.5f);
		uboComposition.lights[0].radius = 15.0f*0.25f;

		// Red
		uboComposition.lights[1].position = glm::vec4(-2.0f, 0.0f, 0.0f, 0.0f);
		uboComposition.lights[1].color = glm::vec3(1.0f, 0.0f, 0.0f);
		uboComposition.lights[1].radius = 15.0f;

		// Blue
		uboComposition.lights[2].position = glm::vec4(2.0f, -1.0f, 0.0f, 0.0f);
		uboComposition.lights[2].color = glm::vec3(0.0f, 0.0f, 2.5f);
		uboComposition.lights[2].radius = 5.0f;

		//Yellow
		uboComposition.lights[3].position = glm::vec4(0.0f, -0.9f, 0.5f, 0.0f);
		uboComposition.lights[3].color = glm::vec3(1.0f, 1.0f, 0.0f);
		uboComposition.lights[3].radius = 2.0f;

		// Green
		uboComposition.lights[4].position = glm::vec4(0.0f, -0.5f, 0.0f, 0.0f);
		uboComposition.lights[4].color = glm::vec3(0.0f, 1.0f, 0.2f);
		uboComposition.lights[4].radius = 5.0f;

		// Yellow
		uboComposition.lights[5].position = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
		uboComposition.lights[5].color = glm::vec3(1.0f, 0.7f, 0.3f);
		uboComposition.lights[5].radius = 25.0f;

		//重设光源位置
		uboComposition.lights[0].position.x = sin(glm::radians(360.0f*timer))*5.0f;
		uboComposition.lights[0].position.z = cos(glm::radians(360.0f*timer))*5.0f;

		uboComposition.lights[1].position.x =-4.0f+sin(glm::radians(360.0f*timer+45.0f))*2.0f;
		uboComposition.lights[1].position.z = 0.0f+cos(glm::radians(360.0f*timer+45.0f))*2.0f;

		uboComposition.lights[2].position.x = 4.0f+sin(glm::radians(360.0f*timer))*2.0f;
		uboComposition.lights[2].position.z = 0.0f+cos(glm::radians(360.0f*timer))*2.0f;

		uboComposition.lights[4].position.x = 0.0f + sin(glm::radians(360.0f*timer + 90.0f))*5.0f;
		uboComposition.lights[4].position.z = 0.0f - cos(glm::radians(360.0f*timer + 45.0f))*5.0f;

		uboComposition.lights[5].position.x = 0.0f + sin(glm::radians(-360.0f*timer + 135.0f))*10.0f;
		uboComposition.lights[5].position.z = 0.0f - cos(glm::radians(-360.0f*timer - 45.0f))*10.0f;

		// Current view position
		uboComposition.viewPos = glm::vec4(camera.position, 0.0f)*glm::vec4(-1.0f, 1.0f, -1.0f, 1.0f);
		uboComposition.debugDisplayTarget = debugDisplayTarget;

		memcpy(uniformBuffers.composition.mappedData, &uboComposition, sizeof(uboComposition));
	}

	void prepareUniformBuffers()
	{
		//Offscreen vertex shader
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.offscreen, sizeof(uboOffscreenVS)));

		// Deferred fragment shader
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.composition, sizeof(uboComposition)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffers.offscreen.map());
		VK_CHECK_RESULT(uniformBuffers.composition.map());

		// Init some values
		uboOffscreenVS.instancePos[0] = glm::vec4(0.0f);
		uboOffscreenVS.instancePos[1] = glm::vec4(-4.0f, 0.0f, -4.0f, 0.0f);
		uboOffscreenVS.instancePos[2] = glm::vec4(4.0f, 0.0f, -4.0f, 0.0f);

		// Update
		updateUniformBufferOffscreen();
		updateUniformBufferDeferredLights();
	}

	void setupDescriptorSetLayoutAndPipelineLayout()
	{
		// Deferred shading layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0:Vertex shader uniform buffer
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,0),
			// Binding 1: Position texture target / Scene ColorMap
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,1),
			// Binding 2: Normals texture target
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,2),
			// Binding 3: Albedo texture target
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,3),
			// Binding 4:Fragment shader uniform buffer
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_FRAGMENT_BIT,4),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayoutCreateInfo = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCreateInfo, nullptr, &commonDescriptorSetLayout));

		// Shared pipeline layout used by all pipelines
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&commonDescriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo = vks::initializers::GenPipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo = vks::initializers::GenPipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::GenPipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo = vks::initializers::GenPipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo = vks::initializers::GenPipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::GenPipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo = vks::initializers::GenPipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = vks::initializers::GenPipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::GenPipelineCreateInfo(pipelineLayout, renderPass);
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCreateInfo;
		pipelineCI.pRasterizationState = &rasterizationStateCreateInfo;
		pipelineCI.pColorBlendState = &colorBlendStateCreateInfo;
		pipelineCI.pDepthStencilState = &depthStencilStateCreateInfo;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pMultisampleState = &multisampleStateCreateInfo;
		pipelineCI.pDynamicState = &dynamicStateCreateInfo;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		// Fullscreen composition pass

		// Empty vertex input state,vertices are generated by the vertex shader
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::GenPipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;

		// Use specialization constants to pass number of samples to the shader(used for MSAA resol
		VkSpecializationMapEntry specializationEntry{};
		specializationEntry.constantID = 0;
		specializationEntry.offset = 0;
		specializationEntry.size = sizeof(uint32_t);

		uint32_t specializationData = sampleCount;

		VkSpecializationInfo specializationInfo;
		specializationInfo.mapEntryCount = 1;
		specializationInfo.pMapEntries = &specializationEntry;
		specializationInfo.dataSize = sizeof(specializationData);
		specializationInfo.pData = &specializationData;

		rasterizationStateCreateInfo.cullMode = VK_CULL_MODE_FRONT_BIT;

		// With MSAA
		shaderStages[0] = loadShader(getShaderPath() + "deferredmultisampling/deferred.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "deferredmultisampling/deferred.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		shaderStages[1].pSpecializationInfo = &specializationInfo;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.deferred));

		// No MSAA(1 sample)
		specializationData = 1;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.deferredNoMSAA));

		/*
		 Bass Pass
		*/

		// Vertex input state from glTF model for pipeline rendering models
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position,vkglTF::VertexComponent::UV ,
			vkglTF::VertexComponent::Color,vkglTF::VertexComponent::Normal,vkglTF::VertexComponent::Tangent });
		rasterizationStateCreateInfo.cullMode = VK_CULL_MODE_BACK_BIT;

		// Offscreen scene rendering pipeline
		// Separate render pass
		pipelineCI.renderPass = offscreenframeBuffers->renderPass;

		shaderStages[0] = loadShader(getShaderPath() + "deferredmultisampling/mrt.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "deferredmultisampling/mrt.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		//rasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_LINE;
		//rasterizationStateCreateInfo.lineWidth = 2.0f;
		multisampleStateCreateInfo.rasterizationSamples = sampleCount;
		multisampleStateCreateInfo.alphaToCoverageEnable = VK_TRUE;

		// Blend attachment states required for all color attachments
		// This is important,as color write mask will otherwise be 0x0 and you
		// won't see anything rendered to the attachment
		std::array<VkPipelineColorBlendAttachmentState, 3> blendAttachmentStates =
		{
			vks::initializers::GenPipelineColorBlendAttachmentState(0xf,VK_FALSE),
			vks::initializers::GenPipelineColorBlendAttachmentState(0xf,VK_FALSE),
			vks::initializers::GenPipelineColorBlendAttachmentState(0xf,VK_FALSE),
		};

		colorBlendStateCreateInfo.attachmentCount = static_cast<uint32_t>(blendAttachmentStates.size());
		colorBlendStateCreateInfo.pAttachments = blendAttachmentStates.data();

		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.offscreen));

		multisampleStateCreateInfo.sampleShadingEnable = VK_TRUE;
		multisampleStateCreateInfo.minSampleShading = 0.25f;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.offscreenSampleShading));
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,8),
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,9)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::GenDescriptorPoolCreateInfo(poolSizes, 3);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetAndUpdate()
	{
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &commonDescriptorSetLayout, 1);

		// Image descriptors for the offscreen color attachments
		VkDescriptorImageInfo texDescriptorPosition = vks::initializers::GenDescriptorImageInfo(offscreenframeBuffers->sampler, 
			offscreenframeBuffers->attachments[0].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo texDescriptorNormal = vks::initializers::GenDescriptorImageInfo(offscreenframeBuffers->sampler,
			offscreenframeBuffers->attachments[1].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		VkDescriptorImageInfo texDescriptorAlbedo = vks::initializers::GenDescriptorImageInfo(offscreenframeBuffers->sampler,
			offscreenframeBuffers->attachments[2].imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

		// Deferred composition
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &commonDescriptorSet));
		writeDescriptorSets =
		{
			vks::initializers::GenWriteDescriptorSet(commonDescriptorSet,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,&texDescriptorPosition,1),// Binding 1: World space position texture
			vks::initializers::GenWriteDescriptorSet(commonDescriptorSet,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2,&texDescriptorNormal,1),// Binding 2: World space normals texture
			vks::initializers::GenWriteDescriptorSet(commonDescriptorSet,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,3,&texDescriptorAlbedo,1),// Binding 3: Albedo texture
			vks::initializers::GenWriteDescriptorSet(commonDescriptorSet,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,4,&uniformBuffers.composition.descriptorBufferInfo,1),// Binding 4: Fragment shader uniform buffer
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		// Offscreen (scene)

		// Model
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &graphicPassDescriptorSets.model));
		writeDescriptorSets =
		{
			vks::initializers::GenWriteDescriptorSet(graphicPassDescriptorSets.model,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&uniformBuffers.offscreen.descriptorBufferInfo,1),// Binding 0: Vertex shader uniform buffer
			vks::initializers::GenWriteDescriptorSet(graphicPassDescriptorSets.model,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,&textures.model.colorMap.descriptorImageInfo,1),// Binding 1: Color texture
			vks::initializers::GenWriteDescriptorSet(graphicPassDescriptorSets.model,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2,&textures.model.normalMap.descriptorImageInfo,1),// Binding 2: World space normals texture
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);

		// Background
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &graphicPassDescriptorSets.background));
		writeDescriptorSets =
		{
			vks::initializers::GenWriteDescriptorSet(graphicPassDescriptorSets.background,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&uniformBuffers.offscreen.descriptorBufferInfo,1),// Binding 0: Vertex shader uniform buffer
			vks::initializers::GenWriteDescriptorSet(graphicPassDescriptorSets.background,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,&textures.background.colorMap.descriptorImageInfo,1),// Binding 1: Color texture
			vks::initializers::GenWriteDescriptorSet(graphicPassDescriptorSets.background,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,2,&textures.background.normalMap.descriptorImageInfo,1),// Binding 2: World space normals texture
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
	}

	void buildCommandBuffersForPreRenderPrmitives()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::GenCommandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = { {0.0f,0.0f,0.2f,0.0f} };
		clearValues[1].depthStencil = { 1.0f,0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::GenRenderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); i++)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = VulkanExampleBase::frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::GenRect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &commonDescriptorSet, 0, NULL);

			// Final composition as full screen quad
			// Note: Also used for debug display if debuDisplayTarget > 0
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, useMSAA ? pipelines.deferred : pipelines.deferredNoMSAA);
			vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}


	}

	void buildDeferredCommandBuffer()
	{
		if (offscreenCmdBuffer == VK_NULL_HANDLE)
		{
			offscreenCmdBuffer = vulkanDevice->CreateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, false);
		}

		// Create a semaphore used to synchronize offscreen rendering and usage
		if (offscreenSemaphore == VK_NULL_HANDLE)
		{
			VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::GenSemaphoneCreateInfo();
			VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &offscreenSemaphore));
		}

		VkCommandBufferBeginInfo cmdBufferBeginInfo = vks::initializers::GenCommandBufferBeginInfo();

		// Clear values for all attachments written in the fragment shader
		std::array<VkClearValue, 4> clearValues;
		clearValues[0].color = clearValues[1].color = clearValues[2].color = { { 0.0f, 0.0f, 0.0f, 0.0f } };
		clearValues[3].depthStencil = { 1.0f,0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::GenRenderPassBeginInfo();
		renderPassBeginInfo.renderPass = offscreenframeBuffers->renderPass;
		renderPassBeginInfo.framebuffer = offscreenframeBuffers->framebuffer;
		renderPassBeginInfo.renderArea.extent.width = offscreenframeBuffers->width;
		renderPassBeginInfo.renderArea.extent.height = offscreenframeBuffers->height;
		renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		renderPassBeginInfo.pClearValues = clearValues.data();

		VK_CHECK_RESULT(vkBeginCommandBuffer(offscreenCmdBuffer, &cmdBufferBeginInfo));

		vkCmdBeginRenderPass(offscreenCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkViewport viewport = vks::initializers::GenViewport((float)offscreenframeBuffers->width, (float)offscreenframeBuffers->height, 0.0f, 1.0f);
		vkCmdSetViewport(offscreenCmdBuffer, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::GenRect2D(offscreenframeBuffers->width, offscreenframeBuffers->height, 0, 0);
		vkCmdSetScissor(offscreenCmdBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(offscreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, useSampleShading ? pipelines.offscreenSampleShading : pipelines.offscreen);

		VkDeviceSize offsets[1] = { 0 };

		// Background
		vkCmdBindDescriptorSets(offscreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &graphicPassDescriptorSets.background, 0, nullptr);
		models.background.draw(offscreenCmdBuffer);

		// Object
		vkCmdBindDescriptorSets(offscreenCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &graphicPassDescriptorSets.model, 0, nullptr);
		models.model.bindBuffers(offscreenCmdBuffer);
		vkCmdDrawIndexed(offscreenCmdBuffer, models.model.indices.count, 3, 0, 0, 0);

		vkCmdEndRenderPass(offscreenCmdBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(offscreenCmdBuffer));
	}

	void prepareForRendering()
	{
		VulkanExampleBase::prepareForRendering();
		sampleCount = getMaxUsableSampleCount();
		loadAssets();
		deferredSetup();
		prepareUniformBuffers();
		setupDescriptorSetLayoutAndPipelineLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSetAndUpdate();
		buildCommandBuffersForPreRenderPrmitives();
		buildDeferredCommandBuffer();
		prepared = true;
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		// Offscreen rendering

		//Wait for swap chain presentation to finish
		submitInfo.pWaitSemaphores = &semaphores.presentComplete;
		submitInfo.waitSemaphoreCount = 1;
		// Signal ready with offscreen semaphore
		submitInfo.pSignalSemaphores = &offscreenSemaphore;
		submitInfo.signalSemaphoreCount = 1;

		// Submit work
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &offscreenCmdBuffer;
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		// Scene rendering

		// Wait for offscreen semaphore
		submitInfo.pWaitSemaphores = &offscreenSemaphore;
		// Signal ready with render complete semaphore
		submitInfo.pSignalSemaphores = &semaphores.renderComplete;

		// Submit work
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentCmdBufferIndex];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();
	}

	virtual void render()
	{
		if (!prepared)
		{
			return;
		}
		draw();
		if (camera.updated)
		{
			updateUniformBufferOffscreen();
		}
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			if (overlay->comboBox("Display", &debugDisplayTarget, { "Final composition", "Position", "Normals", "Albedo", "Specular" }))
			{
				updateUniformBufferDeferredLights();
			}
			if (overlay->checkBox("MSAA", &useMSAA)) {
				buildCommandBuffersForPreRenderPrmitives();
			}
			if (vulkanDevice->features.sampleRateShading) {
				if (overlay->checkBox("Sample rate shading", &useSampleShading)) {
					buildDeferredCommandBuffer();
				}
			}
		}
	}

private:

};

VULKAN_EXAMPLE_MAIN()