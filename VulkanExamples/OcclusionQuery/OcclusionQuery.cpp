/*
* Vulkan Example - Using occlusion query for visibility testing
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "VulkanExampleBase.h"
#include "VulkanglTFModel.h"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

class VulkanExample :public VulkanExampleBase
{
public:

	struct {
		vkglTF::Model teapot;
		vkglTF::Model plane;
		vkglTF::Model sphere;
	} models;

	struct {
		vks::Buffer occluder;
		vks::Buffer teapot;
		vks::Buffer sphere;
	} uniformBuffers;

	struct UBOVS {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
		glm::vec4 color = glm::vec4(0.0f);
		glm::vec4 lightPos = glm::vec4(10.0f, -10.0f, 10.0f, 1.0f);
		float visible;
	} uboVS;

	struct
	{
		VkDescriptorSet teapot;
		VkDescriptorSet sphere;
	} geometryDescriptorSets;

	VkDescriptorSet occluderDescriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout pipelineLayout;

	struct
	{
		VkPipeline solid;
		VkPipeline occluder;
		// Pipeline with basic shaders used for occlusion pass
		VkPipeline simple;
	} pipelines;

	// Pool that stores all occlusion queries
	VkQueryPool queryPool;

	// Passed query samples
	uint64_t passedSamples[2] = { 1,1 };

	VulkanExample():VulkanExampleBase(ENABLE_VALIDATION)
	{
		windowTitle = "Occlusion queries";

		camera.cameraType = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -7.5f));
		camera.setRotation(glm::vec3(0.0f, -123.75f, 0.0f));
		camera.setRotationSpeed(0.5f);
		camera.setPerspective(60.0f, (float)width / (float)height, 1.0f, 256.0f);
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources
		// Note : Inherited destructor cleans up resources stored in base class
		vkDestroyPipeline(device, pipelines.solid, nullptr);
		vkDestroyPipeline(device, pipelines.occluder, nullptr);
		vkDestroyPipeline(device, pipelines.simple, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		vkDestroyQueryPool(device, queryPool, nullptr);

		uniformBuffers.occluder.destroy();
		uniformBuffers.sphere.destroy();
		uniformBuffers.teapot.destroy();
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.plane.loadFromFile(getAssetPath() + "models/plane_z.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.teapot.loadFromFile(getAssetPath() + "models/teapot.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.sphere.loadFromFile(getAssetPath() + "models/sphere.gltf", vulkanDevice, queue, glTFLoadingFlags);
	}

	void setupQueryPool()
	{
		VkQueryPoolCreateInfo queryPoolInfo = {};
		queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
		queryPoolInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
		queryPoolInfo.queryCount = 2;
		VK_CHECK_RESULT(vkCreateQueryPool(device, &queryPoolInfo, NULL, &queryPool));
	}

	void updateUniformBuffers()
	{
		uboVS.projection = camera.matrices.perspective;
		uboVS.view = camera.matrices.view;

		uint8_t* pData;
		// Occluder plane
		uboVS.visible = 1.0f;
		uboVS.model = glm::scale(glm::mat4(1.0f), glm::vec3(6.0f));
		uboVS.color = glm::vec4(0.0f, 0.0f, 1.0f, 0.5f);
		memcpy(uniformBuffers.occluder.mappedData, &uboVS, sizeof(uboVS));

		// Teapot
		// Toggle color depending on visibility
		uboVS.visible = (passedSamples[0] > 0) ? 1.0f : 0.0f;
		uboVS.model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -3.0f));
		uboVS.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
		memcpy(uniformBuffers.teapot.mappedData, &uboVS, sizeof(uboVS));

		// Sphere
		// Toggle color depending on visibility
		uboVS.visible = (passedSamples[1] > 0) ? 1.0f : 0.0f;
		uboVS.model = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 3.0f));
		uboVS.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
		memcpy(uniformBuffers.sphere.mappedData, &uboVS, sizeof(uboVS));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Vertex shader uniform buffer block
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.occluder, sizeof(uboVS)));

		// Teapot
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.teapot, sizeof(uboVS)));

		// Sphere
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.sphere, sizeof(uboVS)));


		// Map persistent
		VK_CHECK_RESULT(uniformBuffers.occluder.map());
		VK_CHECK_RESULT(uniformBuffers.teapot.map());
		VK_CHECK_RESULT(uniformBuffers.sphere.map());

		updateUniformBuffers();
	}

	void setupDescriptorSetLayoutAndPipelineLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,0),
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayoutCI = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCI, nullptr, &descriptorSetLayout));

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::GenPipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::GenPipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::GenPipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::GenPipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::GenPipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::GenPipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::GenPipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::GenPipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::GenPipelineCreateInfo(pipelineLayout, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = shaderStages.size();
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position,vkglTF::VertexComponent::Normal,vkglTF::VertexComponent::Color });

		// Solid rendering pipeline
		shaderStages[0] = loadShader(getShaderPath() + "occlusionquery/mesh.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "occlusionquery/mesh.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.solid));

		// Basic pipeline for coloring occluded objects
		shaderStages[0] = loadShader(getShaderPath() + "occlusionquery/simple.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "occlusionquery/simple.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		rasterizationStateCI.cullMode = VK_CULL_MODE_NONE;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.simple));

		// Visual pipeline for the occluder plane
		shaderStages[0] = loadShader(getShaderPath() + "occlusionquery/occluder.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "occlusionquery/occluder.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Enable blending
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.occluder));
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			// One uniform buffer block for each mesh
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,3)
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vks::initializers::GenDescriptorPoolCreateInfo(poolSizes.size(), poolSizes.data(), 3);

		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSets()
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);

		// Occluder plane
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &occluderDescriptorSet));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0: Vertex Shader uniform buffer
			vks::initializers::GenWriteDescriptorSet(occluderDescriptorSet,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&uniformBuffers.occluder.descriptorBufferInfo),
		};

		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Teapot
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &geometryDescriptorSets.teapot));
		writeDescriptorSets[0].dstSet = geometryDescriptorSets.teapot;
		writeDescriptorSets[0].pBufferInfo = &uniformBuffers.teapot.descriptorBufferInfo;
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Sphere
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &geometryDescriptorSets.sphere));
		writeDescriptorSets[0].dstSet = geometryDescriptorSets.sphere;
		writeDescriptorSets[0].pBufferInfo = &uniformBuffers.sphere.descriptorBufferInfo;
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
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

		for (size_t i = 0; i < drawCmdBuffers.size(); i++)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			//开启当前录制状态
			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufferBeginInfo));
			
			// 使用当前查询Pass
			// Reset query pool,清理查询池状态
			// Must be done outside of render pass
			vkCmdResetQueryPool(drawCmdBuffers[i], queryPool, 0, 2);

			// 开启当前Pass绘制状态,先绘制遮挡物体
			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::GenRect2D(width,height,0,0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			VkDeviceSize offsets[1] = { 0 };
			
			glm::mat4 modelMatrix = glm::mat4(1.0f);

			//// Occulsion pass
			//vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.simple);

			//// 先绘制遮挡物，类似于pre-z
			//vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &occluderDescriptorSet, 0, nullptr);
			//models.plane.draw(drawCmdBuffers[i]);

			//{//证实：查询结果是在RT上保存的
			//	VkClearAttachment clearAttachments[2] = {};
			//	clearAttachments[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			//	clearAttachments[0].clearValue.color = defaultClearColor;
			//	clearAttachments[0].colorAttachment = 0;
			//	clearAttachments[1].aspectMask = VK_IMAGE_ASPECT_NONE;
			//	clearAttachments[1].clearValue.depthStencil = { 1.0f,0 };

			//	VkClearRect clearRect = {};
			//	clearRect.layerCount = 1;
			//	clearRect.rect.offset = { 0,0 };
			//	clearRect.rect.extent = { width,height };

			//	vkCmdClearAttachments(drawCmdBuffers[i], 2, clearAttachments, 1, &clearRect);
			//}

			////创建Teapot的查询操作
			//vkCmdBeginQuery(drawCmdBuffers[i], queryPool, 0, VK_FLAGS_NONE);
			//vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &geometryDescriptorSets.teapot, 0, nullptr);
			//models.teapot.draw(drawCmdBuffers[i]);
			//vkCmdEndQuery(drawCmdBuffers[i], queryPool,0);

			////创建Spere的查询操作
			//vkCmdBeginQuery(drawCmdBuffers[i], queryPool, 1, VK_FLAGS_NONE);
			//vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &geometryDescriptorSets.sphere, 0, nullptr);
			//models.sphere.draw(drawCmdBuffers[i]);
			//vkCmdEndQuery(drawCmdBuffers[i], queryPool, 1);

			//好好画个深度不好吗？？
			// Clear color and depth attachments
			VkClearAttachment clearAttachments[2] = {};
			clearAttachments[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			clearAttachments[0].clearValue.color = defaultClearColor;
			clearAttachments[0].colorAttachment = 0;

			clearAttachments[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			clearAttachments[1].clearValue.depthStencil = { 1.0f,0 };

			VkClearRect clearRect = {};
			clearRect.layerCount = 1;
			clearRect.rect.offset = { 0,0 };
			clearRect.rect.extent = { width,height };

			vkCmdClearAttachments(drawCmdBuffers[i], 2, clearAttachments, 1, &clearRect);

			// 开启可见几何图形绘制
			
			// Occluder
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.occluder);
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &occluderDescriptorSet, 0, NULL);
			models.plane.draw(drawCmdBuffers[i]);

			// Visible pass
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.solid);

			// Teapot
			vkCmdBeginQuery(drawCmdBuffers[i], queryPool, 0, VK_FLAGS_NONE);
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &geometryDescriptorSets.teapot, 0, NULL);
			models.teapot.draw(drawCmdBuffers[i]);
			vkCmdEndQuery(drawCmdBuffers[i], queryPool, 0);

			// Sphere
			vkCmdBeginQuery(drawCmdBuffers[i], queryPool, 1, VK_FLAGS_NONE);
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &geometryDescriptorSets.sphere, 0, NULL);
			models.sphere.draw(drawCmdBuffers[i]);
			vkCmdEndQuery(drawCmdBuffers[i], queryPool, 1);

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}//for
	}

	void prepareForRendering()
	{
		VulkanExampleBase::prepareForRendering();
		loadAssets();
		setupQueryPool();
		prepareUniformBuffers();
		setupDescriptorSetLayoutAndPipelineLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSets();
		buildCommandBuffersForPreRenderPrmitives();
		prepared = true;
	}

	// Retrieves the results of the occlusion queries submitted to the command buffer
	void getQueryResults()
	{
		// We use vkGetQueryResults to copy the results into a host visible buffer
		vkGetQueryPoolResults(device, queryPool, 0, 2, sizeof(passedSamples), passedSamples, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
		// Store results a 64 bit values and wait until the results have been finished
		// If you don't want to wait, you can use VK_QUERY_RESULT_WITH_AVAILABILITY_BIT
		// which also returns the state of the result (ready) in the result
	}

	void draw()
	{
		updateUniformBuffers();
		VulkanExampleBase::prepareFrame();

		// Command buffer to be  submitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentCmdBufferIndex];

		// Submit to queue
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		// Read query results for displaying in next frame
		getQueryResults();

		VulkanExampleBase::submitFrame();
	}

	virtual void render()override
	{
		if (!prepared)
			return;
		draw();
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)override
	{
		if (overlay->header("Occlusion query results")) {
			overlay->text("Teapot: %d samples passed", passedSamples[0]);
			overlay->text("Sphere: %d samples passed", passedSamples[1]);
		}
	}

private:

};

VULKAN_EXAMPLE_MAIN()