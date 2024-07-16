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

	imageDescriptor[0] = CreateDescriptorSet(device, pool, *imageDescrLayout[0]);
	imageDescriptor[1] = CreateDescriptorSet(device, pool, *imageDescrLayout[1]);

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
	WriteStorageImageDescriptor(device, *imageDescriptor[0], 0, *computeTexture, *defaultSampler, vk::ImageLayout::eGeneral);
	WriteBufferDescriptor(device, *imageDescriptor[0], 2, vk::DescriptorType::eStorageBuffer, constVectorBuffer);
	WriteImageDescriptor(device, *imageDescriptor[1], 1, *computeTexture, *defaultSampler, vk::ImageLayout::eGeneral);

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
	cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *computePipeline.layout, 0, 1, &*imageDescriptor[0], 0, nullptr);

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
	cmdBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *basicPipeline.layout, 0, 1, &*imageDescriptor[1], 0, nullptr);
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
	int permCopy[1024] = 
	{ 
		62,157,283,193,344,123,140,495,72,113,408,199,216,353,379,227,35,402,302,118,8,70,281,235,23,233,86,386,463,108,354,226,3,
		240,10,406,7,333,132,15,206,13,269,12,487,60,31,117,120,389,447,433,20,403,280,326,242,100,257,387,2,144,497,503,399,243,
		133,485,372,394,210,124,472,459,323,454,267,484,145,347,363,290,509,304,128,349,401,149,209,166,475,36,460,38,84,42,382,
		357,369,236,334,143,448,158,94,80,18,14,470,91,63,1,154,92,501,225,285,392,119,27,95,221,490,224,335,61,244,19,203,173,
		294,165,234,125,168,229,449,368,325,85,346,314,68,341,64,445,196,360,26,268,256,137,321,6,292,248,103,46,477,482,93,32,182,
		507,211,436,476,231,74,276,59,146,499,511,48,355,178,66,306,189,161,5,313,78,474,82,54,441,217,415,192,439,232,41,24,412,
		493,337,303,410,90,310,277,315,57,320,297,116,183,350,319,126,340,422,468,170,328,365,151,291,49,273,213,186,332,421,312,
		330,413,374,88,83,69,373,101,396,134,424,264,171,204,148,176,375,141,361,443,457,471,21,366,214,450,52,309,55,198,308,245,
		405,491,43,356,112,187,348,253,383,481,222,388,418,180,311,249,498,265,376,39,371,429,169,50,423,17,465,307,147,510,202,
		390,478,47,425,162,102,246,150,359,367,207,65,461,300,37,29,342,289,194,417,219,77,384,241,352,58,247,223,331,453,254,30,
		239,318,434,237,129,76,502,411,163,139,452,212,44,286,444,385,505,127,160,259,416,4,0,260,426,109,504,191,469,230,500,489,
		258,261,420,464,279,181,45,218,345,351,483,138,67,427,131,262,130,115,251,156,358,317,220,339,89,111,414,435,496,343,172,
		153,284,152,398,480,370,53,506,122,87,188,215,271,298,201,272,121,185,301,430,9,96,338,200,381,404,316,105,329,135,99,473,
		295,252,397,288,377,51,33,282,362,275,274,324,255,81,195,327,380,299,174,197,400,432,175,155,190,407,428,456,391,336,40,
		208,205,250,296,107,104,395,159,438,458,479,446,110,393,25,98,16,437,378,431,293,263,177,466,462,486,364,164,228,287,167,
		488,419,305,409,238,266,136,440,73,278,97,142,11,75,451,442,508,114,56,79,179,106,270,467,455,184,34,494,28,22,71,322,492,
		62,157,283,193,344,123,140,495,72,113,408,199,216,353,379,227,35,402,302,118,8,70,281,235,23,233,86,386,463,108,354,226,3,
		240,10,406,7,333,132,15,206,13,269,12,487,60,31,117,120,389,447,433,20,403,280,326,242,100,257,387,2,144,497,503,399,243,
		133,485,372,394,210,124,472,459,323,454,267,484,145,347,363,290,509,304,128,349,401,149,209,166,475,36,460,38,84,42,382,357,
		369,236,334,143,448,158,94,80,18,14,470,91,63,1,154,92,501,225,285,392,119,27,95,221,490,224,335,61,244,19,203,173,294,
		165,234,125,168,229,449,368,325,85,346,314,68,341,64,445,196,360,26,268,256,137,321,6,292,248,103,46,477,482,93,32,182,507,
		211,436,476,231,74,276,59,146,499,511,48,355,178,66,306,189,161,5,313,78,474,82,54,441,217,415,192,439,232,41,24,412,493,
		337,303,410,90,310,277,315,57,320,297,116,183,350,319,126,340,422,468,170,328,365,151,291,49,273,213,186,332,421,312,330,413,
		374,88,83,69,373,101,396,134,424,264,171,204,148,176,375,141,361,443,457,471,21,366,214,450,52,309,55,198,308,245,405,491,
		43,356,112,187,348,253,383,481,222,388,418,180,311,249,498,265,376,39,371,429,169,50,423,17,465,307,147,510,202,390,478,47,
		425,162,102,246,150,359,367,207,65,461,300,37,29,342,289,194,417,219,77,384,241,352,58,247,223,331,453,254,30,239,318,434,
		237,129,76,502,411,163,139,452,212,44,286,444,385,505,127,160,259,416,4,0,260,426,109,504,191,469,230,500,489,258,261,420,
		464,279,181,45,218,345,351,483,138,67,427,131,262,130,115,251,156,358,317,220,339,89,111,414,435,496,343,172,153,284,152,
		398,480,370,53,506,122,87,188,215,271,298,201,272,121,185,301,430,9,96,338,200,381,404,316,105,329,135,99,473,295,252,397,
		288,377,51,33,282,362,275,274,324,255,81,195,327,380,299,174,197,400,432,175,155,190,407,428,456,391,336,40,208,205,250,296,
		107,104,395,159,438,458,479,446,110,393,25,98,16,437,378,431,293,263,177,466,462,486,364,164,228,287,167,488,419,305,409,
		238,266,136,440,73,278,97,142,11,75,451,442,508,114,56,79,179,106,270,467,455,184,34,494,28,22,71,322,492
	};

	memcpy(perms, permCopy, 1024 * sizeof(int));
}




