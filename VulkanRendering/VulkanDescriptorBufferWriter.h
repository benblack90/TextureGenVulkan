/******************************************************************************
This file is part of the Newcastle Vulkan Tutorial Series

Author:Rich Davison
Contact:richgdavison@gmail.com
License: MIT (see LICENSE file at the top of the source tree)
*//////////////////////////////////////////////////////////////////////////////
#pragma once

namespace NCL::Rendering::Vulkan {
	/*
	
	
	*/
	class DescriptorBufferWriter {
	public:
		DescriptorBufferWriter(vk::Device inDevice, vk::DescriptorSetLayout inLayout, VulkanBuffer* inBuffer) {
			device = inDevice;
			destBuffer = inBuffer;
			layout = inLayout;
			descriptorBufferMemory = destBuffer->Map();
		}
		DescriptorBufferWriter(const VulkanBuffer& buffer) {
		}
		~DescriptorBufferWriter() {
			if (descriptorBufferMemory) {
				Finish();
			}
		}

		DescriptorBufferWriter& SetProperties(vk::PhysicalDeviceDescriptorBufferPropertiesEXT* inProps) {
			props = inProps;
			return *this;
		}

		DescriptorBufferWriter& WriteUniformBuffer(uint32_t binding, VulkanBuffer* uniformBuffer) {
			vk::DescriptorAddressInfoEXT descriptorAddress = {
				.address = uniformBuffer->deviceAddress,
				.range = uniformBuffer->size
			};

			vk::DescriptorGetInfoEXT getInfo = {
				.type = vk::DescriptorType::eUniformBuffer,
				.data = &descriptorAddress
			};

			vk::DeviceSize		offset = device.getDescriptorSetLayoutBindingOffsetEXT(layout, binding);

			device.getDescriptorEXT(&getInfo, props->uniformBufferDescriptorSize, ((char*)descriptorBufferMemory) + offset);

			return *this;
		}

		void Finish() {
			destBuffer->Unmap();
			descriptorBufferMemory = nullptr;
		}
	protected:
		vk::Device device;
		VulkanBuffer* destBuffer;
		void* descriptorBufferMemory;
		vk::DescriptorSetLayout layout;
		vk::PhysicalDeviceDescriptorBufferPropertiesEXT* props;

	};
};