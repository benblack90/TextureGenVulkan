/******************************************************************************
This file is part of the Newcastle Vulkan Tutorial Series

Author:Rich Davison
Contact:richgdavison@gmail.com
License: MIT (see LICENSE file at the top of the source tree)
*//////////////////////////////////////////////////////////////////////////////
#include "ComputeExample.h"

using namespace NCL;
using namespace Rendering;
using namespace Vulkan;

const int PARTICLE_COUNT = 32 * 100;

ComputeExample::ComputeExample(Window& window) : VulkanTutorial(window)	{
	VulkanInitialisation vkInit = DefaultInitialisation();
	vkInit.	autoBeginDynamicRendering = false;

	renderer = new VulkanRenderer(window, vkInit);
	InitTutorialObjects();

	FrameState const& state = renderer->GetFrameState();
	vk::Device device = renderer->GetDevice();
	vk::DescriptorPool pool = renderer->GetDescriptorPool();

	imageDescrLayout = DescriptorSetLayoutBuilder(device)
		.WithStorageImages(0, 1, vk::ShaderStageFlagBits::eCompute)
		.Build("Compute Data");

	imageDescriptor = CreateDescriptorSet(device, pool, *imageDescrLayout);
	WriteStorageImageDescriptor(device, *imageDescriptor, 0, *computeTexture, *defaultSampler, vk::ImageLayout::eGeneral);

	computeShader = UniqueVulkanCompute(new VulkanCompute(device, "BasicCompute.comp.spv"));

	computePipeline = ComputePipelineBuilder(device)
		.WithShader(computeShader)
		.WithDescriptorSetLayout(0, *imageDescrLayout)
		.Build("Compute Pipeline");
}

void ComputeExample::RenderFrame(float dt) {
	FrameState const& frameState = renderer->GetFrameState();
	vk::CommandBuffer cmdBuffer = frameState.cmdBuffer;

	cmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
	cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *computePipeline.layout, 0, 1, &*bufferDescriptor, 0, nullptr);
	cmdBuffer.pushConstants(*computePipeline.layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(float), (void*)&runTime);

	cmdBuffer.dispatch(PARTICLE_COUNT / 32, 1, 1);

	cmdBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eComputeShader, 
		vk::PipelineStageFlagBits::eVertexShader, 
		vk::DependencyFlags(), 0, nullptr, 0, nullptr, 0, nullptr
	);

	cmdBuffer.beginRendering(
		DynamicRenderBuilder()
			.WithColourAttachment(frameState.colourView)
			.WithRenderArea(frameState.defaultScreenRect)
			.Build()
	);

	cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, basicPipeline);
	cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *basicPipeline.layout, 0, 1, &*bufferDescriptor, 0, nullptr);
	cmdBuffer.draw(PARTICLE_COUNT, 1, 0, 0);

	cmdBuffer.endRendering();
}