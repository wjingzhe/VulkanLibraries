#include "VulkanExampleBase.h"

std::vector<const char*> VulkanExampleBase::args;

CommandLineParser::CommandLineParser()
{
	add("help", { "--help" }, 0, "Show help");
	add("validation", { "-v","--validation" }, 0, "Enable validation layers");
	add("vsync", { "-vs","--vsync" }, 0, "Enable V-Sync");
	add("fullscreen", { "-f", "--fullscreen" }, 0, "Start in fullscreen mode");
	add("width", { "-w", "--width" }, 1, "Set window width");
	add("height", { "-h", "--height" }, 1, "Set window height");
	add("shaders", { "-s", "--shaders" }, 1, "Select shader type to use (glsl or hlsl)");
	add("gpuselection", { "-g", "--gpu" }, 1, "Select GPU to run on");
	add("gpulist", { "-gl", "--listgpus" }, 0, "Display a list of available Vulkan devices");
	add("benchmark", { "-b", "--benchmark" }, 0, "Run example in benchmark mode");
	add("benchmarkwarmup", { "-bw", "--benchwarmup" }, 1, "Set warmup time for benchmark mode in seconds");
	add("benchmarkruntime", { "-br", "--benchruntime" }, 1, "Set duration time for benchmark mode in seconds");
	add("benchmarkresultfile", { "-bf", "--benchfilename" }, 1, "Set file name for benchmark results");
	add("benchmarkresultframes", { "-bt", "--benchframetimes" }, 0, "Save frame times to benchmark results file");
	add("benchmarkframes", { "-bfs", "--benchmarkframes" }, 1, "Only render the given number of frames");
}

void CommandLineParser::add(std::string name, std::vector<std::string> commands, bool hasValue, std::string help)
{
	options[name].commands = commands;
	options[name].help = help;
	options[name].set = false;
	options[name].hasValue = hasValue;
	options[name].value = "";
}

void CommandLineParser::printHelp()
{
	std::cout << "Available command line options:\n";
	for (auto option:options)
	{
		std::cout << " ";
		for (size_t i = 0;i<option.second.commands.size();++i)
		{
			std::cout << option.second.commands[i];
			if (i<option.second.commands.size()-1)
			{
				std::cout << ",";
			}//for
			std::cout << ": " << option.second.help << "\n";
		}
		std::cout << "Press any key to close...";
	}
}

void CommandLineParser::parse(std::vector<const char*> arguments)
{
	bool printHelp = false;
	//Known arguments
	for (auto& option:options)
	{
		for (auto& command:option.second.commands)
		{
			for (size_t i = 0;i<arguments.size();++i)
			{
				if (strcmp(arguments[i],command.c_str()) ==0)
				{
					option.second.set = true;
					//Get value
					if (option.second.hasValue)
					{
						if (arguments.size()>i+1)
						{
							option.second.value = arguments[i + 1];
							++i;//jingz todo 既然有值且赋值了，理应跳过遍历
						}

						if (option.second.value == "")
						{
							printHelp = true;
							break;
						}
					}//if
				}//if 匹配正确
			}//for
		}//for commands
	}//for-options

	if (printHelp)
	{
		options["help"].set = true;
	}
}

bool CommandLineParser::isSet(std::string name)
{
	return options.find(name) != options.end() && options[name].set;
}

std::string CommandLineParser::getValueAsString(std::string name, std::string defaultValue)
{
	assert(options.find(name) != options.end());
	std::string value = options[name].value;
	return (value != "")?value:defaultValue;
}

int32_t CommandLineParser::getValueAsInt(std::string name, int32_t defaultValue)
{
	assert(options.find(name) != options.end());
	std::string value = options[name].value;

	if (value!="")
	{
		char* numConvPtr;
		int32_t intVal = strtol(value.c_str(), &numConvPtr, 10);
		return (intVal > 0) ? intVal : defaultValue;
	}
	else
	{
		return defaultValue;
	}
}

std::string VulkanExampleBase::getWindowTitle()
{
	std::string deviceName(deviceProperties.deviceName);
	std::string windowTile;
	windowTile = windowTitle + " - " + deviceName;
	if (!settings.overlay)
	{
		windowTile += " - " + std::to_string(frameCounter) + " fps";
	}

	return windowTile;
}

void VulkanExampleBase::windowResize()
{
	if (!prepared)
	{
		return;
	}

	prepared = false;
	resized = true;

	//Ensure all operations on the device have been finished before destroying resources
	vkDeviceWaitIdle(device);

	//Recreate swap chain
	width = destWidth;
	height = destHeight;
	setupSwapChain();

	// Recreate the frame buffers
	vkDestroyImageView(device, depthStencil.view, nullptr);
	vkDestroyImage(device, depthStencil.image, nullptr);
	vkFreeMemory(device, depthStencil.mem, nullptr);
	
	setupDepthStencil();
	
	for (uint32_t i = 0;i<frameBuffers.size();++i)
	{
		vkDestroyFramebuffer(device, frameBuffers[i], nullptr);
	}
	setupFrameBuffer();

	if (width>0.0f && height>0.0f)
	{
		if (settings.overlay)
		{
			uiOverlay.resize(width, height);
		}
	}

	// Command buffers need to be created as they may store references to the recreated frame buffer
	destroyCommandBuffers();
	createCommandBuffers();
	buildCommandBuffersAndRenderPrmitives();

	vkDeviceWaitIdle(device);

	if (width>0.0f && height>0.0f)
	{
		camera.updateAspectRatio((float)width / (float)height);
	}

	//Notify derived class
	windowResized();
	viewChanged();

	prepared = true;
}

void VulkanExampleBase::handleMouseMove(int32_t x, int32_t y)
{
	int32_t dx = (int32_t)mousePos.x - x;
	int32_t dy = (int32_t)mousePos.y - y;

	bool handled = false;

	if (settings.overlay)
	{
		ImGuiIO& io = ImGui::GetIO();
		handled = io.WantCaptureMouse;
	}

	mouseMoved((float)x, (float)y, handled);

	if (handled)
	{
		mousePos = glm::vec2((float)x, (float)y);
		return;
	}

	if (mouseButtons.left)
	{
		camera.rotate(glm::vec3(dy*camera.rotationSpeed, -dx * camera.rotationSpeed, 0.0f));
		viewUpdated = true;
	}
	if (mouseButtons.right)
	{
		camera.translate(glm::vec3(-0.0f, 0.0f, dy*0.005f));
		viewUpdated = true;
	}
	if (mouseButtons.middle)
	{
		camera.translate(glm::vec3(-dx*0.01f, -dy * 0.01f, 0.0f));
		viewUpdated = true;
	}
	mousePos = glm::vec2((float)x, (float)y);
}

void VulkanExampleBase::nextFrame()
{
	auto tStart = std::chrono::high_resolution_clock::now();
	if (viewUpdated)
	{
		viewUpdated = false;
		viewChanged();
	}

	render();
	frameCounter++;
	auto tEnd = std::chrono::high_resolution_clock::now();
	auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
	frameTimer = (float)tDiff / 1000.0f;
	camera.update(frameTimer);
	if (camera.moving())
	{
		viewUpdated = true;
	}
	//Convert to clamped timer value
	if (!paused)
	{
		timer += timerSpeed * frameTimer;
		if (timer > 1.0)
		{
			timer -= 1.0f;
		}
	}

	float fpsTimer = (float)std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
	if (fpsTimer > 1000.0f)
	{
		lastFPS = static_cast<uint32_t>((float)frameCounter*1000.0f / fpsTimer);

#if defined(_WIN32)
		if (!settings.overlay) {
			std::string windowTitle = getWindowTitle();
			SetWindowText(window, windowTitle.c_str());
		}
#endif
		frameCounter = 0;
		lastTimestamp = tEnd;
	}
	updateOverlay();
}

void VulkanExampleBase::updateOverlay()
{
	if (!settings.overlay)
	{
		return;
	}

	ImGuiIO& io = ImGui::GetIO();

	io.DisplaySize = ImVec2((float)width, (float)height);
	io.DeltaTime = frameTimer;

	io.MousePos = ImVec2(mousePos.x, mousePos.y);
	io.MouseDown[0] = mouseButtons.left;
	io.MouseDown[1] = mouseButtons.right;

	ImGui::NewFrame();

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::SetNextWindowPos(ImVec2(10, 10));
	ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiSetCond_FirstUseEver);
	ImGui::Begin("Vulkan Example", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::TextUnformatted(windowTitle.c_str());
	ImGui::TextUnformatted(deviceProperties.deviceName);
	ImGui::Text("%.2f ms/frame (%.1d fps)", (1000.0f / lastFPS), lastFPS);

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 5.0f * UIOverlay.scale));
#endif
	ImGui::PushItemWidth(110.0f * uiOverlay.scale);
	OnUpdateUIOverlay(&uiOverlay);
	ImGui::PopItemWidth();
#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	ImGui::PopStyleVar();
#endif

	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::Render();

	if (uiOverlay.update() || uiOverlay.updated) {
		buildCommandBuffersAndRenderPrmitives();
		uiOverlay.updated = false;
	}

#if defined(VK_USE_PLATFORM_ANDROID_KHR)
	if (mouseButtons.left) {
		mouseButtons.left = false;
	}
#endif
}

void VulkanExampleBase::createPipelineCache()
{
	VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
	pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	VK_CHECK_RESULT(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &pipelineCache));
}

void VulkanExampleBase::createCommandPool()
{
	VkCommandPoolCreateInfo cmdPoolInfo = {};
	cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmdPoolInfo.queueFamilyIndex = swapChain.queueNodeIndex;
	cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &cmdPool));
}

void VulkanExampleBase::createSynchronizationPrimitives()
{
	VkFenceCreateInfo fenceCreateInfo = vks::initializers::GenFenceCreateInfo(VK_FENCE_CREATE_SIGNALED_BIT);
	waitFences.resize(drawCmdBuffers.size());
	for (auto& fence:waitFences)
	{
		VK_CHECK_RESULT(vkCreateFence(device, &fenceCreateInfo, nullptr, &fence));
	}
}

void VulkanExampleBase::initSwapChainSurface()
{
#ifdef _WIN32
	swapChain.initSurface(windowInstance, window);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	swapChain.initSurface(androidApp->window);
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
	swapChain.initSurface(view);
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
	swapChain.initSurface(dfb, surface);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	swapChain.initSurface(display, surface);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	swapChain.initSurface(connection, window);
#elif (defined(_DIRECT2DISPLAY) || defined(VK_USE_PLATFORM_HEADLESS_EXT))
	swapChain.initSurface(width, height);
#endif
}

void VulkanExampleBase::setupSwapChain()
{
	swapChain.create(&width, &height, settings.vsync);
}

void VulkanExampleBase::createCommandBuffers()
{
	drawCmdBuffers.resize(swapChain.imageCount);

	VkCommandBufferAllocateInfo cmdBufferAllocateInfo = vks::initializers::GenCommandBufferAllocateInfo(
		cmdPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, static_cast<uint32_t>(drawCmdBuffers.size()));

	VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &cmdBufferAllocateInfo, drawCmdBuffers.data()));
}

void VulkanExampleBase::destroyCommandBuffers()
{
	vkFreeCommandBuffers(device, cmdPool, static_cast<uint32_t>(drawCmdBuffers.size()), drawCmdBuffers.data());
}

std::string VulkanExampleBase::getShaderPath() const
{
	return getAssetPath()+"shaders/"+shaderDir+"/";
}

VulkanExampleBase::VulkanExampleBase(bool enableValidation)
{
#ifndef VK_USE_PLATFORM_ANDROID_KHR

	// Check for a valid asset path
	struct stat Info;//?? //jingz
	if (stat(getAssetPath().c_str(),&Info)!=0)
	{
#ifdef _WIN32
		std::string msg = "Could not locate asset path in \"" + getAssetPath() + "\"!";
		MessageBox(NULL, msg.c_str(), "Fatal error", MB_OK | MB_ICONERROR);
#else
		std::cerr << "Error: Could not find asset path in " << getAssetPath() << "\n";
#endif // _WIN32
		exit(-1);
	}

#endif // !VK_USE_PLATFORM_ANDROID_KHR

	settings.validation = enableValidation;

	//Command line arguments
	commandLineParser.parse(args);

	if (commandLineParser.isSet("help"))
	{
#ifdef _WIN32
		setupConsole("Vulkan example");
#endif

		commandLineParser.printHelp();
		std::cin.get();
		exit(0);
	}

	if (commandLineParser.isSet("validation"))
	{
		settings.validation = true;
	}

	if (commandLineParser.isSet("vsync"))
	{
		settings.vsync = true;
	}

	if (commandLineParser.isSet("height"))
	{
		height = commandLineParser.getValueAsInt("height", height);
	}
	if (commandLineParser.isSet("width"))
	{
		width = commandLineParser.getValueAsInt("width", width);
	}

	if (commandLineParser.isSet("fullscreen"))
	{
		settings.fullscreen = true;
	}

	if (commandLineParser.isSet("shaders"))
	{
		std::string value = commandLineParser.getValueAsString("shaders", "glsl");
		if ((value!="glsl") &&(value!="hlsl"))
		{
			std::cerr << "Shader type must be one of 'glsl' or 'hlsl'\n";
		}
		else
		{
			shaderDir = value;
		}
	}//if shaders

	if (commandLineParser.isSet("benchmark"))
	{
		benchmark.active = true;
		vks::tools::errorModeSilent = true;
	}

	if (commandLineParser.isSet("benchmarkwarmup"))
	{
		benchmark.warmup = commandLineParser.getValueAsInt("benchmarkwarmup", benchmark.warmup);
	}
	if (commandLineParser.isSet("benchmarkruntime"))
	{
		benchmark.duration = commandLineParser.getValueAsInt("commandLineParser", benchmark.duration);
	}
	if (commandLineParser.isSet("benchmarkresultfile"))
	{
		benchmark.filename = commandLineParser.getValueAsString("benchmarkresultfile", benchmark.filename);
	}
	if (commandLineParser.isSet("benchmarkresultframes"))
	{
		benchmark.outputFrames = true;
	}
	if (commandLineParser.isSet("benchmarkframes")) {
		benchmark.outputFrames = commandLineParser.getValueAsInt("benchmarkframes", benchmark.outputFrames);
	}


#ifdef VK_USE_PLATFORM_ANDROID_KHR
	// Vulkan library is loaded dynamically on Android
	bool libLoaded = vks::android::loadVulkanLibrary();
	assert(libLoaded);
#elif defined(_DIRECT2DISPLAY)

#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	initWaylandConnection();
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	initxcbConnection();
#endif

#ifdef _WIN32
	if (this->settings.validation)
	{
		setupConsole("Vulkan example");
	}
	setupDPIAwareness();
#endif

}

VulkanExampleBase::~VulkanExampleBase()
{
	// Clean up Vulkan resources
	swapChain.cleanup();
	if (descriptorPool!=VK_NULL_HANDLE)
	{
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	}

	destroyCommandBuffers();
	//if (renderPass!=)
	//{
	//}
}

bool VulkanExampleBase::initVulkanSetting()
{
	VkResult err;

	//Vulkan instance
	err = createInstance(settings.validation);

	if (err)
	{
		vks::tools::exitFatal("Could not create Vulkan instance: \n" + vks::tools::errorString(err), err);
		return false;
	}

#ifdef VK_USE_PLATFORM_ANDROID_KHR
	vks::android::loadVulkanFunctions(instance);
#endif

	// If requested, we enable the default validation layers for debugging
	if (settings.validation)
	{
		// The report flags determine what type of message for the layers will be displayed
		// For validation (debugging) an application the error and warming bits should be suffice
		VkDebugReportFlagsEXT debugReportFlags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;

		//Additional flags include performance info,loader and layer debug message,etc.
		vks::debug::setupDebugging(instance, debugReportFlags, VK_NULL_HANDLE);
	}

	// Physical device
	uint32_t gpuCount = 0;
	// Get number of available physical devices
	VK_CHECK_RESULT(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr));
	if (gpuCount == 0)
	{
		vks::tools::exitFatal("No device with Vulkan support found", -1);
		return false;
	}

	// Enumerate devices
	std::vector<VkPhysicalDevice> tempPhysicalDevices(gpuCount);
	err = vkEnumeratePhysicalDevices(instance, &gpuCount, tempPhysicalDevices.data());
	if (err)
	{
		vks::tools::exitFatal("Could not enumerate physical devices: \n" + vks::tools::errorString(err), err);
		return false;
	}

	// GPU selection

	// Select physical device to be used for the vulkan example
	// Defaults to the first device unless sepcified by command line
	uint32_t selectedDevice = 0;

#ifndef VK_USE_PLATFORM_ANDROID_KHR

	// GPU selection via command line argument
	if (commandLineParser.isSet("gpuselection"))
	{
		uint32_t index = commandLineParser.getValueAsInt("gpuselection", 0);
		if (index>gpuCount -1)
		{
			std::cerr<< " Selected device index"<< index << " is out of range, reverting to device 0 (use -listgpus to show available Vulkan devices)" << "\n";
		}
		else
		{
			selectedDevice = index;
		}
	}// gpuselection

	if (commandLineParser.isSet("gpulist"))
	{
		std::cout << "Available Vulkan devices" << "\n";
		for (uint32_t i = 0;i<gpuCount;++i)
		{
			VkPhysicalDeviceProperties deviceProperties;
			vkGetPhysicalDeviceProperties(tempPhysicalDevices[i], &deviceProperties);
			std::cout << "Device [" << i << "] : " << deviceProperties.deviceName << std::endl;
			std::cout << " Type: " << vks::tools::physicalDeviceTypeString(deviceProperties.deviceType) << "\n";
			std::cout << " API: " << (deviceProperties.apiVersion >> 22) << "." << ((deviceProperties.apiVersion >> 12) & 0x3ff) << "." << (deviceProperties.apiVersion & 0xfff) << "\n";
		}
	}// gpulist

#endif

	physicalDevice = tempPhysicalDevices[selectedDevice];

	// Store properties (including limits), features and memory properties of the physical device(so that examples can check against them)
	vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
	vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemoryProperties);

	// Derived examples can override this to set actual features (based on above readings) to enable for logical device creation
	getEnabledFeatures();

	// Vulkan device creation
	// This is handled by a separate class that gets a logical device representation and encapsulates functions related to a device
	vulkanDevice = new vks::VulkanDevice(physicalDevice);
	VkResult res = vulkanDevice->CreateLogicalDevice(enabledFeatures, enableDeviceExtensions, deviceCreateNextChain);
	if (res != VK_SUCCESS)
	{
		vks::tools::exitFatal("Could not create Vulkan device: \n" + vks::tools::errorString(res), res);
		return false;
	}
	device = vulkanDevice->logicalDevice;

	// Get a graphics queue from the device
	vkGetDeviceQueue(device, vulkanDevice->queueFamilyIndices.graphicIndex, 0, &queue);

	//find a suitable depth format
	VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &depthFormat);
	assert(validDepthFormat);

	swapChain.connect(instance, physicalDevice, device);

	// Create synchronization objects
	VkSemaphoreCreateInfo semaphoreCreateInfo = vks::initializers::GenSemaphoneCreateInfo();
	
	// Create a semaphore used to synchronize image presentation
	//Ensure that the image is displayed before we start submitting new commands to the queue
	VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphores.presentComplete));
	
	// Create a semaphore used to synchronize command submission
	// Ensures that the image is not present until all commands have been submitted and executed
	VK_CHECK_RESULT(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphores.renderComplete));

	// Set up submit info structure
	// Semaphores will stay the same during application lifetime
	// Command buffer submission info is set by each example
	submitInfo = vks::initializers::GenSubmitInfo();
	submitInfo.pWaitDstStageMask = &submitPipelineStages;
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = &semaphores.presentComplete;
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &semaphores.renderComplete;

	return true;
}

void VulkanExampleBase::setupConsole(std::string title)
{
	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	FILE* stream;
	freopen_s(&stream, "CONIN$", "r", stdin);
	freopen_s(&stream, "CONOUT$", "w+", stdout);
	freopen_s(&stream, "CONOUT$", "w+", stderr);
	SetConsoleTitle(TEXT(title.c_str()));
}

void VulkanExampleBase::setupDPIAwareness()
{
	typedef HRESULT *(__stdcall *SetProcessDpiAwarenessFunc)(PROCESS_DPI_AWARENESS);

	HMODULE shCore = LoadLibraryA("Shcore.dll");
	if (shCore)
	{
		SetProcessDpiAwarenessFunc setProcessDpiAwareness =
			(SetProcessDpiAwarenessFunc)GetProcAddress(shCore, "SetProcessDpiAwareness");

		if (setProcessDpiAwareness != nullptr)
		{
			setProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
		}

		FreeLibrary(shCore);
	}
}

HWND VulkanExampleBase::setupWindow(HINSTANCE hinstance, WNDPROC wndproc)
{
	this->windowInstance = hinstance;

	WNDCLASSEX wndClass;

	wndClass.cbSize = sizeof(WNDCLASSEX);
	wndClass.style = CS_HREDRAW | CS_VREDRAW;
	wndClass.lpfnWndProc = wndproc;
	wndClass.cbClsExtra = 0;
	wndClass.cbWndExtra = 0;
	wndClass.hInstance = hinstance;
	wndClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wndClass.lpszMenuName = NULL;
	wndClass.lpszClassName = appName.c_str();
	wndClass.hIconSm = LoadIcon(NULL, IDI_WINLOGO);

	if (!RegisterClassEx(&wndClass))
	{
		std::cout << "Could not register window class! \n";
		fflush(stdout);
		exit(1);
	}

	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);

	if (settings.fullscreen)
	{
		if ((width != (uint32_t)screenWidth) && (height != (uint32_t)screenHeight))
		{
			DEVMODE dmScreenSettings;
			memset(&dmScreenSettings, 0, sizeof(dmScreenSettings));
			dmScreenSettings.dmSize = sizeof(dmScreenSettings);
			dmScreenSettings.dmPelsWidth = width;
			dmScreenSettings.dmPanningHeight = height;
			dmScreenSettings.dmBitsPerPel = 32;
			dmScreenSettings.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
			if (ChangeDisplaySettings(&dmScreenSettings,CDS_FULLSCREEN)!=DISP_CHANGE_SUCCESSFUL)
			{
				if (MessageBox(NULL, "Full screen Mode not supported!\n Switch to window mode?", "Error", MB_YESNO | MB_ICONEXCLAMATION) == IDYES)
				{
					settings.fullscreen = false;
				}
				else
				{
					return nullptr;
				}
			}
			screenWidth = width;
			screenHeight = height;
		}
	}//settings.fullscreen

	DWORD dwExStycle;
	DWORD dwStycle;

	if (settings.fullscreen)
	{
		dwExStycle = WS_EX_APPWINDOW;
		dwStycle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	}
	else
	{
		dwExStycle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
		dwStycle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	}

	RECT windowRect;
	windowRect.left = 0L;
	windowRect.top = 0L;
	windowRect.right = settings.fullscreen ? (long)screenWidth : (long)width;
	windowRect.bottom = settings.fullscreen ? (long)screenHeight : (long)height;

	AdjustWindowRectEx(&windowRect, dwStycle, FALSE, dwExStycle); 
	
	std::string windowTitle = getWindowTitle();
	window = CreateWindowEx(0, appName.c_str(), windowTitle.c_str(), dwStycle | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0,
		windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, NULL, NULL, hinstance, NULL);

	if (!settings.fullscreen)
	{
		// Center on screen
		uint32_t x = (GetSystemMetrics(SM_CXSCREEN) - windowRect.right) / 2;
		uint32_t y = (GetSystemMetrics(SM_CYSCREEN) - windowRect.bottom) / 2;
		SetWindowPos(window, 0, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	}

	if (!window)
	{
		printf("Could not create window!\n");
		fflush(stdout);
		return nullptr;
	}

	ShowWindow(window, SW_SHOW);
	SetForegroundWindow(window);
	SetFocus(window);

	return window;
}

void VulkanExampleBase::handleMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CLOSE:
		prepared = false;
		DestroyWindow(hWnd);
		PostQuitMessage(0);
		break;

	case WM_PAINT:
		ValidateRect(window, NULL);
		break;
	case WM_KEYDOWN:
		switch (wParam)
		{
		case  KEY_P:
			paused = !paused;
			break;
		case  KEY_F1:
			if (settings.overlay)
			{
				uiOverlay.visible = !uiOverlay.visible;
			}
			break;
		case KEY_ESCAPE:
			PostQuitMessage(0);
			break;
		default:
			break;
		}

		if (camera.cameraType == Camera::firstperson)
		{
			switch (wParam)
			{
			case KEY_W:
				camera.keys.up = true;
				break;
			case KEY_S:
				camera.keys.down = true;
				break;
			case KEY_A:
				camera.keys.left = true;
				break;
			case KEY_D:
				camera.keys.right = true;
				break;

			default:
				break;
			}
		}

		keyPressed((uint32_t)wParam);
		break;

	case WM_KEYUP:
		if (camera.cameraType == Camera::firstperson)
		{
			switch (wParam)
			{
			case KEY_W:
				camera.keys.up = false;
				break;
			case KEY_S:
				camera.keys.down = false;
				break;
			case KEY_A:
				camera.keys.left = false;
				break;
			case KEY_D:
				camera.keys.right = false;
				break;
			}
		}
		break;

	case WM_LBUTTONDOWN:
		mousePos = glm::vec2((float)LOWORD(lParam), (float)HIWORD(lParam));
		mouseButtons.left = true;
		break;
	case WM_RBUTTONDOWN:
		mousePos = glm::vec2((float)LOWORD(lParam), (float)HIWORD(lParam));
		mouseButtons.right = true;
		break;
	case WM_MBUTTONDOWN:
		mousePos = glm::vec2((float)LOWORD(lParam), (float)HIWORD(lParam));
		mouseButtons.middle = true;
		break;

	case WM_LBUTTONUP:
		mouseButtons.left = false;
		break;
	case WM_RBUTTONUP:
		mouseButtons.right = false;
		break;
	case WM_MBUTTONUP:
		mouseButtons.middle = false;
		break;

	case WM_MOUSEHWHEEL:
	{
		short wheelData = GET_WHEEL_DELTA_WPARAM(wParam);
		camera.translate(glm::vec3(0.0f, 0.0f, (float)wheelData*0.005f));
		viewUpdated = true;
		break;
	}
	
	case WM_MOUSEMOVE:
	{
		handleMouseMove(LOWORD(lParam), HIWORD(lParam));
		break;
	}

	case WM_SIZE:
		if (prepared && (wParam!=SIZE_MINIMIZED))
		{
			if (resizing || (wParam == SIZE_MAXIMIZED)||(wParam == SIZE_RESTORED))
			{
				destWidth = LOWORD(lParam);
				destHeight = HIWORD(lParam);
				windowResize();
			}
		}
		break;

	case WM_GETMINMAXINFO:
	{
		LPMINMAXINFO minMaxInfo = (LPMINMAXINFO)lParam;
		minMaxInfo->ptMinTrackSize.x = 64;
		minMaxInfo->ptMinTrackSize.y = 64;
		break;
	}

	case WM_ENTERSIZEMOVE:
		resizing = true;;
		break;

	case WM_EXITSIZEMOVE:
		resizing = false;
		break;

	default:
		break;
	}
}

VkResult VulkanExampleBase::createInstance(bool enableValidation)
{
	this->settings.validation = enableValidation;

	// Validation can also be forced via a define
#ifdef _VALIDATION
	this->settings.validation = true;
#endif // _VALIDATION

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = appName.c_str();
	appInfo.pEngineName = appName.c_str();
	appInfo.apiVersion = apiVersion;

	std::vector<const char*> instanceExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };

	// Enable surface extensions depending on OS
#ifdef _WIN32
	instanceExtensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	instanceExtensions.push_back(VK_KHR_ANDROID_SURFACE_EXTENSION_NAME);
#elif defined(_DIRECT2DISPLAY)
	instanceExtensions.push_back(VK_KHR_DISPLAY_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
	instanceExtensions.push_back(VK_EXT_DIRECTFB_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	instanceExtensions.push_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	instanceExtensions.push_back(VK_KHR_XCB_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_IOS_MVK)
	instanceExtensions.push_back(VK_MVK_IOS_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_MACOS_MVK)
	instanceExtensions.push_back(VK_MVK_MACOS_SURFACE_EXTENSION_NAME);
#elif defined(VK_USE_PLATFORM_HEADLESS_EXT)
	instanceExtensions.push_back(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
#endif

	// Get extensions supported by the instance and store for later use
	uint32_t extCount = 0;
	vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
	if (extCount>0)
	{
		std::vector<VkExtensionProperties> extensions(extCount);

		if (vkEnumerateInstanceExtensionProperties(nullptr,&extCount,&extensions.front())==VK_SUCCESS)
		{
			for (VkExtensionProperties extension:extensions)
			{
				supportedInstanceExtensions.push_back(extension.extensionName);
			}
		}//if
	}//if extCount

	//Enable requested instance extensions
	if (enableInstanceExtensions.size()>0)
	{
		for (const char* enableExtension :enableInstanceExtensions)
		{
			//Output message if requested extension is not available
			if (std::find(supportedInstanceExtensions.begin(),supportedInstanceExtensions.end(),enableExtension)==supportedInstanceExtensions.end())
			{
				std::cerr << "Enabled instance extension \"" << enableExtension << "\" is not present at instance level\n";
			}
			instanceExtensions.push_back(enableExtension);
		}
	}//if

	VkInstanceCreateInfo instanceCreateInfo = {};
	instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceCreateInfo.pNext = NULL;
	instanceCreateInfo.pApplicationInfo = &appInfo;
	if (instanceExtensions.size()>0)
	{
		if (settings.validation)
		{
			instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		instanceCreateInfo.enabledExtensionCount = (uint32_t)instanceExtensions.size();
		instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
	}

	// The VK_LAYER_KHRONOS_validation contains all current validation functionality.
	// Note that on Android this layer requires at least NDK r20
	const char* validtaionLayerName = "VK_LAYER_KHRONOS_validation";
	if (settings.validation)
	{
		// Check if this layer is available at instance level
		uint32_t  instanceLayerCount;
		vkEnumerateInstanceLayerProperties(&instanceLayerCount, nullptr);
		std::vector<VkLayerProperties> instanceLayerProperties(instanceLayerCount);
		vkEnumerateInstanceLayerProperties(&instanceLayerCount, instanceLayerProperties.data());

		bool validationLayerPresent = false;
		for (VkLayerProperties layer: instanceLayerProperties)
		{
			if (strcmp(layer.layerName,validtaionLayerName)==0)
			{
				validationLayerPresent = true;
				break;
			}
		}

		if (validationLayerPresent)
		{
			instanceCreateInfo.ppEnabledLayerNames = &validtaionLayerName;
			instanceCreateInfo.enabledLayerCount = 1;
		}
		else
		{
			std::cerr << "Validation layer VK_LAYER_KHRONOS_validation not present,validation is disabled";
		}
	}

	return vkCreateInstance(&instanceCreateInfo, nullptr, &instance);
}

void VulkanExampleBase::viewChanged()
{
}

void VulkanExampleBase::keyPressed(uint32_t)
{
}

void VulkanExampleBase::mouseMoved(double x, double y, bool & handled)
{
}

void VulkanExampleBase::windowResized()
{
}

void VulkanExampleBase::buildCommandBuffersAndRenderPrmitives()
{
}

void VulkanExampleBase::setupDepthStencil()
{
	VkImageCreateInfo imageCI{};
	imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageCI.imageType = VK_IMAGE_TYPE_2D;
	imageCI.format = depthFormat;
	imageCI.extent = { width,height,1 };
	imageCI.mipLevels = 1;
	imageCI.arrayLayers = 1;
	imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
	imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageCI.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	VK_CHECK_RESULT(vkCreateImage(device, &imageCI, nullptr, &depthStencil.image));
	VkMemoryRequirements memReqs{};
	vkGetImageMemoryRequirements(device, depthStencil.image, &memReqs);

	VkMemoryAllocateInfo memAlloc{};
	memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memAlloc.allocationSize = memReqs.size;
	memAlloc.memoryTypeIndex = vulkanDevice->GetMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &depthStencil.mem));
	VK_CHECK_RESULT(vkBindImageMemory(device, depthStencil.image, depthStencil.mem, 0));

	VkImageViewCreateInfo imageViewCI{};
	imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
	imageViewCI.image = depthStencil.image;
	imageViewCI.format = depthFormat;
	imageViewCI.subresourceRange.baseMipLevel = 0;
	imageViewCI.subresourceRange.levelCount = 1;
	imageViewCI.subresourceRange.baseArrayLayer = 0;
	imageViewCI.subresourceRange.layerCount = 1;
	imageViewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

	//stencil aspect should only be set on depth+stencil formats (VK_FORMAT_D16_UNORM_S8_UINT..VK_FORMAT_D32_SFLOAT_S8_UINT)
	if (depthFormat>=VK_FORMAT_D16_UNORM_S8_UINT)
	{
		imageViewCI.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
	}

	VK_CHECK_RESULT(vkCreateImageView(device, &imageViewCI, nullptr, &depthStencil.view));
}

void VulkanExampleBase::setupFrameBuffer()
{
	VkImageView attachments[2];

	// Depth/Stencil attachment is the same for all frame buffers
	attachments[1] = depthStencil.view;

	VkFramebufferCreateInfo frameBufferCreateInfo = {};
	frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	frameBufferCreateInfo.pNext = NULL;
	frameBufferCreateInfo.renderPass = renderPass;
	frameBufferCreateInfo.attachmentCount = 2;
	frameBufferCreateInfo.pAttachments = attachments;
	frameBufferCreateInfo.width = width;
	frameBufferCreateInfo.height = height;
	frameBufferCreateInfo.layers = 1;

	//Create frame buffers for every swap chain image
	frameBuffers.resize(swapChain.imageCount);//多层缓冲
	for (uint32_t i = 0;i<frameBuffers.size();++i)
	{
		attachments[0] = swapChain.buffers[i].view;
		VK_CHECK_RESULT(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &frameBuffers[i]));
	}
}

void VulkanExampleBase::setupRenderPass()
{
	std::array<VkAttachmentDescription, 2> attachments = {};

	//Color attachment
	attachments[0].format = swapChain.colorFormat;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;//jingz todo 为什么不保持stencil
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	//Depth attachment
	attachments[1].format = depthFormat;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorReference = {};
	colorReference.attachment = 0;
	colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthReference = {};
	depthReference.attachment = 1;
	depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpassDescription = {};
	subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpassDescription.colorAttachmentCount = 1;
	subpassDescription.pColorAttachments = &colorReference;
	subpassDescription.pDepthStencilAttachment = &depthReference;
	subpassDescription.inputAttachmentCount = 0;
	subpassDescription.pInputAttachments = nullptr;
	subpassDescription.preserveAttachmentCount = 0;
	subpassDescription.pPreserveAttachments = nullptr;
	subpassDescription.pResolveAttachments = nullptr;

	// Subpass dependencies for layout transitions
	std::array<VkSubpassDependency, 2>dependencies;

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
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpassDescription;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
}

void VulkanExampleBase::getEnabledFeatures()
{
}

void VulkanExampleBase::prepareForRendering()
{
	if (vulkanDevice->enableDebugMarkers)
	{
		vks::debugmarker::setup(device);
	}

	initSwapChainSurface();
	createCommandPool();
	setupSwapChain();
	createCommandBuffers();
	createSynchronizationPrimitives();
	setupDepthStencil();
	setupRenderPass();
	createPipelineCache();
	setupFrameBuffer();

	settings.overlay = settings.overlay && (!benchmark.active);
	if (settings.overlay)
	{
		uiOverlay.device = vulkanDevice;
		uiOverlay.queue = queue;
		uiOverlay.shaders =
		{
			loadShader(getShaderPath() + "base/uioverlay.vert.spv",VK_SHADER_STAGE_VERTEX_BIT),
			loadShader(getShaderPath() + "base/uioverlay.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT),
		};
		uiOverlay.prepareResources();
		uiOverlay.preparePipeline(pipelineCache, renderPass);
	}
}

VkPipelineShaderStageCreateInfo VulkanExampleBase::loadShader(std::string fileName, VkShaderStageFlagBits stage)
{
	VkPipelineShaderStageCreateInfo shaderStage = {};
	shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStage.stage = stage;

#ifdef VK_USE_PLATFORM_ANDROID_KHR
	shaderStage.module = vks::tools::loadShader(androidApp->activity->assetManager, fileName.c_str(), device);
#else
	shaderStage.module = vks::tools::loadShader(fileName.c_str(), device);
#endif

	shaderStage.pName = "main";
	assert(shaderStage.module != VK_NULL_HANDLE);
	shaderModules.push_back(shaderStage.module);

	return shaderStage;
}

void VulkanExampleBase::renderLoop()
{
	if (benchmark.active)
	{
		benchmark.run(
			[=] {render(); },
			vulkanDevice->properties
		);

		vkDeviceWaitIdle(device);
		if (benchmark.filename!="")
		{
			benchmark.saveResults();
		}
	}

	destWidth = width;
	destHeight = height;
	lastTimestamp = std::chrono::high_resolution_clock::now();

#ifdef _WIN32
	MSG msg;
	bool quitMessageReceived = false;
	while (!quitMessageReceived)
	{
		while (PeekMessage(&msg,NULL,0,0,PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message ==WM_QUIT)
			{
				quitMessageReceived = true;
				break;
			}
		}//while PeekMessage
		
		if (prepared && !IsIconic(window))
			{
				nextFrame();//逻辑启动点
			}
	}//while quitMessageReceived
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	while (1)
	{
		int ident;
		int events;
		struct android_poll_source* source;
		bool destroy = false;

		focused = true;

		while ((ident = ALooper_pollAll(focused ? 0 : -1, NULL, &events, (void**)&source)) >= 0)
		{
			if (source != NULL)
			{
				source->process(androidApp, source);
			}
			if (androidApp->destroyRequested != 0)
			{
				LOGD("Android app destroy requested");
				destroy = true;
				break;
			}
		}

		// App destruction requested
		// Exit loop, example will be destroyed in application main
		if (destroy)
		{
			ANativeActivity_finish(androidApp->activity);
			break;
		}

		// Render frame
		if (prepared)
		{
			auto tStart = std::chrono::high_resolution_clock::now();
			render();
			frameCounter++;
			auto tEnd = std::chrono::high_resolution_clock::now();
			auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
			frameTimer = tDiff / 1000.0f;
			camera.update(frameTimer);
			// Convert to clamped timer value
			if (!paused)
			{
				timer += timerSpeed * frameTimer;
				if (timer > 1.0)
				{
					timer -= 1.0f;
				}
			}
			float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
			if (fpsTimer > 1000.0f)
			{
				lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
				frameCounter = 0;
				lastTimestamp = tEnd;
			}

			// TODO: Cap UI overlay update rates/only issue when update requested
			updateOverlay();

			bool updateView = false;

			// Check touch state (for movement)
			if (touchDown) {
				touchTimer += frameTimer;
			}
			if (touchTimer >= 1.0) {
				camera.keys.up = true;
				viewChanged();
			}

			// Check gamepad state
			const float deadZone = 0.0015f;
			// todo : check if gamepad is present
			// todo : time based and relative axis positions
			if (camera.type != Camera::CameraType::firstperson)
			{
				// Rotate
				if (std::abs(gamePadState.axisLeft.x) > deadZone)
				{
					camera.rotate(glm::vec3(0.0f, gamePadState.axisLeft.x * 0.5f, 0.0f));
					updateView = true;
				}
				if (std::abs(gamePadState.axisLeft.y) > deadZone)
				{
					camera.rotate(glm::vec3(gamePadState.axisLeft.y * 0.5f, 0.0f, 0.0f));
					updateView = true;
				}
				// Zoom
				if (std::abs(gamePadState.axisRight.y) > deadZone)
				{
					camera.translate(glm::vec3(0.0f, 0.0f, gamePadState.axisRight.y * 0.01f));
					updateView = true;
				}
				if (updateView)
				{
					viewChanged();
				}
			}
			else
			{
				updateView = camera.updatePad(gamePadState.axisLeft, gamePadState.axisRight, frameTimer);
				if (updateView)
				{
					viewChanged();
				}
			}
		}
	}
#elif defined(_DIRECT2DISPLAY)
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
			viewChanged();
		}
		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		// Convert to clamped timer value
		if (!paused)
		{
			timer += timerSpeed * frameTimer;
			if (timer > 1.0)
			{
				timer -= 1.0f;
			}
		}
		float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
		if (fpsTimer > 1000.0f)
		{
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			frameCounter = 0;
			lastTimestamp = tEnd;
		}
		updateOverlay();
	}
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
			viewChanged();
		}
		DFBWindowEvent event;
		while (!event_buffer->GetEvent(event_buffer, DFB_EVENT(&event)))
		{
			handleEvent(&event);
		}
		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		// Convert to clamped timer value
		if (!paused)
		{
			timer += timerSpeed * frameTimer;
			if (timer > 1.0)
			{
				timer -= 1.0f;
			}
		}
		float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
		if (fpsTimer > 1000.0f)
		{
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			frameCounter = 0;
			lastTimestamp = tEnd;
		}
		updateOverlay();
	}
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
			viewChanged();
		}

		while (!configured)
			wl_display_dispatch(display);
		while (wl_display_prepare_read(display) != 0)
			wl_display_dispatch_pending(display);
		wl_display_flush(display);
		wl_display_read_events(display);
		wl_display_dispatch_pending(display);

		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		// Convert to clamped timer value
		if (!paused)
		{
			timer += timerSpeed * frameTimer;
			if (timer > 1.0)
			{
				timer -= 1.0f;
			}
		}
		float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
		if (fpsTimer > 1000.0f)
		{
			if (!settings.overlay)
			{
				std::string windowTitle = getWindowTitle();
				xdg_toplevel_set_title(xdg_toplevel, windowTitle.c_str());
			}
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			frameCounter = 0;
			lastTimestamp = tEnd;
		}
		updateOverlay();
	}
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	xcb_flush(connection);
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
			viewChanged();
		}
		xcb_generic_event_t *event;
		while ((event = xcb_poll_for_event(connection)))
		{
			handleEvent(event);
			free(event);
		}
		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		// Convert to clamped timer value
		if (!paused)
		{
			timer += timerSpeed * frameTimer;
			if (timer > 1.0)
			{
				timer -= 1.0f;
			}
		}
		float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
		if (fpsTimer > 1000.0f)
		{
			if (!settings.overlay)
			{
				std::string windowTitle = getWindowTitle();
				xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
					window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
					windowTitle.size(), windowTitle.c_str());
			}
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			frameCounter = 0;
			lastTimestamp = tEnd;
		}
		updateOverlay();
	}
#elif defined(VK_USE_PLATFORM_HEADLESS_EXT)
	while (!quit)
	{
		auto tStart = std::chrono::high_resolution_clock::now();
		if (viewUpdated)
		{
			viewUpdated = false;
			viewChanged();
		}
		render();
		frameCounter++;
		auto tEnd = std::chrono::high_resolution_clock::now();
		auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
		frameTimer = tDiff / 1000.0f;
		camera.update(frameTimer);
		if (camera.moving())
		{
			viewUpdated = true;
		}
		// Convert to clamped timer value
		timer += timerSpeed * frameTimer;
		if (timer > 1.0)
		{
			timer -= 1.0f;
		}
		float fpsTimer = std::chrono::duration<double, std::milli>(tEnd - lastTimestamp).count();
		if (fpsTimer > 1000.0f)
		{
			lastFPS = (float)frameCounter * (1000.0f / fpsTimer);
			frameCounter = 0;
			lastTimestamp = tEnd;
		}
		updateOverlay();
	}
#elif (defined(VK_USE_PLATFORM_MACOS_MVK) && defined(VK_EXAMPLE_XCODE_GENERATED))
	[NSApp run];
#endif

	//Flush device to make sure all resources can be freed
	if (device!=VK_NULL_HANDLE)
	{
		vkDeviceWaitIdle(device);
	}

}

void VulkanExampleBase::drawUI(const VkCommandBuffer commandBuffer)
{
	if (settings.overlay)
	{
		const VkViewport viewport = vks::initializers::GenViewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

		const VkRect2D scissor = vks::initializers::GenRect2D(width, height, 0, 0);
		vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

		uiOverlay.draw(commandBuffer);
	}
}

void VulkanExampleBase::prepareFrame()
{
	//Acquire the next image from the swap chain
	VkResult result = swapChain.acquireNextImage(semaphores.presentComplete, &currentBuffer);

	//Recreate the swap chain if it's no longer compatible with the surface (OUT_OF_DATE) or no longer optimal for presentation (SUBOPTIMAL)
	if (result==VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
	{
		windowResize();
	}
	else
	{
		VK_CHECK_RESULT(result);
	}
}

void VulkanExampleBase::submitFrame()
{
	VkResult result = swapChain.queuePresent(queue, currentBuffer, semaphores.renderComplete);
	if (!(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR))
	{
		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			windowResize();
			return;
		}
		else
		{
			VK_CHECK_RESULT(result);
		}
	}

	VK_CHECK_RESULT(vkQueueWaitIdle(queue));
}

void VulkanExampleBase::renderFrame()
{
	VulkanExampleBase::prepareFrame();
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

	VK_CHECK_RESULT(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));

	VulkanExampleBase::submitFrame();
}

void VulkanExampleBase::OnUpdateUIOverlay(vks::UIOverlay * overlay)
{
}
