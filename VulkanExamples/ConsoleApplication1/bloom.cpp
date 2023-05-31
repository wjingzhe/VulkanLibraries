// ConsoleApplication1.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>

#include "VulkanExampleBase.h"
#include "vulkanglTFModel.h"

#define ENABLE_VALIDATION true

// Off screen frame buffer properties
#define FB_DIM 256
#define FB_COLOR_FORMAT VK_FORMAT_R8G8B8A8_UNORM

class VulkanExample : public VulkanExampleBase
{
public:
	bool bloom = true;

	vks::TextureCubeMap cubeMap;

	struct
	{
		vkglTF::Model ufo;
		vkglTF::Model ufoGlow;
		vkglTF::Model skyBox;
	}models;

	struct
	{
		vks::Buffer scene;
		vks::Buffer skyBox;
		vks::Buffer blurParams;
	}uniformBuffers;

	struct UBO
	{
		glm::mat4 projectction;
		glm::mat4 view;
		glm::mat4 model;
	};

	struct UBOBluraParams 
	{
		float blurScale = 1.0f;
		float blurStrength = 1.5f;
	};

	struct 
	{
		UBO scene, skyBox;
		UBOBluraParams blurParams;
	}ubos;

	struct
	{
		VkPipeline blurVert;
		VkPipeline blurHorz;
		VkPipeline glowPass;
		VkPipeline phongPass;
		VkPipeline skyBox;
	}pipelines;

	struct
	{
		VkPipelineLayout blur;
		VkPipelineLayout scene;
	}pipelineLayouts;

	struct
	{
		VkDescriptorSet blurVert;
		VkDescriptorSet blurHorz;
		VkDescriptorSet scene;
		VkDescriptorSet skyBox;
	}descriptorSets;

	struct 
	{
		VkDescriptorSetLayout blur;
		VkDescriptorSetLayout scene;
	}descriptorSetLayouts;

	// Frame buffer for off-screen rendering
	struct FrameBufferAttachment
	{
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
	};
	struct FrameBuffer
	{
		VkFramebuffer frameBuffer;
		FrameBufferAttachment color, depth;
		VkDescriptorImageInfo descriptor;
	};

	struct OffscreenPass
	{
		int32_t width, height;
		VkRenderPass renderPass;
		VkSampler sampler;
		std::array<FrameBuffer, 2> frameBuffers;
	}offscreenPass;

	VulkanExample() :VulkanExampleBase(ENABLE_VALIDATION)
	{
		windowTitle = "Bloom (offscreen rendering)";
		timerSpeed *= 0.5f;
		camera.cameraType = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -10.25f));
		camera.setRotation(glm::vec3(7.5f, -343.0f, 0.0f));
		camera.setPerspective(45.0f, (float)width / (float)height, 0.1f, 256.0f);
	}

	~VulkanExample()
	{
		//Clean up used Vulkan resources
		// Note:Inherited destructor cleans up resources stored in base class

		vkDestroySampler(device, offscreenPass.sampler, nullptr);

		//Frame Buffer
		for (auto& frameBuffer:offscreenPass.frameBuffers)
		{
			// Attachments
			vkDestroyImageView(device, frameBuffer.color.view, nullptr);
			vkDestroyImage(device, frameBuffer.color.image, nullptr);
			vkFreeMemory(device, frameBuffer.color.mem, nullptr);

			vkDestroyImageView(device, frameBuffer.depth.view, nullptr);
			vkDestroyImage(device, frameBuffer.depth.image, nullptr);
			vkFreeMemory(device, frameBuffer.depth.mem, nullptr);

			vkDestroyFramebuffer(device, frameBuffer.frameBuffer, nullptr);
		}
		vkDestroyRenderPass(device, offscreenPass.renderPass, nullptr);

		vkDestroyPipeline(device, pipelines.blurHorz, nullptr);
		vkDestroyPipeline(device, pipelines.blurVert, nullptr);
		vkDestroyPipeline(device, pipelines.phongPass, nullptr);
		vkDestroyPipeline(device, pipelines.glowPass, nullptr);
		vkDestroyPipeline(device, pipelines.skyBox, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayouts.blur, nullptr); 
		vkDestroyPipelineLayout(device, pipelineLayouts.scene, nullptr);

		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.blur, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);

		//Uniform buffers
		uniformBuffers.scene.destroy();
		uniformBuffers.skyBox.destroy();
		uniformBuffers.blurParams.destroy();

		cubeMap.destroy();
	}

	// Setup the offscreen 
	void prepareOffscreenFramebuffer(FrameBuffer* frameBuf, VkFormat colorFormat, VkFormat depthFormat)
	{
		// Color attachment
		VkImageCreateInfo imageCreateInfo = vks::initializers::GenImageCreateInfo();
		imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
		imageCreateInfo.format = colorFormat;
		imageCreateInfo.extent.width = FB_DIM;
		imageCreateInfo.extent.height = FB_DIM;
		imageCreateInfo.extent.depth = 1;
		imageCreateInfo.mipLevels = 1;
		imageCreateInfo.arrayLayers = 1;
		imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		// We will sample directly from the door attachment
		imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		VkMemoryAllocateInfo memAlloc = vks::initializers::GenMemoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VkImageViewCreateInfo colorImageView = vks::initializers::GenImageViewCreateInfo();
		colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorImageView.format = colorFormat;
		colorImageView.flags = 0;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;

		VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &frameBuf->color.image));
		vkGetImageMemoryRequirements(device, frameBuf->color.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &frameBuf->color.mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, frameBuf->color.image, frameBuf->color.mem, 0));

		colorImageView.image = frameBuf->color.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &colorImageView, nullptr, &frameBuf->color.view));

		// Depth stencil attachment
		imageCreateInfo.format = depthFormat;
		imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		VkImageViewCreateInfo depthStencilView = vks::initializers::GenImageViewCreateInfo();
		depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthStencilView.format = depthFormat;
		depthStencilView.flags = 0;
		depthStencilView.subresourceRange = {};
		depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (vks::tools::formatHasStencil(depthFormat))
		{
			depthStencilView.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		depthStencilView.subresourceRange.baseMipLevel = 0;
		depthStencilView.subresourceRange.levelCount = 1;
		depthStencilView.subresourceRange.baseArrayLayer = 0;
		depthStencilView.subresourceRange.layerCount = 1;

		VK_CHECK_RESULT(vkCreateImage(device, &imageCreateInfo, nullptr, &frameBuf->depth.image));
		vkGetImageMemoryRequirements(device, frameBuf->depth.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &frameBuf->depth.mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, frameBuf->depth.image, frameBuf->depth.mem, 0));

		depthStencilView.image = frameBuf->depth.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &frameBuf->depth.view));

		VkImageView attachments[2];
		attachments[0] = frameBuf->color.view;
		attachments[1] = frameBuf->depth.view;

		VkFramebufferCreateInfo fBufCreateInfo = vks::initializers::GenFrameBufferCreateInfo();
		fBufCreateInfo.renderPass = offscreenPass.renderPass;
		fBufCreateInfo.attachmentCount = 2;
		fBufCreateInfo.pAttachments = attachments;
		fBufCreateInfo.width = FB_DIM;
		fBufCreateInfo.height = FB_DIM;
		fBufCreateInfo.layers = 1;

		VK_CHECK_RESULT(vkCreateFramebuffer(device, &fBufCreateInfo, nullptr, &frameBuf->frameBuffer));

		//Fill a descriptor for later use in a descriptor set
		frameBuf->descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		frameBuf->descriptor.imageView = frameBuf->color.view;
		frameBuf->descriptor.sampler = offscreenPass.sampler;
	}

	// Prepare the off-screen frame buffers used for the vertical and horizontal blur
	void prepareOffscreen()
	{
		offscreenPass.width = FB_DIM;
		offscreenPass.height = FB_DIM;

		// Find a suitable depth format
		VkFormat fbDepthFormat;
		VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &fbDepthFormat);
		assert(validDepthFormat);

		// Create a separate render pass for the off-screen rendering as it may differ from the one used for scene rendering

		std::array<VkAttachmentDescription, 2> attachmentDescriptions = {};
		//Color attachment
		attachmentDescriptions[0].format = FB_COLOR_FORMAT;
		attachmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		// Depth attachment
		attachmentDescriptions[1].format = fbDepthFormat;
		attachmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attachmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attachmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attachmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attachmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attachmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorReference = { 0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthReference = { 1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;
		subpassDescription.pDepthStencilAttachment = &depthReference;

		//Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 2> dependencies;
		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[1].srcSubpass = 0;
		dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// Create the actual render pass
		VkRenderPassCreateInfo renderPassCreateInfo = {};
		renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassCreateInfo.attachmentCount = static_cast<uint32_t>(attachmentDescriptions.size());
		renderPassCreateInfo.pAttachments = attachmentDescriptions.data();
		renderPassCreateInfo.subpassCount = 1;
		renderPassCreateInfo.pSubpasses = &subpassDescription;
		renderPassCreateInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassCreateInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &offscreenPass.renderPass));

		// Create sampler to sampler from the color attachments
		VkSamplerCreateInfo sampler = vks::initializers::GenSamplerCreateInfo();
		sampler.magFilter = VK_FILTER_LINEAR;
		sampler.minFilter = VK_FILTER_LINEAR;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 1.0f;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &offscreenPass.sampler));

		// Create two frame buffers
		prepareOffscreenFramebuffer(&offscreenPass.frameBuffers[0], FB_COLOR_FORMAT, fbDepthFormat);
		prepareOffscreenFramebuffer(&offscreenPass.frameBuffers[1], FB_COLOR_FORMAT, fbDepthFormat);
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::GenCommandBufferBeginInfo();

		VkClearValue clearValues[2];
		VkViewport viewport;
		VkRect2D scissor;

		/*
		The blur method used in this example is multi pass and renders the vertical blur first and then the horizontal one
		while it's possible to blur in one pass,this method is widely used as it requires far less examples to generate the blur
		*/
		for (int32_t i = 0;i<drawCmdBuffers.size();++i)
		{
			VK_CHECK_RESULT(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo));

			if (bloom)
			{
				clearValues[0].color = { {0.0f,0.0f,0.0f,1.0f} };
				clearValues[1].depthStencil = { 1.0f,0 };

				VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::GenRenderPassBeginInfo();
				renderPassBeginInfo.renderPass = offscreenPass.renderPass;
				renderPassBeginInfo.framebuffer = offscreenPass.frameBuffers[0].frameBuffer;
				renderPassBeginInfo.renderArea.extent.width = offscreenPass.width;
				renderPassBeginInfo.renderArea.extent.height = offscreenPass.height;
				renderPassBeginInfo.clearValueCount = 2;
				renderPassBeginInfo.pClearValues = clearValues;

				viewport = vks::initializers::GenViewport(offscreenPass.width, offscreenPass.height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				scissor = vks::initializers::GenRect2D(offscreenPass.width, offscreenPass.height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				/*
				First render pass: Render glow parts of the model(separate mesh) to an off-screen frame buffer
				*/

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.scene, 0, 1, &descriptorSets.scene, 0, NULL);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.glowPass);

				models.ufoGlow.draw(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);

				/*
				Second render pass:Vertical blur
				Render contents of the first pass into a second framebuffer and apply a vertical blur
				This is the first blur pass,the horizontal blur is applied when rendering on top of the scene
				*/

				renderPassBeginInfo.framebuffer = offscreenPass.frameBuffers[1].frameBuffer;

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.blur, 0, 1, &descriptorSets.blurVert, 0, NULL);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.blurVert);
				vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}//if bloom

			/*
			Note: Explicit synchronization is not required between the render pass,as this is done implicit via sub pass dependencies
			*/

			/*
			Third render pass : Scene rendering with applied vertical blur
			Render the scene and the(vertically blurred) contents of the second framebuffer and apply a horizontal blur
			*/
			{
				clearValues[0].color = defaultClearColor;
				clearValues[1].depthStencil = { 1.0f,0 };

				VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::GenRenderPassBeginInfo();
				renderPassBeginInfo.renderPass = renderPass;
				renderPassBeginInfo.framebuffer = frameBuffers[i];
				renderPassBeginInfo.renderArea.extent.width = width;
				renderPassBeginInfo.renderArea.extent.height = height;
				renderPassBeginInfo.clearValueCount = 2;
				renderPassBeginInfo.pClearValues = clearValues;

				vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
				
				VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

				VkRect2D scissor = vks::initializers::GenRect2D(width, height, 0, 0);
				vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

				VkDeviceSize offsets[1] = { 0 };

				// SkyBox
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.scene, 0, 1, &descriptorSets.skyBox, 0, NULL);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skyBox);
				models.skyBox.draw(drawCmdBuffers[i]);

				// 3D scene
				vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.scene, 0, 1, &descriptorSets.scene, 0, NULL);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.phongPass);
				models.ufo.draw(drawCmdBuffers[i]);

				if (bloom)
				{
					vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.blur, 0, 1, &descriptorSets.blurHorz, 0, NULL);
					vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.blurHorz);
					vkCmdDraw(drawCmdBuffers[i], 3, 1, 0, 0);
				}

				drawUI(drawCmdBuffers[i]);

				vkCmdEndRenderPass(drawCmdBuffers[i]);
			}

			VK_CHECK_RESULT(vkEndCommandBuffer(drawCmdBuffers[i]));

		}// for drawCmdBuffers[i]
	}//buildCommandBuffers

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.ufo.loadFromFile(getAssetPath() + "models/retroufo.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.ufoGlow.loadFromFile(getAssetPath() + "models/retroufo_glow.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.skyBox.loadFromFile(getAssetPath() + "models/cube.gltf", vulkanDevice, queue, glTFLoadingFlags);
		cubeMap.loadFromFile(getAssetPath() + "textures/cubemap_space.ktx", VK_FORMAT_R8G8B8A8_SNORM, vulkanDevice, queue);
	}

	void setupDescriptorPool()
	{
		std::vector<VkDescriptorPoolSize> poolSize = {
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,8),
			vks::initializers::GenDescriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,6)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::GenDescriptorPoolCreateInfo(poolSize, 5);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));
	}

	void setupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding>setLayoutBindings;
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo;
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo;

		// Fullscreen blur
		setLayoutBindings = {
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_FRAGMENT_BIT,0), //Binding 0: Fragment shader uniform buffer
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,1) // Binding 1:Fragment shader image sampler
		};

		descriptorSetLayoutCreateInfo = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayouts.blur));

		pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayouts.blur, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.blur));


		// Scene rendering
		setLayoutBindings = {
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_VERTEX_BIT,0), //Binding 0:Vertex Shader uniform buffer
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,VK_SHADER_STAGE_FRAGMENT_BIT,1), //Binding 1: Fragment shader image sampler
			vks::initializers::GenDescriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,VK_SHADER_STAGE_FRAGMENT_BIT,2), // Binding 2: Fragment shader image sampler
		};

		descriptorSetLayoutCreateInfo = vks::initializers::GenDescriptorSetLayoutCreateInfo(setLayoutBindings.data(), setLayoutBindings.size());
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayouts.scene));
		
		pipelineLayoutCreateInfo = vks::initializers::GenPipelineLayoutCreateInfo(&descriptorSetLayouts.scene, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.scene));
	}

	void setupDescriptorSet()
	{
		VkDescriptorSetAllocateInfo descriptorSetAllocInfo;
		std::vector<VkWriteDescriptorSet> writeDescriptorSets;

		// Full screen blur
		// Vertical
		descriptorSetAllocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.blur, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets.blurVert));

		writeDescriptorSets = {
			vks::initializers::GenWriteDescriptorSet(descriptorSets.blurVert,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&uniformBuffers.blurParams.descriptor),  // Binding 0: Fragment shader uniform buffer
			vks::initializers::GenWriteDescriptorSet(descriptorSets.blurVert,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,&offscreenPass.frameBuffers[0].descriptor), //Binding 1: Fragment shader texture sampler
		};

		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
		// Horizontal
		descriptorSetAllocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.blur, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets.blurHorz));
		writeDescriptorSets = {
			vks::initializers::GenWriteDescriptorSet(descriptorSets.blurHorz,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&uniformBuffers.blurParams.descriptor), //Binding 0: Fragment shader uniform buffer
			vks::initializers::GenWriteDescriptorSet(descriptorSets.blurHorz,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,&offscreenPass.frameBuffers[1].descriptor), // Binding 1: Fragment shader texture sampler
		};
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		//Scene rendering
		descriptorSetAllocInfo = vks::initializers::GenDescriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.scene, 1);
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets.scene));
		writeDescriptorSets = {
			vks::initializers::GenWriteDescriptorSet(descriptorSets.scene,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&uniformBuffers.scene.descriptor) //Binding 0: Vertex shader uniform buffer
		};
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);

		// Sky box
		VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets.skyBox));
		writeDescriptorSets = {
			vks::initializers::GenWriteDescriptorSet(descriptorSets.skyBox,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,0,&uniformBuffers.skyBox.descriptor), // Binding 0: Vertex shader uniform buffer
			vks::initializers::GenWriteDescriptorSet(descriptorSets.skyBox,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,&cubeMap.descriptor),  //Binding 1: Fragment shader texture sampler
		};
		vkUpdateDescriptorSets(device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}//setupDescriptorSet

	void preparePipelines()
	{
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::GenPipelineColorBlendAttachmentState(0xf, VK_FALSE);

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::GenPipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::GenPipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendStateCreateInfo  colorBlendStateCI = vks::initializers::GenPipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::GenPipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::GenPipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multiSampleStateCI = vks::initializers::GenPipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::GenPipelineDynamicStateCreateInfo(dynamicStateEnables.data(), dynamicStateEnables.size(), 0);

		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::GenPipelineCreateInfo(pipelineLayouts.blur, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multiSampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = shaderStages.size();
		pipelineCI.pStages = shaderStages.data();

		// Blur pipelines
		shaderStages[0] = loadShader(getShaderPath() + "bloom/gaussblur.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "bloom/gaussblur.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Empty vertex input state
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::GenPipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;
		pipelineCI.layout = pipelineLayouts.blur;

		// Additive blending
		blendAttachmentState.colorWriteMask = 0xF;
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;

		// Use specialization constants to select between horizontal and vertical blur
		uint32_t blurDirection = 0;
		VkSpecializationMapEntry  specializationMapEntry = vks::initializers::GenSpecializationMapEntry(0, 0, sizeof(uint32_t));
		VkSpecializationInfo specializationInfo = vks::initializers::GenSpecializationInfo(1, &specializationMapEntry, sizeof(uint32_t), &blurDirection);
		shaderStages[1].pSpecializationInfo = &specializationInfo;

		// Vertical blur pipeline
		pipelineCI.renderPass = offscreenPass.renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.blurVert));

		//vk函数全是地址型入参，赋值后可再调用创建函数即可
		// Horizontal blur pipeline
		blurDirection = 1;
		pipelineCI.renderPass = renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.blurHorz));

		// phone pass (3D model)
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState(
			{vkglTF::VertexComponent::Position, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal}
		);
		pipelineCI.layout = pipelineLayouts.scene;
		shaderStages[0] = loadShader(getShaderPath() + "bloom/phongpass.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "bloom/phongpass.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		blendAttachmentState.blendEnable = VK_FALSE;
		depthStencilStateCI.depthWriteEnable = VK_TRUE;
		rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
		pipelineCI.renderPass = renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.phongPass));

		// Color only pass (off-screen blur base)
		shaderStages[0] = loadShader(getShaderPath() + "bloom/colorpass.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "bloom/colorpass.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		pipelineCI.renderPass = offscreenPass.renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.glowPass));

		// Sky box (cube map)
		shaderStages[0] = loadShader(getShaderPath() + "bloom/skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShaderPath() + "bloom/skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		depthStencilStateCI.depthWriteEnable = VK_FALSE;
		rasterizationStateCI.cullMode = VK_CULL_MODE_FRONT_BIT;
		pipelineCI.renderPass = renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skyBox));
	}

	// prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Phong and color pass vertex shader uniform buffer
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.scene, sizeof(ubos.scene)));

		// Blur parameters uniform buffers
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.blurParams, sizeof(ubos.blurParams)));

		//Skybox
		VK_CHECK_RESULT(vulkanDevice->CreateBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &uniformBuffers.skyBox, sizeof(ubos.skyBox)));

		// Map persistent
		VK_CHECK_RESULT(uniformBuffers.scene.map());
		VK_CHECK_RESULT(uniformBuffers.blurParams.map());
		VK_CHECK_RESULT(uniformBuffers.skyBox.map());

		// Initialize uniform buffers
		updateUniformBuffersScene();
		updateUniformBufferBlur();
	}

	//Update uniform buffers for rendering the 3D scene
	void updateUniformBuffersScene()
	{
		// UFO
		ubos.scene.projectction = camera.matrices.perspective;
		ubos.scene.view = camera.matrices.view;

		auto tempVector3 = glm::vec3(sin(glm::radians(timer*360.0f))*0.25f, -1.0f, cos(glm::radians(timer*360.0f)*0.25f));

		ubos.scene.model = glm::translate(glm::mat4(1.0f), tempVector3);
		ubos.scene.model = glm::rotate(ubos.scene.model, -sinf(glm::radians(timer*360.0f))*0.15f, glm::vec3(1.0f, 0.0f, 0.0f));
		ubos.scene.model = glm::rotate(ubos.scene.model, glm::radians(timer*360.0f), glm::vec3(0.0f, 1.0f, 0.0f));

		memcpy(uniformBuffers.scene.mappedData, &ubos.scene, sizeof(ubos.scene));

		// Sky box
		ubos.skyBox.projectction = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 256.0f);
		ubos.skyBox.view = glm::mat4(glm::mat3(camera.matrices.view));
		ubos.skyBox.model = glm::mat4(1.0f);

		memcpy(uniformBuffers.skyBox.mappedData, &ubos.skyBox, sizeof(ubos.skyBox));
	}

	void updateUniformBufferBlur()
	{
		memcpy(uniformBuffers.blurParams.mappedData, &ubos.blurParams, sizeof(ubos.blurParams));
	}

	void draw()
	{
		VulkanExampleBase::prepareFrame();
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];
		VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
		VulkanExampleBase::submitFrame();
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		prepareUniformBuffers();
		prepareOffscreen();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		buildCommandBuffers();
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
		{
			return;
		}

		draw();

		if (!paused || camera.updated)
		{
			updateUniformBuffersScene();
		}
	}//

	virtual void OnUpdateUIOverlay(vks::UIOverlay * overlay) override
	{
		if (overlay->header("Settings"))
		{
			if (overlay->checkBox("Bloom",&bloom))
			{
				buildCommandBuffers();
			}

			if (overlay->inputFloat("Scale",&ubos.blurParams.blurScale,0.1f,2))
			{
				updateUniformBufferBlur();
			}
		}//Settings
	}

protected:
private:
};


VULKAN_EXAMPLE_MAIN();

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
