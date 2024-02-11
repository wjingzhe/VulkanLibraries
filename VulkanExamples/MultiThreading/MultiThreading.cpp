/*
* Vulkan Example - Multi threaded command buffer generation and rendering
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "VulkanExampleBase.h"

#include "threadpool.hpp"
#include "frustum.hpp"

#include "VulkanglTFModel.h"

#define ENABLE_VALIDATION false

class VulkanExample : public VulkanExampleBase
{
public:

	bool displayStarSphere = true;
	bool starBackgroundCmdBufferCacheDirty = true;
	uint32_t tempBackgroundCmdUpdatedFrameIndex = 0;
	bool userInterfaceCmdCacheDirty = true;
	uint32_t tempUserInterfaceCmdUpdatedFrameIndex = 0;

	struct
	{
		vkglTF::Model ufo;
		vkglTF::Model starSphere;
	} models;

	// Shared matrices used for thread push constant blocks
	struct
	{
		glm::mat4 projection;
		glm::mat4 view;
	} matrices;

	struct
	{
		VkPipeline phong;
		VkPipeline starsphere;
	} pipelines;

	VkPipelineLayout pipelineLayout;

	VkCommandBuffer primaryCommandBuffer;

	// Secondary scene command buffers used to store backdrop and user interface
	struct SecondaryCommandBuffers
	{
		std::vector<VkCommandBuffer>backgrounds;
		//VkCommandBuffer background;

		std::vector<VkCommandBuffer>userInterfaces;
		//VkCommandBuffer ui;
	} secondaryCommandBuffers;

	// Number of animated objects to be renderer
	// by using threads and secondary command buffers
	uint32_t numObjectsPerThread;

	// Multi Threaded stuff
	// Max. number of concurrent threads
	uint32_t numThreads;

	// Use push constants to update shader
	// parameters on a per-thread base
	struct ThreadPushConstantBlock
	{
		glm::mat4 MVP;
		glm::vec3 color;
	};

	struct ObjectData
	{
		glm::mat4 model;
		glm::vec3 pos;
		glm::vec3 rotation;
		float rotationDir;
		float rotationSpeed;
		float scale;
		float deltaT;
		float stateT = 0;
		bool visible = true;
	};

	struct ThreadData
	{
		VkCommandPool commandPool;
		// One command buffer per render object
		std::vector<VkCommandBuffer> commandBuffers;
		// One push constant block per render object
		std::vector<ThreadPushConstantBlock> pushConstBlocks;
		// Per object information(Position,rotation.etc)
		std::vector<ObjectData>objectDatas;
	};
	std::vector<ThreadData> threadDatas;

	vks::ThreadPool threadPool;

	// Fence to wait for all command buffers to finish before
	// presenting to the swap chain
	VkFence renderFence = {};

	// View frustum for culling invisible objects
	vks::Frustum frustum;

	std::default_random_engine  rndEngine;

public:
	VulkanExample():VulkanExampleBase(ENABLE_VALIDATION)
	{
		//多缓存更新cache计数
		starBackgroundCmdBufferCacheDirty = true;
		tempBackgroundCmdUpdatedFrameIndex = 0;
		userInterfaceCmdCacheDirty = true;
		tempUserInterfaceCmdUpdatedFrameIndex = 0;

		windowTitle = "Multi Theaded Command Buffer";
		camera.cameraType = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, -0.0f, -32.5f));
		camera.setRotation(glm::vec3(0.0f));
		camera.setRotationSpeed(0.5f);
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 256.0f);

		// Get number of max.concurrent theads
		numThreads = std::thread::hardware_concurrency();
		assert(numThreads > 0);

#if defined(__ANDROID__)
		LOGD("numThreads = %d", numThreads);
#else
		std::cout << "numThreads = " << numThreads << std::endl;
#endif // defined(__ANDROID__)

		threadPool.setThreadCount(numThreads);
		numObjectsPerThread = 512 / numThreads;
		rndEngine.seed(benchmark.active ? 0 : (unsigned)time(nullptr));
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources
		// Note :Inherited destructor cleans up resources stored in base class
		vkDestroyPipeline(device, pipelines.phong, nullptr);
		vkDestroyPipeline(device, pipelines.starsphere, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);

		for (auto& thread :threadDatas )
		{
			vkFreeCommandBuffers(device, thread.commandPool, thread.commandBuffers.size(), thread.commandBuffers.data());
			vkDestroyCommandPool(device, thread.commandPool, nullptr);
		}

		vkDestroyFence(device, renderFence, nullptr);
	}

	float rnd(float range)
	{
		std::uniform_real_distribution<float> rndDist(0.0f, range);

		return rndDist(rndEngine);
	}

	// Create all threads and initialize shader push constants
	void prepareMultiThreadedRenderer()
	{
		// Since this demo updates the command buffers on each frame
		// We don't use the per-framebuffer command buffers from the
		// base class,and create a single primary command buffer instead
		VkCommandBufferAllocateInfo cmdBufAllocateInfo = vks::initializers::GenCommandBufferAllocateInfo(cmdPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &primaryCommandBuffer));

		// Create additional secondary CBs for background and ui
		cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
		for (size_t i = 0; i < swapChain.imageCount; i++)
		{
			secondaryCommandBuffers.backgrounds.push_back(VK_NULL_HANDLE);
			VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &secondaryCommandBuffers.backgrounds[i]));

			secondaryCommandBuffers.userInterfaces.push_back(VK_NULL_HANDLE);
			VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &secondaryCommandBuffers.userInterfaces[i]));
		}

		threadDatas.resize(numThreads);

		float maxX = std::floor(std::sqrt(numThreads*numObjectsPerThread));
		uint32_t posX = 0;
		uint32_t posZ = 0;

		for (uint32_t i = 0; i < numThreads; i++)
		{
			ThreadData * thread = &threadDatas[i];

			//Create one command pool for each thread
			VkCommandPoolCreateInfo cmdPoolInfo = vks::initializers::GenCommandPoolCreateInfo();
			cmdPoolInfo.queueFamilyIndex = swapChain.queueNodeIndex;
			cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &thread->commandPool));

			// One secondary command buffer per object that is updated by this thread
			thread->commandBuffers.resize(numObjectsPerThread);
			// Generate secondary command buffers for each thread
			VkCommandBufferAllocateInfo secondaryCmdBufAllocateInfo = 
				vks::initializers::GenCommandBufferAllocateInfo(thread->commandPool,VK_COMMAND_BUFFER_LEVEL_SECONDARY,thread->commandBuffers.size());
			VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &secondaryCmdBufAllocateInfo, thread->commandBuffers.data()));

			thread->pushConstBlocks.resize(numObjectsPerThread);
			thread->objectDatas.resize(numObjectsPerThread);

			for (uint32_t j = 0; j < numObjectsPerThread; j++)
			{
				float theta = 2.0f*float(M_PI)*rnd(1.0f);
				float phi = acos(1.0f - 2.0f*rnd(1.0f));
				thread->objectDatas[j].pos = glm::vec3(sin(phi) * cos(theta), 0.0f, cos(phi)) * 35.0f;

				thread->objectDatas[j].rotation = glm::vec3(0.0f, rnd(360.0f), 0.0f);
				thread->objectDatas[j].deltaT = rnd(1.0f);
				thread->objectDatas[j].rotationDir = (rnd(100.0f) < 50.0f) ? 1.0f : -1.0f;
				thread->objectDatas[j].rotationSpeed = (2.0f + rnd(4.0f)) * thread->objectDatas[j].rotationDir;
				thread->objectDatas[j].scale = 0.75f + rnd(0.5f);

				thread->pushConstBlocks[j].color = glm::vec3(rnd(1.0f), rnd(1.0f), rnd(1.0f));
			}//for_j
		}//for_i
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.ufo.loadFromFile(getAssetPath() + "models/retroufo_red_lowpoly.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.starSphere.loadFromFile(getAssetPath() + "models/sphere.gltf", vulkanDevice, queue, glTFLoadingFlags);
	}

	void setupPipelineLayout()
	{
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(nullptr, 0);

		// Push constants for model matrices
		VkPushConstantRange pushConstantRange = vks::initializers::GenPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, sizeof(ThreadPushConstantBlock), 0);

		// Push constant ranges are part of the pipeline layout
		pipelineLayoutCreateInfo.pushConstantRangeCount = 1;
		pipelineLayoutCreateInfo.pPushConstantRanges = &pushConstantRange;

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

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::GenPipelineCreateInfo(pipelineLayout, renderPass,0);
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCreateInfo;
		pipelineCI.pRasterizationState = &rasterizationStateCreateInfo;
		pipelineCI.pColorBlendState = &colorBlendStateCreateInfo;
		pipelineCI.pDepthStencilState = &depthStencilStateCreateInfo;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pMultisampleState = &multisampleStateCreateInfo;
		pipelineCI.pDynamicState = &dynamicStateCreateInfo;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::Color });

		// Object rendering pipeline
		shaderStages[0] = loadShader(getShaderPath() + "multithreading/phong.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "multithreading/phong.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.phong));

		// Star sphere rendering pipeline
		rasterizationStateCreateInfo.cullMode = VK_CULL_MODE_FRONT_BIT;
		depthStencilStateCreateInfo.depthWriteEnable = VK_FALSE;
		shaderStages[0] = loadShader(getShaderPath() + "multithreading/starsphere.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "multithreading/starsphere.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.starsphere));
	}

	void updateMatrices()
	{
		matrices.projection = camera.matrices.perspective;
		matrices.view = camera.matrices.view;
		frustum.update(matrices.projection*matrices.view);
	}

	void prepareForRendering()override
	{
		VulkanExampleBase::prepareForRendering();

		// Create a fence for synchronization
		VkFenceCreateInfo fenceCreateInfo = vks::initializers::GenFenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
		vkCreateFence(device, &fenceCreateInfo, nullptr, &renderFence);

		loadAssets();
		setupPipelineLayout();
		preparePipelines();
		prepareMultiThreadedRenderer();
		updateMatrices();

		prepared = true;
	}

	void updateSecondaryCommandBuffersForStarBackgroundAndUI()
	{
		// Inheritance info for the secondary command buffers
		VkCommandBufferInheritanceInfo inheritanceInfo = vks::initializers::GenCommandBufferInheriatanceInfo();
		inheritanceInfo.renderPass = renderPass;
		// Secondary command buffer also use the currently active framebuffer
		inheritanceInfo.framebuffer = frameBuffers[currentCmdBufferIndex];

		// Secondary command buffer
		VkCommandBufferBeginInfo commandBufferBeginInfo = vks::initializers::GenCommandBufferBeginInfo();
		commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
		commandBufferBeginInfo.pInheritanceInfo = &inheritanceInfo;

		VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
		VkRect2D scissor = vks::initializers::GenRect2D(width, height, 0, 0);

		/*
		Background
		*/
		if (starBackgroundCmdBufferCacheDirty)
		{
			vkResetCommandBuffer(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex], 0);

			VK_CHECK_RESULT(vkBeginCommandBuffer(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex], &commandBufferBeginInfo));
			{
				vkCmdSetViewport(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex], 0, 1, &viewport);
				vkCmdSetScissor(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex], 0, 1, &scissor);

				vkCmdBindPipeline(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.starsphere);

				glm::mat4 mvp = matrices.projection*matrices.view;
				mvp[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);//Model to World Position
				mvp = glm::scale(mvp, glm::vec3(2.0f));

				vkCmdPushConstants(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), &mvp);

				models.starSphere.draw(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex]);
			}
			VK_CHECK_RESULT(vkEndCommandBuffer(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex]));
			
			tempBackgroundCmdUpdatedFrameIndex++;
			if (tempBackgroundCmdUpdatedFrameIndex>= swapChain.imageCount)
			{
				tempBackgroundCmdUpdatedFrameIndex = 0;
				starBackgroundCmdBufferCacheDirty = false;
			}
		}

		/*
			User interface

			With VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS, the primary command buffer's content has to be defined
			by secondary command buffers, which also applies to the UI overlay command buffer
		*/
		//if (userInterfaceCmdCacheDirty)
		{
			// Inheritance info for the secondary command buffers
			VkCommandBufferInheritanceInfo inheritanceInfo = vks::initializers::GenCommandBufferInheriatanceInfo();
			inheritanceInfo.renderPass = renderPass;
			// Secondary command buffer also use the currently active framebuffer
			inheritanceInfo.framebuffer = frameBuffers[currentCmdBufferIndex];

			// Secondary command buffer
			VkCommandBufferBeginInfo commandBufferBeginInfo = vks::initializers::GenCommandBufferBeginInfo();
			commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
			commandBufferBeginInfo.pInheritanceInfo = &inheritanceInfo;

			vkResetCommandBuffer(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex], 0);

			VK_CHECK_RESULT(vkBeginCommandBuffer(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex], &commandBufferBeginInfo));
			{
				vkCmdSetViewport(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex], 0, 1, &viewport);
				vkCmdSetScissor(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex], 0, 1, &scissor);

				vkCmdBindPipeline(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.starsphere);

				if (settings.overlay) {
					drawUI(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex]);
				}
			}
			VK_CHECK_RESULT(vkEndCommandBuffer(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex]));
			
			//tempUserInterfaceCmdUpdatedFrameIndex++;
			//if (tempUserInterfaceCmdUpdatedFrameIndex >= swapChain.imageCount)
			//{
			//	tempUserInterfaceCmdUpdatedFrameIndex = 0;
			//	userInterfaceCmdCacheDirty = false;
			//}
		}
	}

	void updateSecondaryCommandBuffersForStarBackground()
	{
		/*
		Background
		*/
		if (starBackgroundCmdBufferCacheDirty)
		{
			VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
			VkRect2D scissor = vks::initializers::GenRect2D(width, height, 0, 0);

			// Inheritance info for the secondary command buffers
			VkCommandBufferInheritanceInfo inheritanceInfo = vks::initializers::GenCommandBufferInheriatanceInfo();
			inheritanceInfo.renderPass = renderPass;
			// Secondary command buffer also use the currently active framebuffer
			inheritanceInfo.framebuffer = frameBuffers[currentCmdBufferIndex];

			// Secondary command buffer
			VkCommandBufferBeginInfo commandBufferBeginInfo = vks::initializers::GenCommandBufferBeginInfo();
			commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
			commandBufferBeginInfo.pInheritanceInfo = &inheritanceInfo;

			vkResetCommandBuffer(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex], 0);

			VK_CHECK_RESULT(vkBeginCommandBuffer(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex], &commandBufferBeginInfo));
			{
				vkCmdSetViewport(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex], 0, 1, &viewport);
				vkCmdSetScissor(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex], 0, 1, &scissor);

				vkCmdBindPipeline(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.starsphere);

				glm::mat4 mvp = matrices.projection*matrices.view;
				mvp[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);//Model to World Position
				mvp = glm::scale(mvp, glm::vec3(2.0f));

				vkCmdPushConstants(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex], pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), &mvp);

				models.starSphere.draw(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex]);
			}
			VK_CHECK_RESULT(vkEndCommandBuffer(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex]));

			tempBackgroundCmdUpdatedFrameIndex++;
			if (tempBackgroundCmdUpdatedFrameIndex >= swapChain.imageCount)
			{
				tempBackgroundCmdUpdatedFrameIndex = 0;
				starBackgroundCmdBufferCacheDirty = false;
			}
		}//if starBackgroundCmdBufferCacheDirty
	}

	void updateSecondaryCommandBuffersForUI()
	{
		if (userInterfaceCmdCacheDirty)
		{
			VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
			VkRect2D scissor = vks::initializers::GenRect2D(width, height, 0, 0);

			// Inheritance info for the secondary command buffers
			VkCommandBufferInheritanceInfo inheritanceInfo = vks::initializers::GenCommandBufferInheriatanceInfo();
			inheritanceInfo.renderPass = renderPass;
			// Secondary command buffer also use the currently active framebuffer
			inheritanceInfo.framebuffer = frameBuffers[currentCmdBufferIndex];

			// Secondary command buffer
			VkCommandBufferBeginInfo commandBufferBeginInfo = vks::initializers::GenCommandBufferBeginInfo();
			commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
			commandBufferBeginInfo.pInheritanceInfo = &inheritanceInfo;

			vkResetCommandBuffer(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex], 0);

			VK_CHECK_RESULT(vkBeginCommandBuffer(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex], &commandBufferBeginInfo));
			{
				vkCmdSetViewport(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex], 0, 1, &viewport);
				vkCmdSetScissor(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex], 0, 1, &scissor);

				vkCmdBindPipeline(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.starsphere);

				if (settings.overlay) {
					drawUI(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex]);
				}
			}
			VK_CHECK_RESULT(vkEndCommandBuffer(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex]));

			tempUserInterfaceCmdUpdatedFrameIndex++;
			if (tempUserInterfaceCmdUpdatedFrameIndex >= swapChain.imageCount)
			{
				tempUserInterfaceCmdUpdatedFrameIndex = 0;
				userInterfaceCmdCacheDirty = false;
			}
		}//if userInterfaceCmdCacheDirty
	}

	// Build the secondary command buffer for each thread
	void threadRenderCode(uint32_t threadIndex,uint32_t cmdBufferIndex,VkCommandBufferInheritanceInfo inheritanceInfo)
	{
		ThreadData * thread = &threadDatas[threadIndex];
		ObjectData * objectData = &thread->objectDatas[cmdBufferIndex];

		// Update
		if (!paused)
		{
			objectData->rotation.y += 2.5f*objectData->rotationSpeed*frameTimer;
			if (objectData->rotation.y > 360.0f)
			{
				objectData->rotation.y -= 360.0f;
			}

			objectData->deltaT += 0.15f*frameTimer;
			if (objectData->deltaT > 1.0f)
			{
				objectData->deltaT -= 1.0f;
			}
			objectData->pos.y = sin(glm::radians(objectData->deltaT*360.0f))*2.5f;
		}

		objectData->model = glm::translate(glm::mat4(1.0f), objectData->pos);
		objectData->model = glm::rotate(objectData->model, -sinf(glm::radians(objectData->deltaT * 360.0f)) * 0.25f, glm::vec3(objectData->rotationDir, 0.0f, 0.0f)); // X
		objectData->model = glm::rotate(objectData->model, glm::radians(objectData->rotation.y), glm::vec3(0.0f, objectData->rotationDir, 0.0f)); // Y
		objectData->model = glm::rotate(objectData->model, glm::radians(objectData->deltaT * 360.0f), glm::vec3(0.0f, objectData->rotationDir, 0.0f)); // Z
		objectData->model = glm::scale(objectData->model, glm::vec3(objectData->scale));

		thread->pushConstBlocks[cmdBufferIndex].MVP = matrices.projection * matrices.view * objectData->model;

		// Check visibility against view frustum using a simple sphere check based on the radius of the mesh
		objectData->visible = frustum.checkSphere(objectData->pos, models.ufo.dimensions.radius*0.5f);

		if (!objectData->visible)
		{
			return;
		}

		/*
			Record Command
		*/
		VkCommandBufferBeginInfo commandBufferBeginInfo = vks::initializers::GenCommandBufferBeginInfo();
		commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
		commandBufferBeginInfo.pInheritanceInfo = &inheritanceInfo;

		VkCommandBuffer cmdBuffer = thread->commandBuffers[cmdBufferIndex];

		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &commandBufferBeginInfo));

		VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
		VkRect2D scissor = vks::initializers::GenRect2D(width, height, 0, 0);
		vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.phong);

		// Update shader push constant block
		// Contains model view matrix
		vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ThreadPushConstantBlock), &thread->pushConstBlocks[cmdBufferIndex]);

		VkDeviceSize offsets[1] = { 0 };
		vkCmdBindVertexBuffers(cmdBuffer, 0, 1, &models.ufo.vertices.buffer, offsets);
		vkCmdBindIndexBuffer(cmdBuffer, models.ufo.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
		vkCmdDrawIndexed(cmdBuffer, models.ufo.indices.count, 1, 0, 0, 0);

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}

	// Updates the secondary command buffers using a thread pool
	// and puts them into the primary command buffer
	// that's last submitted to the queue for rendering
	void updatePrimaryCommandBuffers(VkFramebuffer frameBuffer)
	{
		// Inheritance info for the secondary command buffers
		VkCommandBufferInheritanceInfo inheritanceInfo = vks::initializers::GenCommandBufferInheriatanceInfo();
		inheritanceInfo.renderPass = renderPass;
		// Secondary command buffer also use the currently active framebuffer
		inheritanceInfo.framebuffer = frameBuffer;

		// Update secondary command buffers for scene background and UI
		updateSecondaryCommandBuffersForStarBackgroundAndUI();
		//updateSecondaryCommandBuffersForStarBackground();
		//updateSecondaryCommandBuffersForUI();

		/*
			Reccord Primary Command Buffer
		*/

		// Contains the list of secondary command buffers to be submitted
		std::vector<VkCommandBuffer> commandBuffers;

		VkCommandBufferBeginInfo cmdBufBeginInfo = vks::initializers::GenCommandBufferBeginInfo();

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
		renderPassBeginInfo.framebuffer = frameBuffer;

		// Set target frame buffer
		VK_CHECK_RESULT(vkBeginCommandBuffer(primaryCommandBuffer, &cmdBufBeginInfo));

		//The primary command buffer does not contain any rendering commands
		// These are stored(and retrieved) from the secondary command buffers
		vkCmdBeginRenderPass(primaryCommandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

		if (displayStarSphere)
		{
			commandBuffers.push_back(secondaryCommandBuffers.backgrounds[currentCmdBufferIndex]);
		}

		// Add a job to the thread's queue for each object to be rendered
		for (uint32_t t = 0; t < numThreads; t++)
		{
			for (uint32_t i = 0; i < numObjectsPerThread; i++)
			{
				threadPool.threads[t]->addJob([=] {threadRenderCode(t, i, inheritanceInfo); });
			}//for_i
		}//for_t
		threadPool.wait();

		// Only submit if object is within the current view frustum
		for (uint32_t t = 0; t < numThreads; t++)
		{
			for (uint32_t i = 0; i < numObjectsPerThread; i++)
			{
				if (threadDatas[t].objectDatas[i].visible)
				{
					commandBuffers.push_back(threadDatas[t].commandBuffers[i]);
				}
			}//for_i
		}//for_t

		// Render UI last
		if (uiOverlay.visible)
		{
			commandBuffers.push_back(secondaryCommandBuffers.userInterfaces[currentCmdBufferIndex]);
		}

		//使用继承信息分线程录制渲染命令，在主渲染流程中增加Secondary到主渲染命令Buffer中完成真正命令录制
		// Excute render commands from the secondary command buffer
		vkCmdExecuteCommands(primaryCommandBuffer, commandBuffers.size(), commandBuffers.data());

		vkCmdEndRenderPass(primaryCommandBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(primaryCommandBuffer));
	}

	void draw()
	{
		//Wait for fence to signal that all command buffers are ready
		VkResult fenceRes;
		do
		{
			fenceRes = vkWaitForFences(device, 1, &renderFence, VK_TRUE, 100000000);
		} while (fenceRes == VK_TIMEOUT);//无限时间等待，直至为真
		VK_CHECK_RESULT(fenceRes);

		vkResetFences(device, 1, &renderFence);//可进入渲染阶段

		VulkanExampleBase::prepareFrame();

		updatePrimaryCommandBuffers(frameBuffers[currentCmdBufferIndex]);

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &primaryCommandBuffer;

		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, renderFence));

		VulkanExampleBase::submitFrame();
	}

	virtual void render() override
	{
		if (!prepared)
		{
			return;
		}
		draw();
		if (camera.updated)
		{
			updateMatrices();
		}
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)override
	{
		if (overlay->header("Statistics")) {
			overlay->text("Active threads: %d", numThreads);
		}
		if (overlay->header("Settings")) {
			overlay->checkBox("Stars", &displayStarSphere);
		}
	}

	virtual void viewChanged() override
	{
		VulkanExampleBase::viewChanged();

		userInterfaceCmdCacheDirty = true;
		tempUserInterfaceCmdUpdatedFrameIndex = 0;
	}

	virtual void windowResized() override
	{
		VulkanExampleBase::windowResized();

		starBackgroundCmdBufferCacheDirty = true;
		tempBackgroundCmdUpdatedFrameIndex = 0;

		userInterfaceCmdCacheDirty = true;
		tempUserInterfaceCmdUpdatedFrameIndex = 0;
	}

private:

};

VULKAN_EXAMPLE_MAIN()