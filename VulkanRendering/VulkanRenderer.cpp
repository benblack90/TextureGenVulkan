/******************************************************************************
This file is part of the Newcastle Vulkan Tutorial Series

Author:Rich Davison
Contact:richgdavison@gmail.com
License: MIT (see LICENSE file at the top of the source tree)
*//////////////////////////////////////////////////////////////////////////////
#include "Vulkanrenderer.h"
#include "VulkanMesh.h"
#include "VulkanTexture.h"
#include "VulkanTextureBuilder.h"
#include "VulkanDescriptorSetLayoutBuilder.h"

#include "VulkanUtils.h"

#ifdef _WIN32
#include "Win32Window.h"
using namespace NCL::Win32Code;
#endif

#define VMA_IMPLEMENTATION
#include "vma/vk_mem_alloc.h"

using namespace NCL;
using namespace Rendering;
using namespace Vulkan;
VulkanRenderer::VulkanRenderer(Window& window, const VulkanInitialisation& vkInitInfo) : RendererBase(window)
{
	PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr = Vulkan::dynamicLoader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
	VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

	vkInit = vkInitInfo;

	allocatorInfo		= {};

	InitInstance(vkInit);

	InitPhysicalDevice(vkInit);

	InitGPUDevice(vkInit);
	InitMemoryAllocator(vkInit);

	InitCommandPools();
	InitDefaultDescriptorPool();
	InitDefaultDescriptorSetLayouts();

	hostWindow.SetRenderer(this);

	pipelineCache = device.createPipelineCache(vk::PipelineCacheCreateInfo());

	frameCmds = swapChainList[currentSwap]->cmdBuffer;
}

VulkanRenderer::~VulkanRenderer() {
	device.waitIdle();
	depthBuffer.reset();

	for (auto& i : swapChainList) {
		device.destroyImageView(i->colourView);
	};

	for (unsigned int i = 0; i < numFrameBuffers; ++i) {
		device.destroyFramebuffer(frameBuffers[i]);
		device.destroySemaphore(swapSemaphores[i]);
		device.destroyFence(swapFences[i]);
	}

	for (unsigned int i = 0; i < DefaultSetLayouts::MAX_SIZE; ++i) {
		if (defaultLayouts[i]) {
			device.destroyDescriptorSetLayout(defaultLayouts[i]);
		}
	}

	vmaDestroyAllocator(memoryAllocator);
	device.destroyDescriptorPool(defaultDescriptorPool);
	device.destroySwapchainKHR(swapChain);

	device.destroyCommandPool(commandPools[CommandBuffer::Graphics]);
	device.destroyCommandPool(commandPools[CommandBuffer::Copy]);
	device.destroyCommandPool(commandPools[CommandBuffer::AsyncCompute]);

	device.destroyRenderPass(defaultRenderPass);
	device.destroyPipelineCache(pipelineCache);
	device.destroy(); //Destroy everything except instance before this gets destroyed!

	instance.destroySurfaceKHR(surface);
	instance.destroy();

	delete[] frameBuffers;
}

bool VulkanRenderer::InitInstance(const VulkanInitialisation& vkInit) {
	vk::ApplicationInfo appInfo = {
		.pApplicationName = this->hostWindow.GetTitle().c_str(),
		.apiVersion = VK_MAKE_VERSION(vkInit.majorVersion, vkInit.minorVersion, 0)
	};
		
	vk::InstanceCreateInfo instanceInfo = {
		.flags = {},
		.pApplicationInfo = &appInfo,
		.enabledLayerCount = (uint32_t)vkInit.instanceLayers.size(),
		.ppEnabledLayerNames = vkInit.instanceLayers.data(),
		.enabledExtensionCount = (uint32_t)vkInit.instanceExtensions.size(),
		.ppEnabledExtensionNames = vkInit.instanceExtensions.data()
	};
		
	instance = vk::createInstance(instanceInfo);

	VULKAN_HPP_DEFAULT_DISPATCHER.init(instance);

	return true;
}

bool	VulkanRenderer::InitPhysicalDevice(const VulkanInitialisation& vkInit) {
	auto enumResult = instance.enumeratePhysicalDevices();

	if (enumResult.empty()) {
		return false; //Guess there's no Vulkan capable devices?!
	}

	gpu = enumResult[0];
	for (auto& i : enumResult) {
		if (i.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
			gpu = i; //Prefer a discrete GPU on multi device machines like laptops
		}
	}

	std::cout << __FUNCTION__ << " Vulkan using physical device " << gpu.getProperties().deviceName << "\n";

	vk::PhysicalDeviceProperties2 props;
	gpu.getProperties2(&props);

	return true;
}

bool VulkanRenderer::InitGPUDevice(const VulkanInitialisation& vkInit) {
	InitSurface();
	InitDeviceQueueIndices();

	float queuePriority = 0.0f;

	std::vector< vk::DeviceQueueCreateInfo> queueInfos;

	queueInfos.emplace_back(vk::DeviceQueueCreateInfo()
		.setQueueCount(1)
		.setQueueFamilyIndex(gfxQueueIndex)
		.setPQueuePriorities(&queuePriority)
	);

	if (computeQueueIndex != gfxQueueIndex){
		queueInfos.emplace_back(vk::DeviceQueueCreateInfo()
			.setQueueCount(1)
			.setQueueFamilyIndex(computeQueueIndex)
			.setPQueuePriorities(&queuePriority)
		);
	}
	if (copyQueueIndex != gfxQueueIndex) {
		queueInfos.emplace_back(vk::DeviceQueueCreateInfo()
			.setQueueCount(1)
			.setQueueFamilyIndex(copyQueueIndex)
			.setPQueuePriorities(&queuePriority)
		);
	}

	vk::PhysicalDeviceFeatures2 deviceFeatures;
	gpu.getFeatures2(&deviceFeatures);

	if (!vkInit.features.empty()) {
		for (int i = 1; i < vkInit.features.size(); ++i) {
			vk::PhysicalDeviceFeatures2* prevStruct = (vk::PhysicalDeviceFeatures2*)vkInit.features[i-1];
			prevStruct->pNext = vkInit.features[i];
		}
		deviceFeatures.pNext = vkInit.features[0];
	}

	vk::DeviceCreateInfo createInfo = vk::DeviceCreateInfo()
		.setQueueCreateInfoCount(queueInfos.size())
		.setPQueueCreateInfos(queueInfos.data());
	
	createInfo.setEnabledLayerCount((uint32_t)vkInit.deviceLayers.size())
		.setPpEnabledLayerNames(vkInit.deviceLayers.data())
		.setEnabledExtensionCount((uint32_t)vkInit.deviceExtensions.size())
		.setPpEnabledExtensionNames(vkInit.deviceExtensions.data());

	createInfo.pNext = &deviceFeatures;

	device = gpu.createDevice(createInfo);

	queueTypes[CommandBuffer::Graphics]		= device.getQueue(gfxQueueIndex, 0);
	queueTypes[CommandBuffer::AsyncCompute]	= device.getQueue(computeQueueIndex, 0);
	queueTypes[CommandBuffer::Copy]			= device.getQueue(copyQueueIndex, 0);
	presentQueue	= device.getQueue(gfxPresentIndex, 0);

	deviceMemoryProperties = gpu.getMemoryProperties();
	deviceProperties = gpu.getProperties();

	VULKAN_HPP_DEFAULT_DISPATCHER.init(device);
	return true;
}

bool VulkanRenderer::InitSurface() {
#ifdef _WIN32
	Win32Window* window = (Win32Window*)&hostWindow;

	surface = instance.createWin32SurfaceKHR(
		{
			.flags = {},
			.hinstance = window->GetInstance(),
			.hwnd = window->GetHandle()
		}
	);
#endif

	auto formats = gpu.getSurfaceFormatsKHR(surface);

	if (formats.size() == 1 && formats[0].format == vk::Format::eUndefined) {
		surfaceFormat	= vk::Format::eB8G8R8A8Unorm;
		surfaceSpace	= formats[0].colorSpace;
	}
	else {
		surfaceFormat	= formats[0].format;
		surfaceSpace	= formats[0].colorSpace;
	}

	return formats.size() > 0;
}

uint32_t VulkanRenderer::InitBufferChain(vk::CommandBuffer  cmdBuffer) {
	vk::SwapchainKHR oldChain					= swapChain;
	std::vector<FrameState*> oldSwapChainList	= swapChainList;
	swapChainList.clear();

	vk::SurfaceCapabilitiesKHR surfaceCaps = gpu.getSurfaceCapabilitiesKHR(surface);

	vk::Extent2D swapExtents = vk::Extent2D((int)hostWindow.GetScreenSize().x, (int)hostWindow.GetScreenSize().y);

	auto presentModes = gpu.getSurfacePresentModesKHR(surface); //Type is of vector of PresentModeKHR

	vk::PresentModeKHR idealPresentMode = vk::PresentModeKHR::eFifo;

	for (const auto& i : presentModes) {
		if (i == vk::PresentModeKHR::eMailbox) {
			idealPresentMode = i;
			break;
		}
		else if (i == vk::PresentModeKHR::eImmediate) {
			idealPresentMode = vk::PresentModeKHR::eImmediate; //Might still become mailbox...
		}
	}

	vk::SurfaceTransformFlagBitsKHR idealTransform;

	if (surfaceCaps.supportedTransforms & vk::SurfaceTransformFlagBitsKHR::eIdentity) {
		idealTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
	}
	else {
		idealTransform = surfaceCaps.currentTransform;
	}

	int idealImageCount = surfaceCaps.minImageCount + 1;
	if (surfaceCaps.maxImageCount > 0) {
		idealImageCount = std::min(idealImageCount, (int)surfaceCaps.maxImageCount);
	}

	vk::SwapchainCreateInfoKHR swapInfo;

	swapInfo.setPresentMode(idealPresentMode)
		.setPreTransform(idealTransform)
		.setSurface(surface)
		.setImageColorSpace(surfaceSpace)
		.setImageFormat(surfaceFormat)
		.setImageExtent(swapExtents)
		.setMinImageCount(idealImageCount)
		.setOldSwapchain(oldChain)
		.setImageArrayLayers(1)
		.setImageUsage(vk::ImageUsageFlagBits::eColorAttachment);

	swapChain = device.createSwapchainKHR(swapInfo);

	if (!oldSwapChainList.empty()) {
		for (unsigned int i = 0; i < numFrameBuffers; ++i) {
			device.destroyImageView(oldSwapChainList[i]->colourView);
			delete oldSwapChainList[i];
		}
	}
	if (oldChain) {
		device.destroySwapchainKHR(oldChain);
	}

	auto images = device.getSwapchainImagesKHR(swapChain);

	for (auto& i : images) {
		FrameState* chain = new FrameState();

		chain->colourImage = i;

		ImageTransitionBarrier(cmdBuffer, i, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor, vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::PipelineStageFlagBits2::eColorAttachmentOutput);

		chain->colourView = device.createImageView(
			vk::ImageViewCreateInfo()
			.setSubresourceRange(vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1))
			.setFormat(surfaceFormat)
			.setImage(i)
			.setViewType(vk::ImageViewType::e2D)	
		);

		swapChainList.push_back(chain);

		auto buffers = device.allocateCommandBuffers(	
			{
				.commandPool = commandPools[CommandBuffer::Graphics],
				.level = vk::CommandBufferLevel::ePrimary,
				.commandBufferCount = 1
			}
		);
		 
		chain->cmdBuffer = buffers[0];

		swapSemaphores.push_back(device.createSemaphore({}));
		swapFences.push_back(device.createFence({}));

		chain->acquireSempaphore = swapSemaphores[swapCycle];
		chain->acquireFence		 = swapFences[swapCycle];

		chain->defaultViewport		= defaultViewport;
		chain->defaultScissor		= defaultScissor;
		chain->defaultScreenRect	= defaultScreenRect;
		
		chain->colourFormat = surfaceFormat;

		chain->depthImage	= depthBuffer->GetImage();
		chain->depthView	= depthBuffer->GetDefaultView();
		chain->depthFormat	= depthBuffer->GetFormat();
	}
	swapCycle = 1;
	return (int)images.size();
}

void	VulkanRenderer::InitCommandPools() {	
	commandPools[CommandBuffer::Graphics] = device.createCommandPool(
		{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			.queueFamilyIndex = gfxQueueIndex
		}
	);

	commandPools[CommandBuffer::AsyncCompute] = device.createCommandPool(
		{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			.queueFamilyIndex = computeQueueIndex
		}
	);

	commandPools[CommandBuffer::Copy] = device.createCommandPool(
		{
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			.queueFamilyIndex = copyQueueIndex
		}	
	);
}

void	VulkanRenderer::InitMemoryAllocator(const VulkanInitialisation& vkInit) {
	VmaVulkanFunctions funcs = {};
	funcs.vkGetInstanceProcAddr = ::vk::defaultDispatchLoaderDynamic.vkGetInstanceProcAddr;
	funcs.vkGetDeviceProcAddr   = ::vk::defaultDispatchLoaderDynamic.vkGetDeviceProcAddr;

	allocatorInfo.physicalDevice = gpu;
	allocatorInfo.device	= device;
	allocatorInfo.instance	= instance;

	allocatorInfo.flags |= vkInit.vmaFlags;

	allocatorInfo.pVulkanFunctions = &funcs;
	vmaCreateAllocator(&allocatorInfo, &memoryAllocator);
}

bool VulkanRenderer::InitDeviceQueueIndices() {
	deviceQueueProps = gpu.getQueueFamilyProperties();

	VkBool32 supportsPresent = false;
	gfxQueueIndex		= -1;
	computeQueueIndex	= -1;
	copyQueueIndex		= -1;
	gfxPresentIndex		= -1;

	int gfxBits		= INT_MAX;
	int computeBits = INT_MAX;
	int copyBits	= INT_MAX;

	for (unsigned int i = 0; i < deviceQueueProps.size(); ++i) {
		supportsPresent = gpu.getSurfaceSupportKHR(i, surface);

		int queueBitCount = std::popcount((uint32_t)deviceQueueProps[i].queueFlags);

		if (deviceQueueProps[i].queueFlags & vk::QueueFlagBits::eGraphics && queueBitCount < gfxBits) {
			gfxQueueIndex = i;
			gfxBits = queueBitCount;
			if (supportsPresent && gfxPresentIndex == -1) {
				gfxPresentIndex = i;
			}
		}

		if (deviceQueueProps[i].queueFlags & vk::QueueFlagBits::eCompute && queueBitCount < computeBits) {
			computeQueueIndex = i;
			computeBits = queueBitCount;
		}

		if (deviceQueueProps[i].queueFlags & vk::QueueFlagBits::eTransfer && queueBitCount < copyBits) {
			copyQueueIndex = i;
			copyBits = queueBitCount;
		}
	}

	if (computeQueueIndex == -1) {
		computeQueueIndex = gfxQueueIndex;
	}
	else {
		std::cout << __FUNCTION__ << " Device supports async compute!\n";
	}

	if (copyQueueIndex == -1) {
		copyQueueIndex = gfxQueueIndex;
	}
	else {
		std::cout << __FUNCTION__ << " Device supports async copy!\n";
	}

	if (gfxQueueIndex == -1		|| 
		gfxPresentIndex == -1   || 
		computeQueueIndex == -1 ||
		copyQueueIndex == -1) {
		return false;
	}

	return true;
}

void VulkanRenderer::OnWindowResize(int width, int height) {
	if (!hostWindow.IsMinimised() && width == windowSize.x && height == windowSize.y) {
		return;
	}
	if (width == 0 && height == 0) {
		return;
	}
	windowSize = { width, height };

	defaultScreenRect = vk::Rect2D({ 0,0 }, { (uint32_t)windowSize.x, (uint32_t)windowSize.y });

	if (vkInit.useOpenGLCoordinates) {
		defaultViewport = vk::Viewport(0.0f, (float)windowSize.y, (float)windowSize.x, (float)windowSize.y, -1.0f, 1.0f);
	}
	else {
		defaultViewport = vk::Viewport(0.0f, (float)windowSize.y, (float)windowSize.x, -(float)windowSize.y, 0.0f, 1.0f);
	}

	defaultScissor	= vk::Rect2D(vk::Offset2D(0, 0), vk::Extent2D(windowSize.x, windowSize.y));

	defaultClearValues[0] = vk::ClearValue(vk::ClearColorValue(std::array<float, 4>{0.2f, 0.2f, 0.2f, 1.0f}));
	defaultClearValues[1] = vk::ClearValue(vk::ClearDepthStencilValue(1.0f, 0));

	std::cout << __FUNCTION__ << " New dimensions: " << windowSize.x << " , " << windowSize.y << "\n";

	device.waitIdle();

	depthBuffer = TextureBuilder(GetDevice(), GetMemoryAllocator())
		.UsingPool(GetCommandPool(CommandBuffer::Graphics))
		.UsingQueue(GetQueue(CommandBuffer::Graphics))
		.WithDimension(hostWindow.GetScreenSize().x, hostWindow.GetScreenSize().y)
		.WithAspects(vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil)
		.WithFormat(vkInit.depthStencilFormat)
		.WithLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
		.WithUsages(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled)
		.WithPipeFlags(vk::PipelineStageFlagBits2::eEarlyFragmentTests)
		.WithMips(false)
		.Build("Depth Buffer");

	vk::UniqueCommandBuffer cmds = CmdBufferBegin(device, commandPools[CommandBuffer::Graphics], "Window resize cmds");
	numFrameBuffers = InitBufferChain(*cmds);

	InitDefaultRenderPass();
	CreateDefaultFrameBuffers();

	device.waitIdle();

	CompleteResize();

	CmdBufferEndSubmitWait(*cmds, device, queueTypes[CommandBuffer::Graphics]);
}

void VulkanRenderer::CompleteResize() {

}

void VulkanRenderer::WaitForSwapImage() {
	TransitionUndefinedToColour(frameCmds, swapChainList[currentSwap]->colourImage);

	if (!hostWindow.IsMinimised()) {
		vk::Result waitResult = device.waitForFences(swapChainList[currentSwap]->acquireFence, true, ~0);
	}
}

void	VulkanRenderer::AcquireSwapImage() {
	device.resetFences(swapChainList[currentSwap]->acquireFence);

	currentSwap = device.acquireNextImageKHR(swapChain, UINT64_MAX, swapSemaphores[swapCycle], swapFences[swapCycle]).value;	//Get swap image

	swapChainList[currentSwap]->acquireSempaphore = swapSemaphores[swapCycle];
	swapChainList[currentSwap]->acquireFence = swapFences[swapCycle];

	swapChainList[currentSwap]->defaultViewport		= defaultViewport;
	swapChainList[currentSwap]->defaultScissor		= defaultScissor;
	swapChainList[currentSwap]->defaultScreenRect	= defaultScreenRect;

	swapChainList[currentSwap]->colourFormat = surfaceFormat;
	swapChainList[currentSwap]->depthFormat  = depthBuffer->GetFormat();

	swapCycle = (swapCycle + 1) % swapSemaphores.size();

	defaultBeginInfo = vk::RenderPassBeginInfo()
		.setRenderPass(defaultRenderPass)
		.setFramebuffer(frameBuffers[currentSwap])
		.setRenderArea(defaultScissor)
		.setClearValueCount(sizeof(defaultClearValues) / sizeof(vk::ClearValue))
		.setPClearValues(defaultClearValues);
}

void	VulkanRenderer::BeginFrame() {
	AcquireSwapImage();
	frameCmds = swapChainList[currentSwap]->cmdBuffer;
	frameCmds.reset({});

	frameCmds.begin(vk::CommandBufferBeginInfo());

	if (!vkInit.skipDynamicState) {
		frameCmds.setViewport(0, 1, &defaultViewport);
		frameCmds.setScissor(0, 1, &defaultScissor);
	}

	if (vkInit.autoTransitionFrameBuffer) {
		WaitForSwapImage();
	}
	if (vkInit.autoBeginDynamicRendering) {
		BeginDefaultRendering(frameCmds);
	}
}

void VulkanRenderer::RenderFrame() {

}

void	VulkanRenderer::EndFrame() {
	if (vkInit.autoBeginDynamicRendering) {
		frameCmds.endRendering();
	}

	if (hostWindow.IsMinimised()) {
		CmdBufferEndSubmitWait(frameCmds, device, queueTypes[CommandBuffer::Graphics]);
	}
	else {
		CmdBufferEndSubmit(frameCmds, queueTypes[CommandBuffer::Graphics]);
	}
}

void VulkanRenderer::SwapBuffers() {
	if (!hostWindow.IsMinimised()) {
		vk::CommandPool gfxPool		= commandPools[CommandBuffer::Graphics];
		vk::Queue		gfxQueue	= queueTypes[CommandBuffer::Graphics];

		vk::UniqueCommandBuffer cmds = CmdBufferBegin(device, gfxPool, "Window swap cmds");

		TransitionColourToPresent(*cmds, swapChainList[currentSwap]->colourImage);

		CmdBufferEndSubmitWait(*cmds, device, gfxQueue);

		vk::Result presentResult = gfxQueue.presentKHR(
			{
				.waitSemaphoreCount = 1,
				.pWaitSemaphores = &swapChainList[currentSwap]->acquireSempaphore,
				.swapchainCount = 1,
				.pSwapchains = &swapChain,
				.pImageIndices = &currentSwap
			}
		);	
	}
}

void	VulkanRenderer::InitDefaultRenderPass() {
	if (defaultRenderPass) {
		device.destroyRenderPass(defaultRenderPass);
	}
	vk::AttachmentDescription attachments[] = {
		vk::AttachmentDescription()
			.setInitialLayout(vk::ImageLayout::eColorAttachmentOptimal)
			.setFinalLayout(vk::ImageLayout::eColorAttachmentOptimal)
			.setFormat(surfaceFormat)
			.setLoadOp(vk::AttachmentLoadOp::eClear)
			.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
			.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
	,
		vk::AttachmentDescription()
			.setInitialLayout(vk::ImageLayout::eUndefined)
			.setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal)
			.setFormat(depthBuffer->GetFormat())
			.setLoadOp(vk::AttachmentLoadOp::eClear)
			.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)
			.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
	};

	vk::AttachmentReference references[] = {
		vk::AttachmentReference(0, vk::ImageLayout::eColorAttachmentOptimal),
		vk::AttachmentReference(1, vk::ImageLayout::eDepthStencilAttachmentOptimal)
	};

	vk::SubpassDescription subPass = vk::SubpassDescription()
		.setColorAttachmentCount(1)
		.setPColorAttachments(&references[0])
		.setPDepthStencilAttachment(&references[1])
		.setPipelineBindPoint(vk::PipelineBindPoint::eGraphics);

	vk::RenderPassCreateInfo renderPassInfo = vk::RenderPassCreateInfo()
		.setAttachmentCount(2)
		.setPAttachments(attachments)
		.setSubpassCount(1)
		.setPSubpasses(&subPass);

	defaultRenderPass = device.createRenderPass(renderPassInfo);
}

bool VulkanRenderer::CreateDefaultFrameBuffers() {
	if (frameBuffers) {
		for (unsigned int i = 0; i < numFrameBuffers; ++i) {
			device.destroyFramebuffer(frameBuffers[i]);
		}
	}
	else {
		frameBuffers = new vk::Framebuffer[numFrameBuffers];
	}

	vk::ImageView attachments[2];
	
	vk::FramebufferCreateInfo createInfo = vk::FramebufferCreateInfo()
		.setWidth(hostWindow.GetScreenSize().x)
		.setHeight(hostWindow.GetScreenSize().y)
		.setLayers(1)
		.setAttachmentCount(2)
		.setPAttachments(attachments)
		.setRenderPass(defaultRenderPass);

	for (uint32_t i = 0; i < numFrameBuffers; ++i) {
		attachments[0]	= swapChainList[i]->colourView;
		attachments[1]	= *depthBuffer->defaultView;
		frameBuffers[i] = device.createFramebuffer(createInfo);
	}

	defaultBeginInfo = vk::RenderPassBeginInfo()
		.setRenderPass(defaultRenderPass)
		.setFramebuffer(frameBuffers[currentSwap])
		.setRenderArea(defaultScissor)
		.setClearValueCount(sizeof(defaultClearValues) / sizeof(vk::ClearValue))
		.setPClearValues(defaultClearValues);

	return true;
}

void	VulkanRenderer::InitDefaultDescriptorPool(uint32_t maxSets) {
	vk::DescriptorPoolSize poolSizes[] = {
		vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, maxSets),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, maxSets),
		vk::DescriptorPoolSize(vk::DescriptorType::eUniformBufferDynamic, maxSets),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageBufferDynamic, maxSets),		
		
		vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, maxSets),
		vk::DescriptorPoolSize(vk::DescriptorType::eSampledImage, maxSets),
		vk::DescriptorPoolSize(vk::DescriptorType::eStorageImage, maxSets),
#ifdef USE_RAY_TRACING
		vk::DescriptorPoolSize(vk::DescriptorType::eAccelerationStructureKHR, maxSets),
#endif
	};

	uint32_t poolCount = sizeof(poolSizes) / sizeof(vk::DescriptorPoolSize);

	vk::DescriptorPoolCreateInfo poolCreate;
	poolCreate.setPoolSizeCount(sizeof(poolSizes) / sizeof(vk::DescriptorPoolSize));
	poolCreate.setPPoolSizes(poolSizes);
	poolCreate.setMaxSets(maxSets * poolCount);
	poolCreate.setFlags(vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet);

	defaultDescriptorPool = device.createDescriptorPool(poolCreate);
}

void VulkanRenderer::InitDefaultDescriptorSetLayouts() {
	defaultLayouts[DefaultSetLayouts::Single_Texture] = DescriptorSetLayoutBuilder(GetDevice())
		.WithImageSamplers(0, 1, vk::ShaderStageFlagBits::eAll)
		.Build("Default Single Texture Layout").release();

	defaultLayouts[DefaultSetLayouts::Single_UBO] = DescriptorSetLayoutBuilder(GetDevice())
		.WithUniformBuffers(0, 1, vk::ShaderStageFlagBits::eAll)
		.Build("Default Single UBO Layout").release();

	defaultLayouts[DefaultSetLayouts::Single_SSBO] = DescriptorSetLayoutBuilder(GetDevice())
		.WithStorageBuffers(0, 1, vk::ShaderStageFlagBits::eAll)
		.Build("Default Single SSBO Layout").release();

	defaultLayouts[DefaultSetLayouts::Single_Storage_Image] = DescriptorSetLayoutBuilder(GetDevice())
		.WithStorageImages(0, 1, vk::ShaderStageFlagBits::eAll)
		.Build("Default Single Storage Image Layout").release();

	//defaultLayouts[InbuiltDescriptorSetLayouts::Single_TLAS] = DescriptorSetLayoutBuilder(GetDevice())
	//	.WithSamplers(1, vk::ShaderStageFlagBits::eAll)
	//	.Build("Default Single Texture Layout").release();
}

void	VulkanRenderer::BeginDefaultRenderPass(vk::CommandBuffer  cmds) {
	cmds.beginRenderPass(defaultBeginInfo, vk::SubpassContents::eInline);
	//cmds.setViewport(0, 1, &defaultViewport);
	//cmds.setScissor(0, 1, &defaultScissor);
}

void	VulkanRenderer::BeginDefaultRendering(vk::CommandBuffer  cmds) {
	vk::RenderingInfoKHR renderInfo;
	renderInfo.layerCount = 1;

	vk::RenderingAttachmentInfoKHR colourAttachment;
	colourAttachment.setImageView(swapChainList[currentSwap]->colourView)
		.setImageLayout(vk::ImageLayout::eColorAttachmentOptimal)
		.setLoadOp(vk::AttachmentLoadOp::eClear)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setClearValue(vk::ClearColorValue(0.2f, 0.2f, 0.2f, 1.0f));

	vk::RenderingAttachmentInfoKHR depthAttachment;
	depthAttachment.setImageView(depthBuffer->GetDefaultView())
		.setImageLayout(vk::ImageLayout::eDepthAttachmentOptimal)
		.setLoadOp(vk::AttachmentLoadOp::eClear)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.clearValue.setDepthStencil({ 1.0f, ~0U });

	renderInfo.setColorAttachments(colourAttachment)
		.setPDepthAttachment(&depthAttachment);
		//.setPStencilAttachment(&depthAttachment);

	renderInfo.setRenderArea(defaultScreenRect);

	cmds.beginRendering(renderInfo);
	cmds.setViewport(0, 1, &defaultViewport);
	cmds.setScissor(0, 1, &defaultScissor);
}