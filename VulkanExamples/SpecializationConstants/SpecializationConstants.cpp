/*
* Vulkan Example - Shader specialization constants
*
* For details see https://www.khronos.org/registry/vulkan/specs/misc/GL_KHR_vulkan_glsl.txt
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "VulkanExampleBase.h"
#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION false

class VulkanExample:public VulkanExampleBase  
{
public:
	vkglTF::Model scene;
	vks::Texture2D colorMap;
	vks::Buffer uniformBuffer;

	// Same uniform buffer layout as shader
	struct UBOVS
	{
		glm::mat4 projection;
		glm::mat4 modelView;
		glm::vec4 lightPos = glm::vec4(0.0f, -2.0f, 1.0f, 0.0f);
	} uboVS;

	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	struct
	{
		VkPipeline phong;
		VkPipeline toon;
		VkPipeline textured;
	} pipelines;

	VulkanExample()
	{
		windowTitle = "Specialization constants";
		camera.cameraType = Camera::CameraType::lookat;
		camera.setPerspective(60.0f, ((float)width / 3.0f) / (float)height, 0.1f, 512.0f);
		camera.setRotation(glm::vec3(-40.0f, -90.0f, 0.0f));
		camera.setTranslation(glm::vec3(0.0f, 0.0f, -2.0f));
	}

	~VulkanExample()
	{
		vkDestroyPipeline(device, pipelines.phong, nullptr);
		vkDestroyPipeline(device, pipelines.textured, nullptr);
		vkDestroyPipeline(device, pipelines.toon, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		colorMap.destroy();
		uniformBuffer.destroy();
	}

	void loadAssets()
	{
		scene.loadFromFile(getAssetPath() + "models/color_teapot_spheres.gltf", vulkanDevice, queue, vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY);
		colorMap.loadFromFile(getAssetPath() + "textures/metalplate_nomips_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Create the vertex shader uniform buffer block
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffer, sizeof(uboVS)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffer.map());

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		camera.setPerspective(60.0f, ((float)width / 3.0f) / (float)height, 0.1f, 512.0f);
		uboVS.projection = camera.matrices.perspective;
		uboVS.modelView = camera.matrices.view;

		memcpy(uniformBuffer.mappedData, &uboVS, sizeof(uboVS));
	}

	void setupDescriptorSetLayoutAndPipelineLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = 
		{
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,0),
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,1)
		};

		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayout));

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayout,1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssembleStateCreateInfo = vks::initializers::GenPipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,0,VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo = vks::initializers::GenPipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::GenPipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo = vks::initializers::GenPipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo = vks::initializers::GenPipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCreateInfo = vks::initializers::GenPipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multiSampleStateCreateInfo = vks::initializers::GenPipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_LINE_WIDTH };
		VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = vks::initializers::GenPipelineDynamicStateCreateInfo(dynamicStateEnables);


		// Prepare specialization data

		// Host data to take specialization constants from
		struct SpecilizationData
		{
			// Sets the lighting model used in the fragment "uber" shader
			uint32_t lightingModel;
			// Parameter for the toon shading part of the fragment shader
			float toonDesaturationFactor = 0.5f;
		} specializationData;

		// Each shader constant of a shader stage correspond to one map entry
		std::array<VkSpecializationMapEntry, 2> specializationMapEntries;
		// Shader bindings based on specialization constants are marked by the new "constant_id" layout qualifier:
		// layout (constant_id = 0) const int LIGHTING_MODEL = 0;
		// layout (constant_id = 1) const float PARAM_TOON_DESATURATION = 0.0f;

		// Map entry for the lighting model to be used by the fragment shader
		specializationMapEntries[0].constantID = 0;
		specializationMapEntries[0].size = sizeof(specializationData.lightingModel);
		specializationMapEntries[0].offset = 0;

		// Map entry for the toon shader paramater
		specializationMapEntries[1].constantID = 1;
		specializationMapEntries[1].size = sizeof(specializationData.toonDesaturationFactor);
		specializationMapEntries[1].offset = offsetof(SpecilizationData, toonDesaturationFactor);

		// Prepare specialization info block for the shader stage
		VkSpecializationInfo specializationInfo{};
		specializationInfo.dataSize = sizeof(specializationData);
		specializationInfo.mapEntryCount = static_cast<uint32_t>(specializationMapEntries.size());
		specializationInfo.pMapEntries = specializationMapEntries.data();
		specializationInfo.pData = &specializationData;

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStageCreateInfos;
		// all pipelines will use the same "uber" shader and specialization constants to change branching and paramater of that shader
		shaderStageCreateInfos[0] = loadShader(getShaderPath() + "specializationconstants/uber.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStageCreateInfos[1] = loadShader(getShaderPath() + "specializationconstants/uber.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Specialization info is assigned is part of the shader stage (modul) and must be set after creating the module and before creating the pipeline
		shaderStageCreateInfos[1].pSpecializationInfo = &specializationInfo;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::GenPipelineCreateInfo(pipelineLayout, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssembleStateCreateInfo;
		pipelineCI.pRasterizationState = &rasterizationStateCreateInfo;
		pipelineCI.pColorBlendState = &colorBlendStateCreateInfo;
		pipelineCI.pMultisampleState = &multiSampleStateCreateInfo;
		pipelineCI.pViewportState = &viewportStateCreateInfo;
		pipelineCI.pDepthStencilState = &depthStencilStateCreateInfo;
		pipelineCI.pDynamicState = &dynamicStateCreateInfo;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStageCreateInfos.size());
		pipelineCI.pStages = shaderStageCreateInfos.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position,vkglTF::VertexComponent::Normal,vkglTF::VertexComponent::UV,vkglTF::VertexComponent::Color });

		// Solid phong shading
		specializationData.lightingModel = 0;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.phong));

		// Phong and textured
		specializationData.lightingModel = 1;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.toon));

		// Textured discard
		specializationData.lightingModel = 2;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.textured));
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSize = 
		{
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1),
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1),
		};

		VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = vks::initializers::GenDescriptorPoolCreateInfo(static_cast<uint32_t>(poolSize.size()), poolSize.data(), 1);

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetAndUpdate()
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			vks::initializers::GenWriteDescriptorSet(descriptorSet,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&uniformBuffer.descriptorBufferInfo),
			vks::initializers::GenWriteDescriptorSet(descriptorSet,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,&colorMap.descriptorImageInfo),
		};

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL);
	}

	void buildCommandBuffersForPreRenderPrmitives()
	{
		VkCommandBufferBeginInfo cmdBufferBeginInfo = vks::initializers::GenCommandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };

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
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufferBeginInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkRect2D scissor = vks::initializers::GenRect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

			VkDeviceSize offset[1] = { 0 };

			// Left
			VkViewport viewport = vks::initializers::GenViewport((float)width / 3.0f, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i],0, 1, &viewport);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.phong);
			scene.draw(drawCmdBuffers[i]);

			// Middle
			viewport.x = (float)width / 3.0f;
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.toon);
			scene.draw(drawCmdBuffers[i]);

			// Right
			viewport.x = (float)width / 3.0f+ (float)width / 3.0f;
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.textured);
			scene.draw(drawCmdBuffers[i]);

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}
		
	}

	void prepareForRendering()
	{
		VulkanExampleBase::prepareForRendering();
		loadAssets();
		prepareUniformBuffers();
		setupDescriptorSetLayoutAndPipelineLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSetAndUpdate();
		buildCommandBuffersForPreRenderPrmitives();
		prepared = true;
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentCmdBufferIndex];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();
	}

	virtual void render()
	{
		if (!prepared) {
			return;
		}
		draw();
		if (camera.updated) {
			updateUniformBuffers();
		}
	}

	virtual void windowResized()
	{
		updateUniformBuffers();
	}

private:

};

VULKAN_EXAMPLE_MAIN()