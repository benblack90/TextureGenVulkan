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
	quad = GenerateQuad();

	FrameState const& state = renderer->GetFrameState();
	vk::Device device = renderer->GetDevice();
	vk::DescriptorPool pool = renderer->GetDescriptorPool();

	//create descriptor set and descriptor set layout for the compute image
	imageDescrLayout = DescriptorSetLayoutBuilder(device)
		.WithStorageImages(0, 1, vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eFragment)
		.Build("Compute Data");

	imageDescriptor = CreateDescriptorSet(device, pool, *imageDescrLayout);

	//build the texture to be used by the compute shader, and then actually turn it into an ImageDescriptor
	TextureBuilder builder(renderer->GetDevice(), renderer->GetMemoryAllocator());
	builder.UsingPool(renderer->GetCommandPool(CommandBuffer::Graphics))
		.UsingQueue(renderer->GetQueue(CommandBuffer::Graphics))
		.WithDimension(hostWindow.GetScreenSize().x, hostWindow.GetScreenSize().y, 1)
		.WithMips(false)
		.WithUsages(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment)
		.WithLayout(vk::ImageLayout::eGeneral)
		.WithFormat(vk::Format::eB8G8R8A8Unorm);
	computeTexture = builder.Build("compute RW texture");
	WriteStorageImageDescriptor(device, *imageDescriptor, 0, *computeTexture, *defaultSampler, vk::ImageLayout::eGeneral);

	//build the compute shader, and attach the compute image descriptor to the pipeline
	computeShader = UniqueVulkanCompute(new VulkanCompute(device, "BasicCompute.comp.spv"));
	computePipeline = ComputePipelineBuilder(device)
		.WithShader(computeShader)
		.WithDescriptorSetLayout(0, *imageDescrLayout)
		.Build("Compute Pipeline");

	//build the raster shader, and attach the compute image descriptor to the pipeline
	rasterShader = ShaderBuilder(device)
		.WithVertexBinary("BasicCompute.vert.spv")
		.WithFragmentBinary("BasicCompute.frag.spv")
		.Build("Shader using compute data!");
	basicPipeline = PipelineBuilder(device)
		.WithVertexInputState(quad->GetVertexInputState())
		.WithTopology(vk::PrimitiveTopology::eTriangleStrip)
		.WithShader(rasterShader)
		.WithColourAttachment(state.colourFormat)
		.WithDescriptorSetLayout(0, *imageDescrLayout)
		.Build("Raster Pipeline");
	
}

void ComputeExample::RenderFrame(float dt) {
	FrameState const& frameState = renderer->GetFrameState();
	vk::CommandBuffer cmdBuffer = frameState.cmdBuffer;
	cmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
	cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *computePipeline.layout, 0, 1, &*imageDescriptor, 0, nullptr);

	cmdBuffer.dispatch(std::ceil(hostWindow.GetScreenSize().x /16.0), std::ceil(hostWindow.GetScreenSize().y / 16.0), 1);

	cmdBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eComputeShader, 
		vk::PipelineStageFlagBits::eAllCommands, 
		vk::DependencyFlags(), 0, nullptr, 0, nullptr, 0, nullptr
	);


	cmdBuffer.beginRendering(
		DynamicRenderBuilder()
			.WithColourAttachment(frameState.colourView)
			.WithRenderArea(frameState.defaultScreenRect)
			.Build()
	);

	cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, basicPipeline);
	cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *basicPipeline.layout, 0, 1, &*imageDescriptor, 0, nullptr);
	quad->Draw(cmdBuffer);

	cmdBuffer.endRendering();
}