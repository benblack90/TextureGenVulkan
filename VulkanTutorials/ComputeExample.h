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

	class ComputeExample : public VulkanTutorial {
	public:
		enum KeyComms 
		{
			NEW,
			NEXT,
			PREV
		};

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
		int currentTex;
		std::vector<vk::UniqueDescriptorSet> planetDescr;
		std::vector<vk::UniqueDescriptorSet> vertFragDescr;
		vk::UniqueDescriptorSetLayout	imageDescrLayout[2];

		std::vector<KeyComms> pendCommands;
	};
}