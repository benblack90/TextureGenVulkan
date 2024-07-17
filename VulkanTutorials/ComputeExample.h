/******************************************************************************
This file is part of the Newcastle Vulkan Tutorial Series

Author:Rich Davison
Contact:richgdavison@gmail.com
License: MIT (see LICENSE file at the top of the source tree)
*//////////////////////////////////////////////////////////////////////////////
#pragma once
#include "VulkanTutorial.h"

namespace NCL::Rendering::Vulkan {

	constexpr int NUM_PERMUTATIONS = 512;

	class ComputeExample : public VulkanTutorial {
	public:
		ComputeExample(Window& window);
		~ComputeExample() {}
		
	protected:
		void RenderFrame(float dt) override;
		void InitConstantVectors();
		void InitTestConstVectors();

		UniqueVulkanShader	rasterShader;
		UniqueVulkanCompute	computeShader;
		UniqueVulkanTexture computeTexture;
		UniqueVulkanMesh quad;
		VulkanBuffer constVectorBuffer;

		VulkanPipeline	basicPipeline;
		VulkanPipeline	computePipeline;

		int perms[NUM_PERMUTATIONS * 2];
		int currentTex;
		std::vector<vk::UniqueDescriptorSet> planetDescr;
		vk::UniqueDescriptorSet vertFragDescr;
		vk::UniqueDescriptorSetLayout	imageDescrLayout[2];
	};
}