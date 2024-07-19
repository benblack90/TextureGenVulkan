/******************************************************************************
This file is part of the Newcastle Vulkan Tutorial Series

Author:Rich Davison
Contact:richgdavison@gmail.com
License: MIT (see LICENSE file at the top of the source tree)
*//////////////////////////////////////////////////////////////////////////////
#include "ComputeExample.h"

#include <random>

using namespace NCL;
using namespace Rendering;
using namespace Vulkan;


ComputeExample::ComputeExample(Window& window)
	: VulkanTutorial(window)
{
	VulkanInitialisation vkInit = DefaultInitialisation();
	vkInit.autoBeginDynamicRendering = false;

	renderer = new VulkanRenderer(window, vkInit);
	InitTutorialObjects();
	quad = GenerateQuad();

	FrameState const& state = renderer->GetFrameState();
	vk::Device device = renderer->GetDevice();
	vk::DescriptorPool pool = renderer->GetDescriptorPool();

	//InitTestConstVectors();
	InitConstantVectors();

	currentTex = 0;

	constVectorBuffer = BufferBuilder(renderer->GetDevice(), renderer->GetMemoryAllocator())
		.WithBufferUsage(vk::BufferUsageFlagBits::eStorageBuffer)
		.WithHostVisibility()
		.WithPersistentMapping()
		.Build(sizeof(int) * NUM_PERMUTATIONS * 2, "Constant Vector Buffer");

	constVectorBuffer.CopyData(perms, sizeof(int) * NUM_PERMUTATIONS * 2);

	//create descriptor set and descriptor set layout for the compute image
	imageDescrLayout[0] = DescriptorSetLayoutBuilder(device)
		.WithStorageImages(0, 1, vk::ShaderStageFlagBits::eCompute)
		.WithStorageBuffers(2, 1, vk::ShaderStageFlagBits::eCompute)
		.Build("Compute Data");
	imageDescrLayout[1] = DescriptorSetLayoutBuilder(device)
		.WithImageSamplers(1, 1, vk::ShaderStageFlagBits::eFragment)
		.Build("Raster version");

	planetDescr.push_back(CreateDescriptorSet(device, pool, *imageDescrLayout[0]));
	vertFragDescr = CreateDescriptorSet(device, pool, *imageDescrLayout[1]);

	//build the texture to be used by the compute shader, and then actually turn it into an ImageDescriptor
	TextureBuilder builder(renderer->GetDevice(), renderer->GetMemoryAllocator());
	builder.UsingPool(renderer->GetCommandPool(CommandBuffer::Graphics))
		.UsingQueue(renderer->GetQueue(CommandBuffer::Graphics))
		.WithDimension(hostWindow.GetScreenSize().x, hostWindow.GetScreenSize().y, 1)
		.WithMips(false)
		.WithUsages(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled)
		.WithLayout(vk::ImageLayout::eGeneral)
		.WithFormat(vk::Format::eB8G8R8A8Unorm);
	computeTexture = builder.Build("compute RW texture");
	WriteStorageImageDescriptor(device, *planetDescr[0], 0, *computeTexture, *defaultSampler, vk::ImageLayout::eGeneral);
	WriteBufferDescriptor(device, *planetDescr[0], 2, vk::DescriptorType::eStorageBuffer, constVectorBuffer);
	WriteImageDescriptor(device, *vertFragDescr, 1, *computeTexture, *defaultSampler, vk::ImageLayout::eGeneral);

	//build the compute shader, and attach the compute image descriptor to the pipeline
	computeShader = UniqueVulkanCompute(new VulkanCompute(device, "BasicCompute.comp.spv"));
	computePipeline = ComputePipelineBuilder(device)
		.WithShader(computeShader)
		.WithDescriptorSetLayout(0, *imageDescrLayout[0])
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
		.WithDescriptorSetLayout(0, *imageDescrLayout[1])
		.Build("Raster Pipeline");

}

void ComputeExample::RenderFrame(float dt) {
	FrameState const& frameState = renderer->GetFrameState();
	vk::CommandBuffer cmdBuffer = frameState.cmdBuffer;
	cmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
	Vector3 positionUniform = { runTime, 0.0f, 0.0f };
	cmdBuffer.pushConstants(*computePipeline.layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(positionUniform), (void*)&positionUniform);
	cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *computePipeline.layout, 0, 1, &*planetDescr[currentTex], 0, nullptr);

	cmdBuffer.dispatch(std::ceil(hostWindow.GetScreenSize().x / 16.0), std::ceil(hostWindow.GetScreenSize().y / 16.0), 1);

	cmdBuffer.pipelineBarrier(
		vk::PipelineStageFlagBits::eComputeShader,
		vk::PipelineStageFlagBits::eFragmentShader,
		vk::DependencyFlags(), 0, nullptr, 0, nullptr, 0, nullptr
	);


	cmdBuffer.beginRendering(
		DynamicRenderBuilder()
		.WithColourAttachment(frameState.colourView)
		.WithRenderArea(frameState.defaultScreenRect)
		.Build()
	);

	cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, basicPipeline);
	cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *basicPipeline.layout, 0, 1, &*vertFragDescr, 0, nullptr);
	quad->Draw(cmdBuffer);

	cmdBuffer.endRendering();
}

void ComputeExample::InitConstantVectors()
{
	srand(time(0));
	for (int i = 0; i < NUM_PERMUTATIONS; i++)
	{
		perms[i] = i;
	}
	for (int j = NUM_PERMUTATIONS - 1; j > 0; j--) {
		int index = std::round(rand() % (j));
		int temp = perms[j];
		perms[j] = perms[index];
		perms[index] = temp;

		perms[j + NUM_PERMUTATIONS] = perms[j];
		perms[index + NUM_PERMUTATIONS] = temp;
	}
}

//for testing purposes: sends across the same set of numbers to derive the constant vectors every time, so we can see what different effects do
void ComputeExample::InitTestConstVectors()
{
	int permCopy[512] =
	{
		2,55, 48, 70, 141, 196, 84, 51, 198, 46, 181, 149, 64, 102, 17, 57, 127, 15, 50, 212, 178,
		197, 138, 42, 244, 215, 236, 233, 225, 85, 26, 158, 71, 174, 228, 134, 235, 205, 21, 111, 67,
		192, 169, 145, 162, 214, 34, 190, 182, 95, 105, 106, 137, 186, 38, 109, 1, 41, 151, 159, 185,
		155, 254, 130, 118, 218, 114, 73, 250, 147, 75, 19, 40, 184, 93, 188, 247, 237, 62, 245, 49,
		76, 116, 104, 249, 119, 142, 171, 16, 144, 164, 10, 100, 202, 161, 187, 157, 32, 44, 253, 167,
		29, 132, 90, 92, 152, 59, 241, 146, 248, 226, 229, 154, 246, 25, 96, 173, 170, 168, 52, 78,
		43, 213, 113, 143, 177, 74, 86, 251, 27, 58, 28, 200, 122, 204, 60, 230, 24, 69, 8, 211,
		179, 129, 220, 193, 11, 125, 153, 133, 61, 108, 209, 91, 117, 79, 150, 45, 163, 176, 6, 242,
		189, 195, 20, 148, 166, 238, 68, 123, 33, 115, 23, 77, 199, 191, 22, 165, 135, 180, 219, 36,
		98, 223, 88, 126, 9, 12, 82, 227, 124, 160, 224, 239, 30, 89, 5, 121, 0, 66, 7, 110,
		107, 103, 210, 54, 65, 81, 194, 240, 203, 80, 37, 94, 87, 128, 99, 252, 112, 243, 172, 201,
		3, 18, 131, 140, 101, 206, 136, 97, 217, 231, 222, 156, 120, 255, 63, 56, 83, 234, 221, 13,
		183, 39, 14, 139, 53, 232, 72, 216, 35, 208, 31, 47, 207, 175, 4,
		2,55, 48, 70, 141, 196, 84, 51, 198, 46, 181, 149, 64, 102, 17, 57, 127, 15, 50, 212, 178,
		197, 138, 42, 244, 215, 236, 233, 225, 85, 26, 158, 71, 174, 228, 134, 235, 205, 21, 111, 67,
		192, 169, 145, 162, 214, 34, 190, 182, 95, 105, 106, 137, 186, 38, 109, 1, 41, 151, 159, 185,
		155, 254, 130, 118, 218, 114, 73, 250, 147, 75, 19, 40, 184, 93, 188, 247, 237, 62, 245, 49,
		76, 116, 104, 249, 119, 142, 171, 16, 144, 164, 10, 100, 202, 161, 187, 157, 32, 44, 253, 167,
		29, 132, 90, 92, 152, 59, 241, 146, 248, 226, 229, 154, 246, 25, 96, 173, 170, 168, 52, 78,
		43, 213, 113, 143, 177, 74, 86, 251, 27, 58, 28, 200, 122, 204, 60, 230, 24, 69, 8, 211,
		179, 129, 220, 193, 11, 125, 153, 133, 61, 108, 209, 91, 117, 79, 150, 45, 163, 176, 6, 242,
		189, 195, 20, 148, 166, 238, 68, 123, 33, 115, 23, 77, 199, 191, 22, 165, 135, 180, 219, 36,
		98, 223, 88, 126, 9, 12, 82, 227, 124, 160, 224, 239, 30, 89, 5, 121, 0, 66, 7, 110,
		107, 103, 210, 54, 65, 81, 194, 240, 203, 80, 37, 94, 87, 128, 99, 252, 112, 243, 172, 201,
		3, 18, 131, 140, 101, 206, 136, 97, 217, 231, 222, 156, 120, 255, 63, 56, 83, 234, 221, 13,
		183, 39, 14, 139, 53, 232, 72, 216, 35, 208, 31, 47, 207, 175, 4
	};

	memcpy(perms, permCopy, 512 * sizeof(int));
}




