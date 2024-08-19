/******************************************************************************
This file is part of the Newcastle Vulkan Tutorial Series

Author:Rich Davison
Contact:richgdavison@gmail.com
License: MIT (see LICENSE file at the top of the source tree)
*//////////////////////////////////////////////////////////////////////////////
#include "GasGiantTexGen.h"

#include <random>
#include <thread>

using namespace NCL;
using namespace Rendering;
using namespace Vulkan;


GasGiantTexGen::GasGiantTexGen(Window& window)
	: VulkanTutorial(window), seed{ time(0) }, currentTex{ 0 }, frameNum{0}, msToCompute {0.0f}, msToEnd {0.0f}, LoDIndex{0}, timedMode{false}
{
	VulkanInitialisation vkInit = DefaultInitialisation();
	vkInit.autoBeginDynamicRendering = false;

	renderer = new VulkanRenderer(window, vkInit);
	InitTutorialObjects();
	quad = GenerateQuad();
	InitLoDs();
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
	computeShader = UniqueVulkanCompute(new VulkanCompute(device, "GasGiantTex.comp.spv"));
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

void GasGiantTexGen::Update(float dt)
{

	VulkanTutorial::Update(dt);

	if (!timedMode)
	{
		if (Window::GetKeyboard()->KeyPressed(KeyCodes::RIGHT))
		{
			currentTex = (currentTex < planetDescr.size() - 1) ? currentTex + 1 : currentTex;
		}

		if (Window::GetKeyboard()->KeyPressed(KeyCodes::LEFT))
		{
			currentTex = (currentTex > 0) ? currentTex - 1 : currentTex;
		}

		if (Window::GetKeyboard()->KeyPressed(KeyCodes::MINUS))
		{
			LoDIndex = (LoDIndex < 3) ? LoDIndex + 1 : 0;
		}

		if (Window::GetKeyboard()->KeyPressed(KeyCodes::PLUS))
		{
			LoDIndex = (LoDIndex > 0) ? LoDIndex - 1 : 0;
		}
	}
	

	if (Window::GetKeyboard()->KeyPressed(KeyCodes::T))
	{
		timedMode = !timedMode;

		if (!timedMode)
		{
			std::cout << "Timed mode stopped\n";
		}

		if (timedMode)
		{
			currentTex = 0;
			frameNum = 0;
			msToCompute = 0.0f;
			msToEnd = 0.0f;
			std::cout << "Timed mode begun ... \n";
		}
	}
}

void GasGiantTexGen::InitLoDs()
{
	LoDs.push_back({ 5,5,5,4,4,4 });
	LoDs.push_back({ 3,3,3,2,2,2 });
	LoDs.push_back({ 1,1,1,1,1,1 });
}

void GasGiantTexGen::CreateNewPlanetDescrSets(int iteration)
{
	vk::Device device = renderer->GetDevice();
	vk::DescriptorPool pool = renderer->GetDescriptorPool();

	//InitTestConstVectors();
	InitConstantVectors();
	InitColourVars();
	constVectorBuffer = BufferBuilder(renderer->GetDevice(), renderer->GetMemoryAllocator())
		.WithBufferUsage(vk::BufferUsageFlagBits::eStorageBuffer)
		.WithHostVisibility()
		.WithPersistentMapping()
		.Build(sizeof(Vector4) * (NUM_PERMUTATIONS * 2 + 1), "Constant Vector Buffer");

	constVectorBuffer.CopyData(perms, sizeof(Vector4) * (NUM_PERMUTATIONS * 2 + 1));

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

void GasGiantTexGen::InitColourVars()
{
	Vector4 vars;
	float x = ((float)rand() / (RAND_MAX)) - 0.5f;
	//whether to add or subtract for bass warp
	vars.x = (x > 0) ? 1 : -1;
	//upper limit for bass colour smoothstep
	vars.y = 0.35 + ((float)rand() / (RAND_MAX) * 0.5);
	//adjustment for frequency
	vars.z = ((float)rand() / (RAND_MAX)) - 0.35f;

	perms[NUM_PERMUTATIONS * 2] = vars;
}

void GasGiantTexGen::RenderFrame(float dt) {
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
		cmdBuffer.pushConstants(*computePipeline.layout, vk::ShaderStageFlagBits::eCompute, sizeof(Vector3), sizeof(Vector4) * 2, (void*)&LoDs[LoDIndex]);
		cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *computePipeline.layout, 0, 1, &*planetDescr[i], 0, nullptr);
		cmdBuffer.dispatch(std::ceil(hostWindow.GetScreenSize().x / 16.0), std::ceil(hostWindow.GetScreenSize().y / 16.0), 1);
	}
	cmdBuffer.writeTimestamp(vk::PipelineStageFlagBits::eBottomOfPipe, timeStampQP, 1);
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

	if (timedMode)
	{
		PrintAverageTimestamps();
	}
}

void GasGiantTexGen::PrintAverageTimestamps()
{
	
	frameNum++;
	if (frameNum > 1000)
	{
		frameNum = 0;
		std::cout <<  currentTex + 1 << ','
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

void GasGiantTexGen::InitConstantVectors()
{
	srand(seed++);
	for (int i = 0; i < NUM_PERMUTATIONS; i++)
	{
		perms[i].z = i;
		HashConstVecs(i,i);
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

void GasGiantTexGen::HashConstVecs(int x, int ind)
{
	
	int hash = x % 6;
	switch (hash)
	{
	case 0:
		perms[ind].x = 0.0f;
		perms[ind].y = 1.4142f;
		break;
	case 1:
		perms[ind].x = 0.0f;
		perms[ind].y = -1.1412f;
		break;
	case 2:
		perms[ind].x = 0.2455732529f;
		perms[ind].y = 1.392715124f;
		break;
	case 3:
		perms[ind].x = -0.2455732529f;
		perms[ind].y = 1.392715124f;
		break;
	case 4:
		perms[ind].x = 0.2455732529f;
		perms[ind].y = -1.392715124f;
		break;
	case 5:
		perms[ind].x = -0.2455732529f;
		perms[ind].y = -1.392715124f;
		break;
	default:
		break;
	}
	


}

//for testing purposes: sends across the same set of numbers to derive the constant vectors every time, so we can see what different effects do
void GasGiantTexGen::InitTestConstVectors()
{
	int permCopy[512] =
	{
		21,177,105,37,157,239,156,251,80,48,70,60,127,3,234,96,173,65,122,194,144,115,107,158,167,126,135,44,19,94,24,147,25,
		118,79,88,187,183,72,30,64,199,145,91,214,216,230,86,205,218,226,246,140,164,143,42,181,76,223,58,104,195,201,162,16,
		252,49,191,4,12,207,32,209,190,152,34,237,203,179,202,103,244,50,175,198,200,114,82,233,26,186,225,117,102,185,255,248,
		36,238,35,78,85,165,0,1,38,242,7,108,41,153,163,106,227,228,112,178,100,184,245,116,87,171,224,241,66,138,53,129,13,182,
		253,148,250,155,55,113,111,52,196,46,51,240,172,217,176,67,101,133,74,68,5,62,210,161,221,90,61,134,28,63,84,154,47,31,9,
		222,150,170,180,130,6,121,14,146,206,40,128,57,249,10,23,99,236,20,15,189,43,92,215,169,160,125,247,211,192,136,22,2,168,
		75,11,151,204,33,29,232,123,109,27,17,54,73,56,254,69,213,93,81,97,231,141,120,188,98,71,110,219,235,89,166,208,193,142,
		45,159,18,243,229,212,197,137,59,139,131,95,39,220,77,119,83,174,8,132,149,124,
		21,177,105,37,157,239,156,251,80,48,70,60,127,3,234,96,173,65,122,194,144,115,107,158,167,126,135,44,19,94,24,147,25,
		118,79,88,187,183,72,30,64,199,145,91,214,216,230,86,205,218,226,246,140,164,143,42,181,76,223,58,104,195,201,162,16,
		252,49,191,4,12,207,32,209,190,152,34,237,203,179,202,103,244,50,175,198,200,114,82,233,26,186,225,117,102,185,255,248,
		36,238,35,78,85,165,0,1,38,242,7,108,41,153,163,106,227,228,112,178,100,184,245,116,87,171,224,241,66,138,53,129,13,182,
		253,148,250,155,55,113,111,52,196,46,51,240,172,217,176,67,101,133,74,68,5,62,210,161,221,90,61,134,28,63,84,154,47,31,9,
		222,150,170,180,130,6,121,14,146,206,40,128,57,249,10,23,99,236,20,15,189,43,92,215,169,160,125,247,211,192,136,22,2,168,
		75,11,151,204,33,29,232,123,109,27,17,54,73,56,254,69,213,93,81,97,231,141,120,188,98,71,110,219,235,89,166,208,193,142,
		45,159,18,243,229,212,197,137,59,139,131,95,39,220,77,119,83,174,8,132,149,124

	};

	for (int i = 0; i < NUM_PERMUTATIONS * 2; i++)
	{
		perms[i].z = permCopy[i];
		HashConstVecs(perms[i].z, i);
	}
}




