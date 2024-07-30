/******************************************************************************
This file is part of the Newcastle Vulkan Tutorial Series

Author:Rich Davison
Contact:richgdavison@gmail.com
License: MIT (see LICENSE file at the top of the source tree)
*//////////////////////////////////////////////////////////////////////////////
#pragma once
#include "VulkanTutorial.h"

namespace NCL::Rendering::Vulkan {

	constexpr int NUM_PERMUTATIONS = 256;
	constexpr int MAX_PLANETS = 100;

	class ComputeExample : public VulkanTutorial {
	public:
		ComputeExample(Window& window);
		void Update(float dt) override;
		~ComputeExample() {}
		
	protected:
		void RenderFrame(float dt) override ;
		void InitConstantVectors();
		void InitTestConstVectors();
		void CreateNewPlanetDescrSets();

		UniqueVulkanShader	rasterShader;
		UniqueVulkanCompute	computeShader;
		UniqueVulkanMesh quad;
		VulkanBuffer constVectorBuffer;


		VulkanPipeline	basicPipeline;
		VulkanPipeline	computePipeline;

		int perms[NUM_PERMUTATIONS * 2];
		time_t seed;
		int currentTex;
		std::vector<vk::UniqueDescriptorSet> planetDescr;
		std::vector<vk::UniqueDescriptorSet> vertFragDescr;
		vk::UniqueDescriptorSetLayout	imageDescrLayout[2];
	};
}