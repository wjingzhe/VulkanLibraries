/*
* Vulkan Example - Push constants example (small shader block accessed outside of uniforms for fast updates)
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

/*
* Summary:
* Using push constants it's possible to pass a small bit of static data to a shader, which is stored in the command buffer stat
* This is perfect for passing e.g. static per-object data or parameters without the need for descriptor sets
* The sample uses these to push different static parameters for rendering multiple objects
*/

#include <cmath>
#include "VulkanExampleBase.h"
#include "VulkanglTFModel.h"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

float RandomFloat()
{
	return ((float)rand() / (RAND_MAX));
}

class VulkanExample:public VulkanExampleBase
{
public:
	vkglTF::Model model;

	// Color and position data for each sphere is uploaded using push constants
	struct SpherePushConstantData
	{
		glm::vec4 color;
		glm::vec4 position;
	};

	std::array<SpherePushConstantData, 16> spheres;

	vks::Buffer uniformBuffer;

	struct UBOMatrices
	{
		glm::mat4 projection;
		glm::mat4 model;
		glm::mat4 view;
	} uboMatrices;

	VkPipeline pipeline;
	VkPipelineLayout pipelineLayout;

	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	VulkanExample() :VulkanExampleBase(ENABLE_VALIDATION)
	{
		windowTitle = "Push constants";

		camera.cameraType = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -10.0f));
		camera.setRotation(glm::vec3(0.0f, 0.0f, 0.0f));
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);
		camera.setRotationSpeed(0.5f);
	};


	~VulkanExample()
	{
		// Clean up used Vulkan resources
		// Note : Inherited destructor cleans up resources stored in base class
		vkDestroyPipeline(device, pipeline, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		uniformBuffer.destroy();
	}

	void setupSpheres()
	{
		// Setup random colors and fixed positions for every spheres in the scene
		for (uint32_t i = 0; i < spheres.size(); i++)
		{
			spheres[i].color = glm::vec4(RandomFloat(), RandomFloat(), RandomFloat(), 1.0f);
			const float radian = glm::radians(i*360.0f / static_cast<uint32_t>(spheres.size()));
			spheres[i].position = glm::vec4(glm::vec3(std::sin(radian), std::cos(radian), 0.0f)*3.5f, 1.0f);
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

		//∂‡ª∫≥Â√¸¡Ó¬º÷∆
		for (int32_t i = 0; i < drawCmdBuffers.size(); i++)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufferBeginInfo));
	
			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vks::initializers::GenRect2D(width, height, 0, 0);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

			// Render the spheres passing color and position via push constants
			uint32_t sphereCount = static_cast<uint32_t>(spheres.size());
			for (uint32_t j = 0; j < sphereCount; j++)
			{
				// Pass static sphere data as push constants
				vkCmdPushConstants(drawCmdBuffers[i], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SpherePushConstantData), &spheres[j]);
				model.draw(drawCmdBuffers[i]);
			}

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}//for_i
	}//buildCommandBuffersForPreRenderPrmitives

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		model.loadFromFile(getAssetPath() + "models/sphere.gltf", vulkanDevice, queue, glTFLoadingFlags);
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizes = { vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1) };
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::GenDescriptorPoolCreateInfo(poolSizes, 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetLayoutAndPipelineLayout()
	{
		// Binding 0: Vertex shader uniform buffer
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = { vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,0) };
		VkDescriptorSetLayoutCreateInfo desctiptorLayout = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &desctiptorLayout, nullptr, &descriptorSetLayout));

		//Define the push constant rang used by the pipeline layout
		// Note that the spec only requires a minimum of 128 bytes,so for passing larger blocks of data you'd use UBOs or SSBOs
		VkPushConstantRange pushConstantRange{};
		pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		pushConstantRange.offset = 0;
		pushConstantRange.size = sizeof(SpherePushConstantData);

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));
	}

	void setupDescriptorSetAndUpdate()
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet));

		// Write Binding 0: Vertex shader uniform buffer
		VkWriteDescriptorSet writeDescriptorSet = vks::initializers::GenWriteDescriptorSet(descriptorSet, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffer.descriptorBufferInfo);

		vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, NULL);
	}

	void preparePipelines()
	{
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo = vks::initializers::GenPipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo = vks::initializers::GenPipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::GenPipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo = vks::initializers::GenPipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::GenPipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCreateInfo = vks::initializers::GenPipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multiSampleStateCreateInfo = vks::initializers::GenPipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = vks::initializers::GenPipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;
		shaderStages[0] = loadShader(getShaderPath() + "pushconstants/pushconstants.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "pushconstants/pushconstants.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::GenPipelineCreateInfo(pipelineLayout, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCreateInfo;
		pipelineCI.pRasterizationState = &rasterizationStateCreateInfo;
		pipelineCI.pColorBlendState = &colorBlendStateCreateInfo;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pViewportState = &viewportStateCreateInfo;
		pipelineCI.pMultisampleState = &multiSampleStateCreateInfo;
		pipelineCI.pDynamicState = &dynamicStateCreateInfo;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position,vkglTF::VertexComponent::Normal,vkglTF::VertexComponent::Color });
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipeline));
	}

	void prepareUniformBuffers()
	{
		// Vertex shader uniform buffer block
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&uniformBuffer,sizeof(uboMatrices)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffer.map());
		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		uboMatrices.projection = camera.matrices.perspective;
		uboMatrices.view = camera.matrices.view;
		uboMatrices.model = glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));

		memcpy(uniformBuffer.mappedData, &uboMatrices, sizeof(uboMatrices));
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		// Command buffer to be submitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentCmdBufferIndex];

		// submit to queue
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

		VulkanExampleBase::submitFrame();
	}

	void prepareForRendering()
	{
		VulkanExampleBase::prepareForRendering();
		loadAssets();
		setupSpheres();
		prepareUniformBuffers();
		setupDescriptorSetLayoutAndPipelineLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSetAndUpdate();
		buildCommandBuffersForPreRenderPrmitives();

		prepared = true;
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
			updateUniformBuffers();
		}
	}


private:

};

VULKAN_EXAMPLE_MAIN()