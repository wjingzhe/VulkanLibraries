/*
* Vulkan Example - Using different pipelines in one single renderpass
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "VulkanExampleBase.h"
#include "vulkanglTFModel.h"

#define ENABLE_VALIDATION false

class VulkanExample :public VulkanExampleBase
{
public:
	vkglTF::Model scene;

	vks::Buffer uniformBuffer;

	// Same uniform buffer layout as shader
	struct UBOVS
	{
		glm::mat4 projection;
		glm::mat4 modelView;
		glm::vec4 lightPos = glm::vec4(0.0f, 2.0f, 1.0f, 0.0f);
	} uboVS;


	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	struct
	{
		VkPipeline phong;
		VkPipeline wireframe;
		VkPipeline toon;
	}pipelines;

public:
	VulkanExample() :VulkanExampleBase(ENABLE_VALIDATION) 
	{
		windowTitle = "Pipeline State Objects";
		camera.cameraType = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -10.5f));
		camera.setRotation(glm::vec3(-25.0f, 15.0f, 0.0f));
		camera.setRotationSpeed(0.5f);
		camera.setPerspective(60.0f, (float)(width / 3.0f) / (float)height, 0.1f, 256.0f);
	};
	
	~VulkanExample()
	{
		// Clean up used Vulkan resources
		// Note: Inherited destruct cleans up resources stored in base class
		vkDestroyPipeline(device, pipelines.phong, nullptr);
		if (deviceFeatures.fillModeNonSolid)
		{
			vkDestroyPipeline(device, pipelines.wireframe, nullptr);
		}
		vkDestroyPipeline(device, pipelines.toon, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		uniformBuffer.destroy();
	}

	// Enable physical device features required for this example
	virtual void getEnabledFeatures()
	{
		// Fill mode non solid is required for wireframe display
		if (deviceFeatures.fillModeNonSolid)
		{
			curEnabledDeviceFeatures.fillModeNonSolid = VK_TRUE;

			//Wide lines must be present for line width > 1.0f
			if (deviceFeatures.wideLines)
			{
				curEnabledDeviceFeatures.wideLines = VK_TRUE;
			}
		}
	}

	void buildCommandBuffersForPreRenderPrmitives()
	{
		VkCommandBufferBeginInfo cmdBufferBeginInfo = vks::initializers::GenCommandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
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
			//Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufferBeginInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::GenRect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);
			scene.bindBuffers(drawCmdBuffers[i]);

			// Left : Solid colored
			viewport.width = (float)width / 3.0f;
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.phong);
			scene.draw(drawCmdBuffers[i]);

			// Center : Toon
			viewport.x = (float)width / 3.0f;
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.toon);
			// Line width > 1.0f only if wide lines feature is supported
			if (deviceFeatures.wideLines)
			{
				vkCmdSetLineWidth(drawCmdBuffers[i], 2.0f);
			}
			scene.draw(drawCmdBuffers[i]);

			if (deviceFeatures.fillModeNonSolid)
			{
				//Right : Wireframe
				viewport.x = (float)width / 3.0f + (float)width / 3.0f;
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.wireframe);
				scene.draw(drawCmdBuffers[i]);
			}

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}//for

	}//buildCommandBuffersForPreRenderPrmitives

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		scene.loadFromFile(getAssetPath() + "models/treasure_smooth.gltf", vulkanDevice, queue, glTFLoadingFlags);
	}//loadAssets

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSize = 
		{
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::GenDescriptorPoolCreateInfo(poolSize.size(), poolSize.data(), 2);
		
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}//setupDescriptorPool

	void setupDescriptorSetLayoutAndPipelineLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = 
		{
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,0)
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayoutCreateInfo = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());

		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCreateInfo, nullptr, &descriptorSetLayout));

		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayout,1);

		//里面可能包含vertex shader和fragment shader的setLayout用于设置uniform buffer，所以统一为pipelineLayout管理
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void setupDescriptorSetAndUpdate()
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets = { vks::initializers::GenWriteDescriptorSet(descriptorSet,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&uniformBuffer.descriptorBufferInfo) };

		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::GenPipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::GenPipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::GenPipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::GenPipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo = vks::initializers::GenPipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::GenPipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multiSampleState = vks::initializers::GenPipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_LINE_WIDTH };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::GenPipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::GenPipelineCreateInfo(pipelineLayout, renderPass);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multiSampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilStateCreateInfo;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = shaderStages.size();
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState(
			{vkglTF::VertexComponent::Position,vkglTF::VertexComponent::Normal,vkglTF::VertexComponent::Color});

		// Create the graphics pipeline state objects

		// We are using this pipeline as the base for the other pipelines(derivatives)
		// Pipeline derivatives can be used for pipelines that share most of their state
		// Depending on the implementation this may result in better performance for pipeline switching and faster creation time
		pipelineCI.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;

		// Textured pipeline
		// Phong shading pipeline
		shaderStages[0] = loadShader(getShadersPath() + "pipelines/phong.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "pipelines/phong.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.phong));

		// All pipelines created after the base pipeline will be derivatives
		pipelineCI.flags = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
		// Base pipeline will be our first created pipeline
		pipelineCI.basePipelineHandle = pipelines.phong;
		// it's only allowed to either use a handle or index for the base pipeline
		// As we use the handle,we must set the index to -1(see section 9.5 of the specification)
		pipelineCI.basePipelineIndex = -1;

		// Toon shading pipeline
		shaderStages[0] = loadShader(getShadersPath() + "pipelines/toon.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "pipelines/toon.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.toon));

		// Pipeline for wire frame rendering
		// Non solid rendering is not a mandatory
		if (deviceFeatures.fillModeNonSolid)
		{
			rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
			shaderStages[0] = loadShader(getShadersPath() + "pipelines/wireframe.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
			shaderStages[1] = loadShader(getShadersPath() + "pipelines/wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.wireframe));
		}
	}//preparePipelines

	//Prepare and Initialize uniform buffer containing shader uniforms
	void prepareUnifomrBuffers()
	{
		//创建数据对象
		 // Create the vertex shader uniform buffer block
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&uniformBuffer,sizeof(UBOVS)));

		// 数据赋值
		// Map persisitent
		VK_CHECK_RESULT(uniformBuffer.map());
		updateUniformBuffers();
	}//prepareUnifomrBuffers

	void updateUniformBuffers()
	{
		uboVS.projection = camera.matrices.perspective;
		uboVS.modelView = camera.matrices.view;
		
		memcpy(uniformBuffer.mappedData, &uboVS, sizeof(UBOVS));
	}//updateUniformBuffers

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentCmdBufferIndex];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();
	}//submit draw cmd and present

	void prepareForRendering() override
	{
		VulkanExampleBase::prepareForRendering();
		loadAssets();
		prepareUnifomrBuffers();
		setupDescriptorSetLayoutAndPipelineLayout();
		preparePipelines();
		//实际的管线状态要组装或读写uniform，还需要描述符Set对象
		setupDescriptorPool();
		setupDescriptorSetAndUpdate();
		buildCommandBuffersForPreRenderPrmitives();
		prepared = true;
	}//prepareForRendering

	virtual void render()override
	{
		if (!prepared)
		{
			return;
		}
		draw();
		
		//为下一帧drawCmdBuffer更新准备环境
		if (camera.updated)
		{
			updateUniformBuffers();
		}
	}//render

	virtual void OnUpdateUIOverlay(vks::UIOverlay * overlay)
	{
		if (!deviceFeatures.fillModeNonSolid)
		{
			if (overlay->header("Info"))
			{
				overlay->text("Non solid fill modes not supported");
			}
		}
	}//OnUpdateUIOverlay

private:

};

VULKAN_EXAMPLE_MAIN()