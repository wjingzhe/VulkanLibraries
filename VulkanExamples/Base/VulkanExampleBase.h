/*
* Vulkan Example base class
*
* Copyright (C) by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#pragma once

#ifdef _WIN32
#pragma comment(linker, "/subsystem:windows")
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <ShellScalingAPI.h>
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
#include <android/native_activity.h>
#include <android/asset_manager.h>
#include <android_native_app_glue.h>
#include <sys/system_properties.h>
#include "VulkanAndroid.h"
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
#include <directfb.h>
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#elif defined(_DIRECT2DISPLAY)
//
#elif defined(VK_USE_PLATFORM_XCB_KHR)
#include <xcb/xcb.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <array>
#include <unordered_map>
#include <numeric>
#include <ctime>
#include <iostream>
#include <chrono>
#include <random>
#include <algorithm>
#include <sys/stat.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <numeric>
#include <array>

#include "vulkan/vulkan.h"

#include "keycodes.hpp"
#include "VulkanTools.h"
#include "VulkanDebug.h"
#include "VulkanUIOverlay.h"
#include "VulkanSwapChain.h"
#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "VulkanTexture.h"

#include "VulkanInitializers.hpp"
#include "camera.hpp"
#include "benchmark.hpp"

class CommandLineParser
{
public:
	struct CommandLineOption
	{
		std::vector<std::string> commands;
		std::string value;
		bool hasValue = false;
		std::string help;
		bool set = false;
	};

	std::unordered_map<std::string, CommandLineOption> options;
	CommandLineParser();
	void add(std::string name, std::vector<std::string> commands, bool hasValue, std::string help);
	void printHelp();
	void parse(std::vector<const char*> arguments);
	bool isSet(std::string name);
	std::string getValueAsString(std::string name, std::string defaultValue);
	int32_t getValueAsInt(std::string name, int32_t defaultValue);
};

class VulkanExampleBase
{
private:
	std::string getWindowTitle();
	bool viewUpdated = false;
	uint32_t destWidth;
	uint32_t destHeight;
	bool resizing = false;
	void windowResize();
	void handleMouseMove(int32_t x, int32_t y);
	void nextFrame();
	void updateOverlay();
	void createPipelineCache();
	void createCommandPool();
	void createSynchronizationPrimitives();
	void initSwapChainSurface();
	void setupSwapChain();
	void createCommandBuffers();
	void destroyCommandBuffers();
	std::string shaderDir = "glsl";
protected:
	// Returns the path to the root of the glsl or hlsl shader directory
	std::string getShadersPath() const;

	// Frame counter to display fps
	uint32_t frameCounter = 0;
	uint32_t lastFPS = 0;
	std::chrono::time_point<std::chrono::high_resolution_clock> lastTimestamp;
	VkInstance instance;
	std::vector<std::string> supportedInstanceExtensions;
	// Physical device(GPU) that Vulkan will use
	VkPhysicalDevice physicalDevice;
	// Stores physical device properties (for e.g. checking device limits)
	VkPhysicalDeviceProperties deviceProperties;
	// Stores all features available on the selected physical device (for e.g checking if a feature is available)
	VkPhysicalDeviceFeatures deviceFeatures;
	//Stores all available memory(type) properties for the physical device
	VkPhysicalDeviceMemoryProperties deviceMemoryProperties;
	// Set of physical device features to be enabled for this example (must be set in the derived constructor)
	VkPhysicalDeviceFeatures curEnabledDeviceFeatures{};
	std::vector<const char*> enabledDeviceExtensions;
	std::vector<const char*> enabledInstanceExtensions;

	void* deviceCreateNextChain = nullptr;
	
	// logical device, application's view of the physical device(GPU)
	VkDevice device;

	// handle to the device graphics queue that command buffers are submitted to
	VkQueue queue;
	
	// Depth buffer format (selected during Vulkan initialization)
	VkFormat depthFormat;

	//Command buffer pool
	VkCommandPool cmdPool;

	VkPipelineStageFlags submitPipelineStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	//Contains command buffers and semaphores to be presented to the queue
	VkSubmitInfo submitInfo;
	// Command buffers used for rendering
	std::vector<VkCommandBuffer> drawCmdBuffers;

	//Global render pass for frame buffer writes
	VkRenderPass renderPass = VK_NULL_HANDLE;
	// List of available frame buffers (same as number of swap chain images)
	std::vector<VkFramebuffer> frameBuffers;
	//Active frame buffer index
	uint32_t currentCmdBufferIndex = 0;
	//Descriptor set pool
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	// List of shader modules created (stored for cleanup)
	std::vector<VkShaderModule> shaderModules;
	//Pipeline cache object
	VkPipelineCache pipelineCache;
	VulkanSwapChain swapChain;

	//Synchronization semaphore
	struct  
	{
		VkSemaphore presentComplete;
		VkSemaphore renderComplete;
	} semaphores;

	std::vector<VkFence> waitFences;

public:
	bool prepared = false;
	bool resized = false;
	uint32_t width = 1280;
	uint32_t height = 720;

	vks::UIOverlay uiOverlay;
	CommandLineParser commandLineParser;

	//Last frame time measured using a high performance timer (if available)
	float frameTimer = 1.0f;

	vks::Benchmark benchmark;
	// Encapsulated physical and logical vulkan device
	vks::VulkanDevice * vulkanDevice;

	struct Setting
	{
		// Activates validation layers (and message output) when set to true
		bool validation = false;
		// Set to true if full screen mode has been requested via command line
		bool fullscreen = false;
		// Set to true if v-sync will be forced for the swapchain
		bool vsync = false;
		// Enable UI overlay
		bool overlay = true;
	} settings;

	VkClearColorValue defaultClearColor = { {0.025f,0.025f,0.025f,1.0f} };

	static std::vector<const char*>args;

	//Defines a frame rate independent timer value clamped from -1.0..1.0 for use in animations, rotations, etc.
	float timer = 0.0f;
	// Multiplier for speeding up (or slowing down) the global timer
	float timerSpeed = 0.25f;
	bool paused = false;

	Camera camera;
	glm::vec2 mousePos;

	std::string windowTitle = "Vulkan Example";
	std::string appName = "vulkanExample";
	uint32_t apiVersion = VK_API_VERSION_1_0;

	struct
	{
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
	}depthStencil;

	struct
	{
		glm::vec2 axisLeft = glm::vec2(0.0f);
		glm::vec2 axisRight = glm::vec2(0.0f);
	} gamePadState;

	struct  
	{
		bool left = false;
		bool right = false;
		bool middle = false;
	} mouseButtons;


	// OS specific
#if defined(_WIN32)
	HWND window;
	HINSTANCE windowInstance;
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	// true if application has focused, false if moved to background
	bool focused = false;
	struct TouchPos {
		int32_t x;
		int32_t y;
	} touchPos;
	bool touchDown = false;
	double touchTimer = 0.0;
	int64_t lastTapTime = 0;
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
	void* view;
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
	bool quit = false;
	IDirectFB *dfb = nullptr;
	IDirectFBDisplayLayer *layer = nullptr;
	IDirectFBWindow *window = nullptr;
	IDirectFBSurface *surface = nullptr;
	IDirectFBEventBuffer *event_buffer = nullptr;
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	wl_display *display = nullptr;
	wl_registry *registry = nullptr;
	wl_compositor *compositor = nullptr;
	struct xdg_wm_base *shell = nullptr;
	wl_seat *seat = nullptr;
	wl_pointer *pointer = nullptr;
	wl_keyboard *keyboard = nullptr;
	wl_surface *surface = nullptr;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *xdg_toplevel;
	bool quit = false;
	bool configured = false;

#elif defined(_DIRECT2DISPLAY)
	bool quit = false;
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	bool quit = false;
	xcb_connection_t *connection;
	xcb_screen_t *screen;
	xcb_window_t window;
	xcb_intern_atom_reply_t *atom_wm_delete_window;
#elif defined(VK_USE_PLATFORM_HEADLESS_EXT)
	bool quit = false;
#endif

	VulkanExampleBase(bool enableValidation = false);
	virtual ~VulkanExampleBase();
	bool initVulkanSetting();

#ifdef _WIN32
	void setupConsole(std::string title);
	void setupDPIAwareness();
	HWND setupWindow(HINSTANCE hinstance, WNDPROC wndproc);
	void handleMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
	static int32_t handleAppInput(struct android_app* app, AInputEvent* event);
	static void handleAppCommand(android_app* app, int32_t cmd);
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
	void* setupWindow(void* view);
	void displayLinkOutputCb();
	void mouseDragged(float x, float y);
	void windowWillResize(float x, float y);
	void windowDidResize();
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
	IDirectFBSurface *setupWindow();
	void handleEvent(const DFBWindowEvent *event);
#elif defined(VK_USE_PLATFORM_WAYLAND_KHR)
	struct xdg_surface *setupWindow();
	void initWaylandConnection();
	void setSize(int width, int height);
	static void registryGlobalCb(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version);
	void registryGlobal(struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version);
	static void registryGlobalRemoveCb(void *data, struct wl_registry *registry,
		uint32_t name);
	static void seatCapabilitiesCb(void *data, wl_seat *seat, uint32_t caps);
	void seatCapabilities(wl_seat *seat, uint32_t caps);
	static void pointerEnterCb(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface, wl_fixed_t sx,
		wl_fixed_t sy);
	static void pointerLeaveCb(void *data, struct wl_pointer *pointer,
		uint32_t serial, struct wl_surface *surface);
	static void pointerMotionCb(void *data, struct wl_pointer *pointer,
		uint32_t time, wl_fixed_t sx, wl_fixed_t sy);
	void pointerMotion(struct wl_pointer *pointer,
		uint32_t time, wl_fixed_t sx, wl_fixed_t sy);
	static void pointerButtonCb(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
	void pointerButton(struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
	static void pointerAxisCb(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value);
	void pointerAxis(struct wl_pointer *wl_pointer,
		uint32_t time, uint32_t axis, wl_fixed_t value);
	static void keyboardKeymapCb(void *data, struct wl_keyboard *keyboard,
		uint32_t format, int fd, uint32_t size);
	static void keyboardEnterCb(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, struct wl_surface *surface, struct wl_array *keys);
	static void keyboardLeaveCb(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, struct wl_surface *surface);
	static void keyboardKeyCb(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
	void keyboardKey(struct wl_keyboard *keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
	static void keyboardModifiersCb(void *data, struct wl_keyboard *keyboard,
		uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched,
		uint32_t mods_locked, uint32_t group);

#elif defined(_DIRECT2DISPLAY)
	//
#elif defined(VK_USE_PLATFORM_XCB_KHR)
	xcb_window_t setupWindow();
	void initxcbConnection();
	void handleEvent(const xcb_generic_event_t *event);
#else
	void setupWindow();
#endif

	// virtual Creates the application wide Vulkan instance
	virtual VkResult createInstance(bool enableValidation);

	// Pure virtual Render function to be implemented by the sample appication
	virtual void render() = 0;

	// Called when the camera view has changed
	virtual void viewChanged();

	//Called after a key was pressed,can be uesed to do custom key handling
	virtual void keyPressed(uint32_t);

	virtual void mouseMoved(double x, double y, bool &handled);

	virtual void windowResized();

	virtual void buildCommandBuffersForPreRenderPrmitives();

	virtual void setupDepthStencil();

	virtual void setupFrameBuffer();

	virtual void setupRenderPass();

	virtual void getEnabledFeatures();

	virtual void prepareForRendering();

	VkPipelineShaderStageCreateInfo loadShader(std::string fileName, VkShaderStageFlagBits stage);

	void renderLoop();

	bool drawUI(const VkCommandBuffer commandBuffer);

	void prepareFrame();

	void submitFrame();

	virtual void renderFrame();

	virtual void OnUpdateUIOverlay(vks::UIOverlay * overlay);
};//VulkanExampleBase

#if defined(_WIN32)
// Windows entry point
#define VULKAN_EXAMPLE_MAIN() \
VulkanExampleBase * vulkanExample; \
LRESULT CALLBACK WndProc(HWND hWnd,UINT uMsg, WPARAM wParam,LPARAM lParam) \
{\
	if (vulkanExample !=NULL) \
	{\
		vulkanExample->handleMessages(hWnd,uMsg,wParam,lParam);\
	}\
	return (DefWindowProc(hWnd,uMsg,wParam,lParam));\
}\
\
int APIENTRY WinMain(HINSTANCE hInstance,HINSTANCE,LPSTR,int) \
{\
	for (int32_t i = 0;i<__argc;++i)\
	{\
		VulkanExampleBase::args.push_back(__argv[i]);\
	};\
vulkanExample = new VulkanExample();\
vulkanExample->initVulkanSetting();\
vulkanExample->setupWindow(hInstance, WndProc); \
vulkanExample->prepareForRendering();\
vulkanExample->renderLoop();\
delete(vulkanExample);\
return 0;\
}

#elif defined(VK_USE_PLATFORM_ANDROID_KHR)
// Android entry point
#define VULKAN_EXAMPLE_MAIN()																		\
VulkanExample *vulkanExample;																		\
void android_main(android_app* state)																\
{																									\
	vulkanExample = new VulkanExample();															\
	state->userData = vulkanExample;																\
	state->onAppCmd = VulkanExample::handleAppCommand;												\
	state->onInputEvent = VulkanExample::handleAppInput;											\
	androidApp = state;																				\
	vks::android::getDeviceConfig();																\
	vulkanExample->renderLoop();																	\
	delete(vulkanExample);																			\
}
#elif defined(_DIRECT2DISPLAY)
// Linux entry point with direct to display wsi
#define VULKAN_EXAMPLE_MAIN()																		\
VulkanExample *vulkanExample;																		\
static void handleEvent()                                											\
{																									\
}																									\
int main(const int argc, const char *argv[])													    \
{																									\
	for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };  				\
	vulkanExample = new VulkanExample();															\
	vulkanExample->initVulkanSetting();																	\
	vulkanExample->prepareForRendering();																		\
	vulkanExample->renderLoop();																	\
	delete(vulkanExample);																			\
	return 0;																						\
}
#elif defined(VK_USE_PLATFORM_DIRECTFB_EXT)
#define VULKAN_EXAMPLE_MAIN()																		\
VulkanExample *vulkanExample;																		\
static void handleEvent(const DFBWindowEvent *event)												\
{																									\
	if (vulkanExample != NULL)																		\
	{																								\
		vulkanExample->handleEvent(event);															\
	}																								\
}																									\
int main(const int argc, const char *argv[])													    \
{																									\
	for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };  				\
	vulkanExample = new VulkanExample();															\
	vulkanExample->initVulkanSetting();																	\
	vulkanExample->setupWindow();					 												\
	vulkanExample->prepareForRendering();																		\
	vulkanExample->renderLoop();																	\
	delete(vulkanExample);																			\
	return 0;																						\
}
#elif (defined(VK_USE_PLATFORM_WAYLAND_KHR) || defined(VK_USE_PLATFORM_HEADLESS_EXT))
#define VULKAN_EXAMPLE_MAIN()																		\
VulkanExample *vulkanExample;																		\
int main(const int argc, const char *argv[])													    \
{																									\
	for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };  				\
	vulkanExample = new VulkanExample();															\
	vulkanExample->initVulkanSetting();																	\
	vulkanExample->setupWindow();					 												\
	vulkanExample->prepareForRendering();																		\
	vulkanExample->renderLoop();																	\
	delete(vulkanExample);																			\
	return 0;																						\
}
#elif defined(VK_USE_PLATFORM_XCB_KHR)
#define VULKAN_EXAMPLE_MAIN()																		\
VulkanExample *vulkanExample;																		\
static void handleEvent(const xcb_generic_event_t *event)											\
{																									\
	if (vulkanExample != NULL)																		\
	{																								\
		vulkanExample->handleEvent(event);															\
	}																								\
}																									\
int main(const int argc, const char *argv[])													    \
{																									\
	for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };  				\
	vulkanExample = new VulkanExample();															\
	vulkanExample->initVulkanSetting();																	\
	vulkanExample->setupWindow();					 												\
	vulkanExample->prepareForRendering();																		\
	vulkanExample->renderLoop();																	\
	delete(vulkanExample);																			\
	return 0;																						\
}
#elif (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
#if defined(VK_EXAMPLE_XCODE_GENERATED)
#define VULKAN_EXAMPLE_MAIN()																		\
	VulkanExample *vulkanExample;																		\
	int main(const int argc, const char *argv[])														\
	{																									\
		@autoreleasepool																				\
		{																								\
			for (size_t i = 0; i < argc; i++) { VulkanExample::args.push_back(argv[i]); };				\
			vulkanExample = new VulkanExample();														\
			vulkanExample->initVulkanSetting();																\
			vulkanExample->setupWindow(nullptr);														\
			vulkanExample->prepareForRendering();																	\
			vulkanExample->renderLoop();																\
			delete(vulkanExample);																		\
		}																								\
		return 0;																						\
	}
#else
#define VULKAN_EXAMPLE_MAIN()
#endif
#endif