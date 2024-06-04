/******************************************************************************
This file is part of the Newcastle Vulkan Tutorial Series

Author:Rich Davison
Contact:richgdavison@gmail.com
License: MIT (see LICENSE file at the top of the source tree)
*//////////////////////////////////////////////////////////////////////////////
#pragma once
#include "VulkanTutorial.h"

namespace NCL::Rendering::Vulkan {
	class ComputeExample : public VulkanTutorial {
	public:
		ComputeExample(Window& window);
		~ComputeExample() {}
	protected:

		void RenderFrame(float dt) override;
		void InitNoiseVals();
		
		UniqueVulkanShader	rasterShader;
		UniqueVulkanCompute	computeShader;
		UniqueVulkanTexture computeTexture;
		UniqueVulkanMesh quad;

		VulkanPipeline	basicPipeline;
		VulkanPipeline	computePipeline;

		const Vector2 possGradients[4];
		int perms[512];
		vk::UniqueDescriptorSet	imageDescriptor[2];
		vk::UniqueDescriptorSetLayout	imageDescrLayout[2];
	};
}