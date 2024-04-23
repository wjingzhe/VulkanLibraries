/*
* Vulkan Example - Compute shader culling and LOD using indirect rendering
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*
*/

#include "VulkanExampleBase.h"
#include "VulkanglTFModel.h"
#include "frustum.hpp"

#define VERTEX_BUFFER_BIND_ID 0
#define INSTANCE_BUFFER_BIND_ID 1
#define ENABLE_VALIDATION false

// Total number of objects in the scene
#if defined(__ANDROID__)
#define OBJECT_COUNT 32
#else
#define OBJECT_COUNT 64
#endif

#define MAX_LOD_LEVEL 5

class VulkanExample : public VulkanExampleBase
{
public:
	bool fixedFrustum = false;

	// The model contains multiple versions of a single object with different levels of detail
	vkglTF::Model lodModel;

	// Per-instance data block
	struct InstanceData
	{
		glm::vec3 pos;
		float scale;
	};


	// Contains the instance data
	vks::Buffer instanceBuffer;
	// Contains the indirect drawing commands
	vks::Buffer indirectCommandsBuffer;
	vks::Buffer indirectDrawCountConstBuffer;


	// Indirect draw statistics (update via compute)
	struct
	{
		uint32_t drawCount; // Total number of indirect draw counts to be issued
		uint32_t lodCount[MAX_LOD_LEVEL + 1]; // Statistics for number of draws per LOD level (written by compute shader)
	} indirectStats;

	// Store the indirect draw commands containing index offets and instance count per object
	std::vector<VkDrawIndexedIndirectCommand> drawIndirectCommands;



	// Resources for the compute part of the example
	struct
	{
		vks::Buffer lodLevelBuffers; // Contains index start and counts for the different lod levels
		VkQueue queue; // Separate queue for compute commands (queue family may differ from the one used for graphics)
		VkCommandPool commandPool; // Use a separate command pool(queue family may differ from the one used for graphics)
		VkCommandBuffer commandBuffer; // Command buffer storing the dispatch commands and barriers
		VkFence fence; // Synchronization fence to avoid rewriting compute CB if still in use
		VkSemaphore semaphore; // 用于返回compute shader已完成指令执行 Used as a wait semephore for graphics submission
		VkDescriptorSetLayout descriptorSetLayout;// Compute shader binding layout
		VkDescriptorSet descriptorSet; // Compute shader bindings
		VkPipelineLayout pipelineLayout; // Layout of the compute pipeline
		VkPipeline pipeline; // Compute pipeline for updating particle positions
	} compute;

	struct
	{
		vks::Buffer scene;
	} uniformData;

	struct
	{
		glm::mat4 projection;
		glm::mat4 modelView;
		glm::vec4 cameraPos;
		glm::vec4 frustumPlanes[6];
	} uboSceneTransformDatas;

	// View frustum for culling invisible objects
	vks::Frustum frustum;

	struct
	{
		VkPipeline plants;
	} pipelines;

	VkPipelineLayout pipelineLayout_IndirectDraw;
	VkDescriptorSet descriptorSet_IndirectDraw;
	VkDescriptorSetLayout descriptorSetLayout_IndirectDraw;

	uint32_t objectCount = 0;

	VulkanExample():VulkanExampleBase(ENABLE_VALIDATION)
	{
		windowTitle = "Vulkan Example - Compute cull and lod";
		
		camera.cameraType = Camera::CameraType::firstperson;
		camera.setPerspective(60.0f, (float)width / (float)height, 0.1f, 512.0f);
		camera.setTranslation(glm::vec3(0.5f, 0.0f, 0.0f));
		camera.movementSpeed = 5.0f;
		memset(&indirectStats, 0, sizeof(indirectStats));
	}
	
	~VulkanExample()
	{
		vkDestroyPipeline(device, pipelines.plants, nullptr);
		vkDestroyPipelineLayout(device, pipelineLayout_IndirectDraw, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout_IndirectDraw, nullptr);

		instanceBuffer.destroy();
		indirectCommandsBuffer.destroy();
		uniformData.scene.destroy();
		compute.lodLevelBuffers.destroy();

		vkDestroyPipelineLayout(device, compute.pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, compute.descriptorSetLayout, nullptr);
		vkDestroyPipeline(device, compute.pipeline, nullptr);
		vkDestroyFence(device, compute.fence, nullptr);
		vkDestroyCommandPool(device, compute.commandPool, nullptr);
		vkDestroySemaphore(device, compute.semaphore, nullptr);


	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		lodModel.loadFromFile(getAssetPath() + "models/suzanne_lods.gltf", vulkanDevice, queue, glTFLoadingFlags);
	}

	void updateUniformBuffer(bool viewChanged)
	{
		if (viewChanged)
		{
			uboSceneTransformDatas.projection = camera.matrices.perspective;
			uboSceneTransformDatas.modelView = camera.matrices.view;
			if (!fixedFrustum)
			{
				uboSceneTransformDatas.cameraPos = glm::vec4(camera.position, 1.0f)*-1.0f;
				frustum.update(uboSceneTransformDatas.projection*uboSceneTransformDatas.modelView);
				memcpy(uboSceneTransformDatas.frustumPlanes, frustum.planes.data(), sizeof(glm::vec4) * 6);
			}//if
		}//if

		memcpy(uniformData.scene.mappedData, &uboSceneTransformDatas, sizeof(uboSceneTransformDatas));
	}

	void prepareBuffersForIndirectDrawAndComputeLOD()
	{
		objectCount = OBJECT_COUNT * OBJECT_COUNT*OBJECT_COUNT;

		vks::Buffer tempStagingBuffer;

		std::vector<InstanceData> instanceDatas(objectCount);
		drawIndirectCommands.resize(objectCount);

		// Indirect draw commands
		for (uint32_t x = 0; x < OBJECT_COUNT; x++)
		{
			for (uint32_t y = 0; y < OBJECT_COUNT; y++)
			{
				for (uint32_t z = 0; z < OBJECT_COUNT; z++)
				{
					uint32_t index = x + y * OBJECT_COUNT + z * OBJECT_COUNT*OBJECT_COUNT;
					drawIndirectCommands[index].instanceCount = 1;
					drawIndirectCommands[index].firstInstance = index;
					// firstIndex and indexCount are written by the compute shader
				}
			}
		}

		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&tempStagingBuffer, drawIndirectCommands.size() * sizeof(VkDrawIndexedIndirectCommand), drawIndirectCommands.data()));

		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &indirectCommandsBuffer, tempStagingBuffer.size));

		vulkanDevice->CopyBuffer(&tempStagingBuffer, &indirectCommandsBuffer, queue);
		tempStagingBuffer.destroy();

		indirectStats.drawCount = static_cast<uint32_t>(drawIndirectCommands.size());
		// indirectDrawCountConstBuffer 重新组织constBuffer的填充
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &indirectDrawCountConstBuffer, sizeof(indirectStats)));
		// Map for host access
		VK_CHECK_RESULT(indirectDrawCountConstBuffer.map());

		// Instance data
		for (uint32_t x = 0; x < OBJECT_COUNT; x++)
		{
			for (uint32_t y = 0; y < OBJECT_COUNT; y++)
			{
				for (uint32_t z = 0; z < OBJECT_COUNT; z++)
				{
					uint32_t index = x + y * OBJECT_COUNT + z * OBJECT_COUNT*OBJECT_COUNT;
					instanceDatas[index].pos = glm::vec3((float)x, (float)y, (float)z) - glm::vec3((float)OBJECT_COUNT / 2.0f);
					instanceDatas[index].scale = 2.0f;
				}//for
			}//for
		}//for

		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &tempStagingBuffer, instanceDatas.size() * sizeof(InstanceData), instanceDatas.data()));
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &instanceBuffer, tempStagingBuffer.size));

		vulkanDevice->CopyBuffer(&tempStagingBuffer, &instanceBuffer, queue);
		tempStagingBuffer.destroy();


		// Shader storage buffer containing index offsets and counts for the LODs
		struct LOD
		{
			uint32_t firstIndex;
			uint32_t indexCount;
			float distance;
			float _pad0;
		};

		std::vector<LOD> LODLevels;
		uint32_t n = 0;
		for (auto node : lodModel.nodes)
		{
			LOD lod;
			lod.firstIndex = node->mesh->primitives[0]->firstIndex;// First index for this LOD
			lod.indexCount = node->mesh->primitives[0]->indexCount;// Index count for this LOD
			lod.distance = 5.0f + n * 5.0f; //Staging distance (to viewer) for this LOD
			n++;
			LODLevels.push_back(lod);
		}//for

		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &tempStagingBuffer, LODLevels.size() * sizeof(LOD), LODLevels.data()));
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &compute.lodLevelBuffers, tempStagingBuffer.size));
		vulkanDevice->CopyBuffer(&tempStagingBuffer, &compute.lodLevelBuffers, queue);
		tempStagingBuffer.destroy();

		// Scene uniform buffer
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformData.scene, sizeof(uboSceneTransformDatas)));
		VK_CHECK_RESULT(uniformData.scene.map());

		updateUniformBuffer(true);
	}

	void setupDescriptorSetLayoutAndPipelineLayout_IndirectDraw()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0: Vertex Shader uniform buffer
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,0),
		};
		VkDescriptorSetLayoutCreateInfo descritptorLayoutCreateInfo = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descritptorLayoutCreateInfo, nullptr, &descriptorSetLayout_IndirectDraw));

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayout_IndirectDraw, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout_IndirectDraw));
	}

	void preparePipelines_IndirectDraw()
	{
		// This example uses two different input stats, one for the instanced part and one for non-instanced rendering
		VkPipelineVertexInputStateCreateInfo vertexInputStateCI = vks::initializers::GenPipelineVertexInputStateCreateInfo();
		
		// 顶点属性描述
		std::vector<VkVertexInputBindingDescription> vertexInputDescriptions;
		// Vertex input bindings
		// The instancing pipeline uses a vertex input state with two bindings
		vertexInputDescriptions =
		{
			// Binding point 0: Mesh vertex layout description at per-vertex rate
			vks::initializers::GenVertexInputBindingDescription(VERTEX_BUFFER_BIND_ID,sizeof(vkglTF::Vertex),VK_VERTEX_INPUT_RATE_VERTEX),
			// Binding point 1: Instanced data at per-instance rate
			vks::initializers::GenVertexInputBindingDescription(INSTANCE_BUFFER_BIND_ID,sizeof(InstanceData),VK_VERTEX_INPUT_RATE_INSTANCE),
		};

		// Vertex attribute bindings
		std::vector<VkVertexInputAttributeDescription>attributeDescriptions = 
		{
			// Per-vertex attributes
			// These are advanced for each vertex fetched by the vertex shader
			vks::initializers::GenVertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID,0,VK_FORMAT_R32G32B32_SFLOAT,offsetof(vkglTF::Vertex,pos)),
			vks::initializers::GenVertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID,1,VK_FORMAT_R32G32B32_SFLOAT,offsetof(vkglTF::Vertex,normal)),
			vks::initializers::GenVertexInputAttributeDescription(VERTEX_BUFFER_BIND_ID,2,VK_FORMAT_R32G32B32_SFLOAT,offsetof(vkglTF::Vertex,color)),

			// Per-instance attributes
			// These are fetched for each instance rendered
			vks::initializers::GenVertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID,4,VK_FORMAT_R32G32B32_SFLOAT,offsetof(InstanceData,pos)), // Location 4: Postion
			vks::initializers::GenVertexInputAttributeDescription(INSTANCE_BUFFER_BIND_ID,5,VK_FORMAT_R32_SFLOAT,offsetof(InstanceData,scale)), // Location 5: Scale
		};

		vertexInputStateCI.pVertexBindingDescriptions = vertexInputDescriptions.data();
		vertexInputStateCI.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputDescriptions.size());
		vertexInputStateCI.pVertexAttributeDescriptions = attributeDescriptions.data();
		vertexInputStateCI.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());


		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::GenPipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo  rasterizationStateCI = vks::initializers::GenPipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::GenPipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::GenPipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::GenPipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::GenPipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::GenPipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::GenPipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCreateInfo = vks::initializers::GenPipelineCreateInfo(pipelineLayout_IndirectDraw, renderPass);
		pipelineCreateInfo.pVertexInputState = &vertexInputStateCI;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCreateInfo.pRasterizationState = &rasterizationStateCI;
		pipelineCreateInfo.pColorBlendState = &colorBlendStateCI;
		pipelineCreateInfo.pDepthStencilState = &depthStencilStateCI;
		pipelineCreateInfo.pViewportState = &viewportStateCI;
		pipelineCreateInfo.pMultisampleState = &multisampleStateCI;
		pipelineCreateInfo.pDynamicState = &dynamicStateCI;
		pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCreateInfo.pStages = shaderStages.data();

		// Indirect (and instanced) pipeline for the plants
		shaderStages[0] = loadShader(getShadersPath() + "computecullandlod/indirectdraw.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "computecullandlod/indirectdraw.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.plants));
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSizesForIndirectDrawAndCompute =
		{
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2),
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4),
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::GenDescriptorPoolCreateInfo(poolSizesForIndirectDrawAndCompute, 2);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetAndUpdate_IndirectDraw()
	{
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout_IndirectDraw, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet_IndirectDraw));

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0: Vertex shader uniform buffer
			vks::initializers::GenWriteDescriptorSet(descriptorSet_IndirectDraw,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&uniformData.scene.descriptorBufferInfo),
		};
		vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
	}

	void buildComputeCommandBuffer()
	{
		VkCommandBufferBeginInfo cmdBufBeginInfo = vks::initializers::GenCommandBufferBeginInfo();
		VK_CHECK_RESULT(vkBeginCommandBuffer(compute.commandBuffer, &cmdBufBeginInfo));

		// Add memory barrier to ensure that the indirect commands have been consumed before the compute shader updates them
		VkBufferMemoryBarrier bufferBarrier = vks::initializers::GenBufferMemoryBarrier();
		bufferBarrier.buffer = indirectCommandsBuffer.buffer;
		bufferBarrier.size = indirectCommandsBuffer.descriptorBufferInfo.range;
		bufferBarrier.srcAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		bufferBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		bufferBarrier.srcQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphicIndex;
		bufferBarrier.dstQueueFamilyIndex = vulkanDevice->queueFamilyIndices.computeIndex;

		vkCmdPipelineBarrier(compute.commandBuffer, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_FLAGS_NONE,
			0, nullptr, 1, &bufferBarrier, 0, nullptr);

		vkCmdBindPipeline(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipeline);
		vkCmdBindDescriptorSets(compute.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, compute.pipelineLayout, 0, 1, &compute.descriptorSet, 0, nullptr);

		// Dispatch the compute job
		// The compute shader will do the frustum culling and adjust the indirect draw calls depending on object visibility.
		// It also determines the lod to use depending on distance to the viewer
		vkCmdDispatch(compute.commandBuffer, objectCount / 16, 1, 1);

		{
			// Add memory barrier to ensure that the compute shader has finished writing the indirect command buffer before it's consumed
			VkBufferMemoryBarrier bufferBarrier = vks::initializers::GenBufferMemoryBarrier();
			bufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			bufferBarrier.dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
			bufferBarrier.buffer = indirectCommandsBuffer.buffer;
			bufferBarrier.size = indirectCommandsBuffer.descriptorBufferInfo.range;
			bufferBarrier.srcQueueFamilyIndex = vulkanDevice->queueFamilyIndices.computeIndex;
			bufferBarrier.dstQueueFamilyIndex = vulkanDevice->queueFamilyIndices.graphicIndex;

			vkCmdPipelineBarrier(compute.commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
				VK_FLAGS_NONE, 0, nullptr, 1, &bufferBarrier, 0, nullptr);
		}

		// todo: barrier for indirect stats buffer?
		vkEndCommandBuffer(compute.commandBuffer);
	}

	void prepareCompute()
	{
		// Get a compute capable device queue
		vkGetDeviceQueue(device, vulkanDevice->queueFamilyIndices.computeIndex, 0, &compute.queue);

		// Create compute pipeline
		// Compute pipeline are created separate from graphics pipelines even if they use the same queue(family index)

		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0: Instance input data buffer
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_SHADER_STAGE_COMPUTE_BIT,0,1),
			// Binding 1: Indirect draw command output buffer(input)
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_SHADER_STAGE_COMPUTE_BIT,1,1),
			// Binding 2: Uniform buffer with global matrices(input)
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_COMPUTE_BIT,2,1),
			// Binding 3: Indirect draw stats(output)
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_SHADER_STAGE_COMPUTE_BIT,3,1),
			// Binding 4: LOD info(input)
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,VK_SHADER_STAGE_COMPUTE_BIT,4,1),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayoutCreateInfo = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayoutCreateInfo, nullptr, &compute.descriptorSetLayout));

		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&compute.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &compute.pipelineLayout));

		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &compute.descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &compute.descriptorSet));
		std::vector<VkWriteDescriptorSet> computeWriteDescriptorSets =
		{
			// Binding 0: Instance input data buffer
			vks::initializers::GenWriteDescriptorSet(compute.descriptorSet,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,0,&instanceBuffer.descriptorBufferInfo),
			// Binding 1: Indirect draw command output buffer
			vks::initializers::GenWriteDescriptorSet(compute.descriptorSet,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,&indirectCommandsBuffer.descriptorBufferInfo),
			// Binding 2: Uniform buffer with global matrices
			vks::initializers::GenWriteDescriptorSet(compute.descriptorSet,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2,&uniformData.scene.descriptorBufferInfo),
			// Binding 3: Atomic counter (written in shader)
			vks::initializers::GenWriteDescriptorSet(compute.descriptorSet,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3,&indirectDrawCountConstBuffer.descriptorBufferInfo),
			// Binding 4: LOD info
			vks::initializers::GenWriteDescriptorSet(compute.descriptorSet,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4,&compute.lodLevelBuffers.descriptorBufferInfo),
		};

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(computeWriteDescriptorSets.size()), computeWriteDescriptorSets.data(), 0, NULL);

		// Create pipline
		VkComputePipelineCreateInfo computePipelineCreateInfo = vks::initializers::GenComputePipelineCreateInfo(compute.pipelineLayout, 0);
		computePipelineCreateInfo.stage = loadShader(getShadersPath() + "computecullandlod/cull.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

		// Use specialization constants to pass max. level of detail(determined by no. of meshes)
		VkSpecializationMapEntry specializationEntry{};
		specializationEntry.constantID = 0;
		specializationEntry.offset = 0;
		specializationEntry.size = sizeof(uint32_t);

		uint32_t specializationData = static_cast<uint32_t>(lodModel.nodes.size()) - 1;
		VkSpecializationInfo specializationInfo;
		specializationInfo.mapEntryCount = 1;
		specializationInfo.pMapEntries = &specializationEntry;
		specializationInfo.dataSize = sizeof(specializationData);
		specializationInfo.pData = &specializationData;

		computePipelineCreateInfo.stage.pSpecializationInfo = &specializationInfo;
		VK_CHECK_RESULT(vkCreateComputePipelines(device, pipelineCache, 1, &computePipelineCreateInfo, nullptr, &compute.pipeline));

		// Separate command pool as queue family for compute may be different than graphics
		VkCommandPoolCreateInfo cmdPoolInfo = {};
		cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		cmdPoolInfo.queueFamilyIndex = vulkanDevice->queueFamilyIndices.computeIndex;
		cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &compute.commandPool));

		// Create a command buffer for compute operation
		VkCommandBufferAllocateInfo cmdBufferAllocateInfo = vks::initializers::GenCommandBufferAllocateInfo(compute.commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1);
		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufferAllocateInfo, &compute.commandBuffer));

		// Fence for compute CB sync
		VkFenceCreateInfo fenceCreateInfo = vks::initializers::GenFenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
		VK_CHECK_RESULT(vkCreateFence(device, &fenceCreateInfo, nullptr, &compute.fence));

		VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::GenSemaphoreCreateInfo();
		VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &compute.semaphore));

		// Build a single command buffer containing the compute dispatch commands
		buildComputeCommandBuffer();
	}

	virtual void getEnabledFeatures() override
	{
		// Enable multi draw indirect if supported
		if (deviceFeatures.multiDrawIndirect) 
		{
			curEnabledDeviceFeatures.multiDrawIndirect = VK_TRUE;
		}
	}

	void buildCommandBuffersForPreRenderPrmitives()
	{
		VkCommandBufferBeginInfo cmdBufBeginInfo = vks::initializers::GenCommandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = { {0.18f,0.27f,0.5f,0.0f} };
		clearValues[1].depthStencil = { 1.0f,0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::GenRenderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		for (int32_t i = 0; i < drawCmdBuffers.size(); i++)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufBeginInfo));

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
			
			VkRect2D scissor = vks::initializers::GenRect2D((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_IndirectDraw, 0, 1, &descriptorSet_IndirectDraw, 0, NULL);

			VkDeviceSize offsets[1] = { 0 };
			// Mesh containing the LODs
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.plants);
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &lodModel.vertices.buffer, offsets);
			vkCmdBindVertexBuffers(drawCmdBuffers[i], INSTANCE_BUFFER_BIND_ID, 1, &instanceBuffer.buffer, offsets);

			vkCmdBindIndexBuffer(drawCmdBuffers[i], lodModel.indices.buffer, 0, VK_INDEX_TYPE_UINT32);

			if (vulkanDevice->features.multiDrawIndirect)
			{
				vkCmdDrawIndexedIndirect(drawCmdBuffers[i], indirectCommandsBuffer.buffer, 0, drawIndirectCommands.size(), sizeof(VkDrawIndexedIndirectCommand));
			}
			else
			{
				// if multi draw is not avaiable,we must issue separate draw commands
				for (auto j = 0; j < drawIndirectCommands.size(); j++)
				{
					vkCmdDrawIndexedIndirect(drawCmdBuffers[i], indirectCommandsBuffer.buffer, j * sizeof(VkDrawIndexedIndirectCommand), 1, sizeof(VkDrawIndexedIndirectCommand));
				}
			}//if_else

			drawUI(drawCmdBuffers[i]);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));
		}//for
	}

	void prepareForRendering()override
	{
		VulkanExampleBase::prepareForRendering();
		loadAssets();
		prepareBuffersForIndirectDrawAndComputeLOD();
		setupDescriptorSetLayoutAndPipelineLayout_IndirectDraw();
		preparePipelines_IndirectDraw();
		setupDescriptorPool();
		setupDescriptorSetAndUpdate_IndirectDraw();
		prepareCompute();
		buildCommandBuffersForPreRenderPrmitives();
		prepared = true;
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();

		// Submit compute shader for frustum culling

		// Wait for fence to ensure that compute buffer writes have finished
		vkWaitForFences(device, 1, &compute.fence, VK_TRUE, UINT64_MAX);
		vkResetFences(device, 1, &compute.fence);

		VkSubmitInfo computeSubmitInfo = vks::initializers::GenSubmitInfo();
		computeSubmitInfo.commandBufferCount = 1;
		computeSubmitInfo.pCommandBuffers = &compute.commandBuffer;
		computeSubmitInfo.signalSemaphoreCount = 1;
		computeSubmitInfo.pSignalSemaphores = &compute.semaphore;
		VK_CHECK_RESULT(vkQueueSubmit(compute.queue, 1, &computeSubmitInfo, VK_NULL_HANDLE));


		// Submit graphics command buffer
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentCmdBufferIndex];
		//submitInfo.pCommandBuffers = VK_NULL_HANDLE;

		// Wait on present and compute semaphores
		std::array<VkPipelineStageFlags, 2> stageFlags =
		{
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		};
		std::array<VkSemaphore, 2> waitSemaphores =
		{
			semaphores.presentComplete,// Wait for presentation to finished
			compute.semaphore, //Wait for compute to finish
		};

		submitInfo.pWaitSemaphores = waitSemaphores.data();
		submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
		submitInfo.pWaitDstStageMask = stageFlags.data();
		// Submit to queue
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, compute.fence));

		VulkanExampleBase::submitFrame();

		// Get draw count from compute
		memcpy(&indirectStats, indirectDrawCountConstBuffer.mappedData, sizeof(indirectStats));
	}

	virtual void render()override
	{
		if (!prepared)
		{
			return;
		}
		draw();
		if (camera.updated)
		{
			updateUniformBuffer(true);
		}
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay) override
	{
		if (overlay->header("Settings")) {
			if (overlay->checkBox("Freeze frustum", &fixedFrustum)) {
				updateUniformBuffer(true);
			}
		}
		if (overlay->header("Statistics")) {
			overlay->text("Visible objects: %d", indirectStats.drawCount);
			for (uint32_t i = 0; i < MAX_LOD_LEVEL + 1; i++) {
				overlay->text("LOD %d: %d", i, indirectStats.lodCount[i]);
			}
		}
	}

private:

};

VULKAN_EXAMPLE_MAIN()