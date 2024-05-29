/******************************************************************************
This file is part of the Newcastle Vulkan Tutorial Series

Author:Rich Davison
Contact:richgdavison@gmail.com
License: MIT (see LICENSE file at the top of the source tree)
*//////////////////////////////////////////////////////////////////////////////
#pragma once
#include "../NCLCoreClasses/RendererBase.h"
#include "../NCLCoreClasses/Maths.h"

#include "VulkanPipeline.h"
#include "SmartTypes.h"
#include "vma/vk_mem_alloc.h"
using std::string;

namespace NCL::Rendering::Vulkan {
	class VulkanMesh;
	class VulkanShader;
	class VulkanCompute;
	class VulkanTexture;
	struct VulkanBuffer;

	namespace CommandBuffer {
		enum Type : uint32_t {
			Graphics,
			AsyncCompute,
			Copy,
			MAX_BUFFERS
		};
	};
	//Some auto-generated descriptor set layouts for quick prototyping
	struct DefaultSetLayouts {
		enum Type : uint32_t {
			Single_Texture,
			Single_UBO,
			Single_SSBO,
			Single_Storage_Image,
			Single_TLAS,
			MAX_SIZE
		};
	};

	struct FrameState {		
		vk::CommandBuffer	cmdBuffer;

		vk::Semaphore		acquireSempaphore;
		vk::Fence			acquireFence;

		vk::Image			colourImage;
		vk::ImageView		colourView;
		vk::Format			colourFormat;

		vk::Image			depthImage;
		vk::ImageView		depthView;
		vk::Format			depthFormat;

		vk::Viewport		defaultViewport;
		vk::Rect2D			defaultScissor;
		vk::Rect2D			defaultScreenRect;
	};

	struct VulkanInitialisation {
		vk::Format			depthStencilFormat = vk::Format::eD32SfloatS8Uint;

		int majorVersion = 1;
		int minorVersion = 1;

		std::vector<void*> features;

		VmaAllocatorCreateFlags vmaFlags = {};

		std::vector<const char*>	instanceExtensions;
		std::vector<const char*>	instanceLayers;

		std::vector<const char*> deviceExtensions;
		std::vector<const char*> deviceLayers;

		bool				autoTransitionFrameBuffer = true;
		bool				autoBeginDynamicRendering = true;
		bool				useOpenGLCoordinates = false;
		bool				skipDynamicState = false;
	};

	class VulkanRenderer : public RendererBase {
		friend class VulkanMesh;
		friend class VulkanTexture;
	public:
		VulkanRenderer(Window& window, const VulkanInitialisation& vkInit);
		~VulkanRenderer();

		virtual bool HasInitialised() const { return device; }

		vk::Device GetDevice() const {
			return device;
		}

		vk::PhysicalDevice GetPhysicalDevice() const {
			return gpu;
		}

		VmaAllocator GetMemoryAllocator() const {
			return memoryAllocator;
		}

		vk::Queue GetQueue(CommandBuffer::Type type) const {
			return queueTypes[type];
		}

		vk::CommandPool GetCommandPool(CommandBuffer::Type type) const {
			return commandPools[type];
		}

		vk::DescriptorPool GetDescriptorPool() {
			return defaultDescriptorPool;
		}

		FrameState const& GetFrameState() const {
			return *(swapChainList[currentSwap]);
		}

		vk::DescriptorSetLayout GetDefaultLayout(DefaultSetLayouts::Type layout) {
			return defaultLayouts[layout];
		}

		UniqueVulkanTexture const & GetDepthBuffer() const {
			return depthBuffer;
		}

		void	BeginDefaultRenderPass(vk::CommandBuffer cmds);
		void	BeginDefaultRendering(vk::CommandBuffer  cmds);

		void BeginFrame()		override;
		void RenderFrame()		override;
		void EndFrame()			override;
		void SwapBuffers()		override;

	protected:
		void OnWindowResize(int w, int h)	override;

		virtual void	CompleteResize();
		virtual void	InitDefaultRenderPass();
		virtual void	InitDefaultDescriptorPool(uint32_t maxSets = 128);

		virtual void WaitForSwapImage();


	protected:
		vk::DescriptorSetLayout defaultLayouts[DefaultSetLayouts::MAX_SIZE];		
		vk::ClearValue			defaultClearValues[2];
		vk::Viewport			defaultViewport;
		vk::Rect2D				defaultScissor;	
		vk::Rect2D				defaultScreenRect;			
		vk::RenderPass			defaultRenderPass;
		vk::RenderPassBeginInfo defaultBeginInfo;
		
		vk::DescriptorPool		defaultDescriptorPool;	//descriptor sets come from here!

		vk::CommandPool			commandPools[CommandBuffer::Type::MAX_BUFFERS];
		vk::Queue				queueTypes[CommandBuffer::Type::MAX_BUFFERS];

		vk::CommandBuffer		frameCmds;

		//Initialisation Info
		std::vector<vk::QueueFamilyProperties> deviceQueueProps;

		UniqueVulkanTexture depthBuffer;

		VmaAllocatorCreateInfo	allocatorInfo;

		VulkanInitialisation vkInit;
	private: 
		void	InitCommandPools();
		bool	InitInstance(const VulkanInitialisation& vkInit);
		bool	InitPhysicalDevice(const VulkanInitialisation& vkInit);
		bool	InitGPUDevice(const VulkanInitialisation& vkInit);
		bool	InitSurface();
		void	InitMemoryAllocator(const VulkanInitialisation& vkInit);
		uint32_t	InitBufferChain(vk::CommandBuffer  cmdBuffer);

		bool	InitDeviceQueueIndices();
		bool	CreateDefaultFrameBuffers();

		void	AcquireSwapImage();

		void InitDefaultDescriptorSetLayouts();

		vk::Instance		instance;	//API Instance
		vk::PhysicalDevice	gpu;		//GPU in use

		vk::PhysicalDeviceProperties		deviceProperties;
		vk::PhysicalDeviceMemoryProperties	deviceMemoryProperties;

		vk::PipelineCache		pipelineCache;
		vk::Device				device;		//Device handle	

		vk::SurfaceKHR		surface;
		vk::Format			surfaceFormat;
		vk::ColorSpaceKHR	surfaceSpace;

		vk::Queue			presentQueue;

		uint32_t			gfxQueueIndex			= 0;
		uint32_t			computeQueueIndex		= 0;
		uint32_t			copyQueueIndex			= 0;
		uint32_t			gfxPresentIndex			= 0;
		uint32_t			numFrameBuffers			= 0;

		std::vector<FrameState*> swapChainList;
		uint32_t				currentSwap = 0;
		uint32_t				swapCycle = 0;
		vk::Framebuffer* frameBuffers = nullptr;

		std::vector<vk::Semaphore>	swapSemaphores;
		std::vector<vk::Fence>		swapFences;

		vk::SwapchainKHR	swapChain;
		VmaAllocator		memoryAllocator;
	};
}