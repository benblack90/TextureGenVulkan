/******************************************************************************
This file is part of the Newcastle Vulkan Tutorial Series

Author:Rich Davison
Contact:richgdavison@gmail.com
License: MIT (see LICENSE file at the top of the source tree)
*//////////////////////////////////////////////////////////////////////////////
#include "ComputeExample.h"

#include <random>
#include <thread>

using namespace NCL;
using namespace Rendering;
using namespace Vulkan;


ComputeExample::ComputeExample(Window& window)
	: VulkanTutorial(window), seed{ time(0) }, currentTex{ 0 }, frameNum{0}, msToCompute {0.0f}, msToEnd {0.0f}
{


	VulkanInitialisation vkInit = DefaultInitialisation();
	vkInit.autoBeginDynamicRendering = false;

	renderer = new VulkanRenderer(window, vkInit);
	InitTutorialObjects();
	quad = GenerateQuad();

	FrameState const& state = renderer->GetFrameState();
	vk::Device device = renderer->GetDevice();
	vk::DescriptorPool pool = renderer->GetDescriptorPool();

	// Create the query pool object used to get the GPU time stamps
	vk::QueryPoolCreateInfo qpInfo{};
	qpInfo.sType = vk::StructureType::eQueryPoolCreateInfo;	
	qpInfo.queryType = vk::QueryType::eTimestamp;
	qpInfo.queryCount = 3;
	device.createQueryPool(&qpInfo,nullptr, &timeStampQP);


	for (int i = 0; i < MAX_PLANETS; i++)
	{
		CreateNewPlanetDescrSets(i);
	}


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

void ComputeExample::Update(float dt)
{

	VulkanTutorial::Update(dt);

	if (Window::GetKeyboard()->KeyPressed(KeyCodes::RIGHT))
	{
		currentTex = (currentTex < planetDescr.size() - 1) ? currentTex + 1 : currentTex;
	}

	if (Window::GetKeyboard()->KeyPressed(KeyCodes::LEFT))
	{
		currentTex = (currentTex > 0) ? currentTex - 1 : currentTex;
	}
}

void ComputeExample::CreateNewPlanetDescrSets(int iteration)
{
	vk::Device device = renderer->GetDevice();
	vk::DescriptorPool pool = renderer->GetDescriptorPool();

	//InitTestConstVectors();
	InitConstantVectors();
	constVectorBuffer = BufferBuilder(renderer->GetDevice(), renderer->GetMemoryAllocator())
		.WithBufferUsage(vk::BufferUsageFlagBits::eStorageBuffer)
		.WithHostVisibility()
		.WithPersistentMapping()
		.Build(sizeof(Vector4) * NUM_PERMUTATIONS * 2, "Constant Vector Buffer");

	constVectorBuffer.CopyData(perms, sizeof(Vector4) * NUM_PERMUTATIONS * 2);

	//create descriptor set and descriptor set layout for the compute image
	imageDescrLayout[0] = DescriptorSetLayoutBuilder(device)
		.WithStorageImages(0, 1, vk::ShaderStageFlagBits::eCompute)
		.WithStorageBuffers(2, 1, vk::ShaderStageFlagBits::eCompute)
		.Build("Compute Data");
	imageDescrLayout[1] = DescriptorSetLayoutBuilder(device)
		.WithImageSamplers(1, 1, vk::ShaderStageFlagBits::eFragment)
		.Build("Raster version");
	planetDescr.push_back(CreateDescriptorSet(device, pool, *imageDescrLayout[0]));
	vertFragDescr.push_back(CreateDescriptorSet(device, pool, *imageDescrLayout[1]));

	//build the texture to be used by the compute shader, and then actually turn it into an ImageDescriptor
	TextureBuilder builder(renderer->GetDevice(), renderer->GetMemoryAllocator());
	builder.UsingPool(renderer->GetCommandPool(CommandBuffer::Graphics))
		.UsingQueue(renderer->GetQueue(CommandBuffer::Graphics))
		.WithDimension(hostWindow.GetScreenSize().x, hostWindow.GetScreenSize().y, 1)
		.WithMips(false)
		.WithUsages(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled)
		.WithLayout(vk::ImageLayout::eGeneral)
		.WithFormat(vk::Format::eB8G8R8A8Unorm);

	computeTextures[iteration] = builder.Build("compute RW texture");
	WriteStorageImageDescriptor(device, *planetDescr.back(), 0, *computeTextures[iteration], *defaultSampler, vk::ImageLayout::eGeneral);
	WriteBufferDescriptor(device, *planetDescr.back(), 2, vk::DescriptorType::eStorageBuffer, constVectorBuffer);
	WriteImageDescriptor(device, *vertFragDescr.back(), 1, *computeTextures[iteration], *defaultSampler, vk::ImageLayout::eGeneral);

}

void ComputeExample::RenderFrame(float dt) {
	FrameState const& frameState = renderer->GetFrameState();
	vk::Device device = renderer->GetDevice();
	vk::CommandBuffer cmdBuffer = frameState.cmdBuffer;
	cmdBuffer.resetQueryPool(timeStampQP,0,3);
	cmdBuffer.writeTimestamp(vk::PipelineStageFlagBits::eTopOfPipe, timeStampQP, 0);

	cmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, computePipeline);
	Vector3 positionUniform = { runTime, 0.0f, 0.0f };

	for (int i = 0; i < currentTex + 1; i++)
	{
		cmdBuffer.pushConstants(*computePipeline.layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(positionUniform), (void*)&positionUniform);
		cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *computePipeline.layout, 0, 1, &*planetDescr[i], 0, nullptr);
		cmdBuffer.dispatch(std::ceil(hostWindow.GetScreenSize().x / 16.0), std::ceil(hostWindow.GetScreenSize().y / 16.0), 1);
	}
	cmdBuffer.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, timeStampQP, 1);
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
	cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *basicPipeline.layout, 0, 1, &*vertFragDescr[currentTex], 0, nullptr);
	quad->Draw(cmdBuffer);
	cmdBuffer.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, timeStampQP, 2);

	device.getQueryPoolResults(timeStampQP,
		0,
		3,
		3 * sizeof(uint64_t),
		timeStamps,
		sizeof(uint64_t),
		vk::QueryResultFlagBits::e64
	);
	cmdBuffer.endRendering();

	PrintAverageTimestamps();
}

void ComputeExample::PrintAverageTimestamps()
{
	
	frameNum++;
	if (frameNum > 1000)
	{
		frameNum = 0;
		std::cout <<  currentTex << ','
			<< msToCompute / 1000.0f << ','
			<< msToEnd / 1000.0f << '\n';

		msToCompute = 0.0f;
		msToEnd = 0.0f;
		currentTex = (currentTex < planetDescr.size() - 1) ? currentTex + 1 : currentTex;
	}
	VkPhysicalDeviceLimits deviceLimits = renderer->GetPhysicalDevice().getProperties().limits;
	msToCompute += float(timeStamps[1] - timeStamps[0]) * deviceLimits.timestampPeriod / 1000000.0f;
	msToEnd += float(timeStamps[2] - timeStamps[1]) * deviceLimits.timestampPeriod / 1000000.0f;
}

void ComputeExample::InitConstantVectors()
{
	srand(seed++);
	for (int i = 0; i < NUM_PERMUTATIONS; i++)
	{
		perms[i].z = i;
		HashConstVecs(i);
	}
	for (int j = NUM_PERMUTATIONS - 1; j > 0; j--) {
		int index = std::round(rand() % (j));
		Vector4 temp = perms[j];
		perms[j] = perms[index];
		perms[index] = temp;

		perms[j + NUM_PERMUTATIONS] = perms[j];
		perms[index + NUM_PERMUTATIONS] = temp;
	}
}

void ComputeExample::HashConstVecs(int x)
{
	int hash = x % 6;
	switch (hash)
	{
	case 0:
		perms[x].x = 0.0f;
		perms[x].y = 1.4142f;
		break;
	case 1:
		perms[x].x = 0.0f;
		perms[x].y = -1.1412f;
		break;
	case 2:
		perms[x].x = 0.2455732529f;
		perms[x].y = 1.392715124f;
		break;
	case 3:
		perms[x].x = -0.2455732529f;
		perms[x].y = 1.392715124f;
		break;
	case 4:
		perms[x].x = 0.2455732529f;
		perms[x].y = -1.392715124f;
		break;
	case 5:
		perms[x].x = -0.2455732529f;
		perms[x].y = -1.392715124f;
		break;
	default:
		break;
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

	for (int i = 0; i < NUM_PERMUTATIONS; i++)
	{
		perms[i].z = i;
		HashConstVecs(i);
	}
}




