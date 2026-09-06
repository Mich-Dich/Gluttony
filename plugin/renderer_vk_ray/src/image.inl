// #pragma once

// #include <vulkan/vulkan.h>
// #define STB_IMAGE_IMPLEMENTATION
// #include <stb_image.h>

// #include <plugin_system/i_renderer_plugin.h>


// FORWARD DECLARATIONS ================================================================================================

// namespace GLT::renderer_vk_ray {

//     // CONSTANTS =======================================================================================================

//     // MACROS ==========================================================================================================

//     // TYPES ===========================================================================================================

//     // STATIC VARIABLES ================================================================================================

//     // INTERNAL TEMPLATE DECLARATION ===================================================================================

//     // INTERNAL FUNCTION DECLARATION ===================================================================================

//     static u32 bytes_per_pixel(const GLT::render::image_format format);

//     static vk::Format image_format_to_vulkan_format(const GLT::render::image_format format);

//     // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

//     // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

//     static u32 bytes_per_pixel(const GLT::render::image_format format) {

//         switch (format) {
//             case GLT::render::image_format::RGBA:    return 4;
//             case GLT::render::image_format::RGBA32F: return 16;
//             default: return 0;
//         }
//     }


//     static vk::Format image_format_to_vulkan_format(const GLT::render::image_format format) {

//         switch (format) {
//             case GLT::render::image_format::RGBA:    return vk::Format::eR8G8B8A8Unorm;
//             case GLT::render::image_format::RGBA32F: return vk::Format::eR32G32B32A32Sfloat;
//             default:                    return vk::Format::eUndefined;
//         }
//     }

//     // TEMPLATE IMPLEMENTATION =========================================================================================

//     // TEMPLATE CLASS IMPLEMENTATION ===================================================================================

// 	image::image(const void* data, const glm::uvec3 size, const GLT::render::image_format format, const bool mipmapped) {

// 		allocate_memory(data, size, format, mipmapped);
// 	}


//     image::image(const void* data, const u32 width, const u32 height, const GLT::render::image_format format, bool mipmapped) {

// 		allocate_memory(data, const glm::uvec3{width, height, 1}, format, mipmapped);
// 	}


//     image::image(const std::filesystem::path& image_path, const GLT::render::image_format format, const bool mipmapped) {

// 		int channels;
// 		int width = 0, height = 0;
// 		void* data = stbi_load(image_path.string().c_str(), &width, &height, &channels, 4);
// 		VALIDATE(data != nullptr, return, "", "Could not load image from path [" << image_path.generic_string() << "]")
//         allocate_memory(data, glm::uvec3{width, height, 1}, format, mipmapped);
// 		stbi_image_free(data);
// 	}


// 	image::~image() {
		
// 		release();
// 	}

//     // TEMPLATE CLASS PUBLIC ===========================================================================================

// 	void* image::decode(const void* data, const u64 length, u32& out_width, u32& out_height) {

// 		int width, height, channels;
// 		u8* buffer = nullptr;
// 		u16 size = 0;

// 		buffer = stbi_load_from_memory((const stbi_uc*)data, (int)length, &width, &height, &channels, 4);
// 		size = width * height * 4;
// 		out_width = width;
// 		out_height = height;

// 		return buffer;
// 	}


// 	void* image::load(const std::filesystem::path& path, const u64 length, u32& out_width, u32& out_height) {

// 		// TODO: implement

// 		return nullptr;
// 	}


// 	void* image::get_descriptor_set() {

// 		if (m_descriptor_set == nullptr)
// 			m_descriptor_set = (vk::DescriptorSet)ImGui_ImplVulkan_AddTexture(
// 				static_cast<VkImageView>(m_image_view),
// 				static_cast<VkImageLayout>(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));

// 		return m_descriptor_set;
// 	}

//     // TEMPLATE CLASS PROTECTED ========================================================================================

//     // TEMPLATE CLASS PRIVATE ==========================================================================================

// 	vk::DescriptorSet image::generate_descriptor_set(const vk::Sampler sampler, const vk::ImageLayout layout) {

// 		return m_descriptor_set = (vk::DescriptorSet)ImGui_ImplVulkan_AddTexture(
// 			static_cast<VkImageView>(m_image_view),
// 			static_cast<VkImageLayout>(layout));
// 	}


// 	void image::allocate_memory(const void* data, const glm::uvec3 size, const GLT::render::image_format format, const bool mipmapped, const vk::ImageUsageFlags usage) {

//         m_renderer = GLT::plugin_manager::get_plugin_ref<GLT::render::i_renderer_plugin>(
//             GLT::plugin_manager::interface::renderer);

// 		size_t data_size = size.x * size.y * size.z * util::bytes_per_pixel(format);
// 		render::vulkan::vk_buffer uploadbuffer = m_renderer->create_buffer(data_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
// 		memcpy(uploadbuffer.info.pMappedData, data, data_size);
// 		allocate_image(size, util::image_format_to_vulkan_vormat(format), usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, mipmapped);

// 		m_renderer->immediate_submit([&](VkCommandBuffer cmd) {

// 			render::vulkan::util::transition_image(cmd, m_image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

// 			VkBufferImageCopy copyRegion = {};
// 			copyRegion.bufferOffset = 0;
// 			copyRegion.bufferRowLength = 0;
// 			copyRegion.bufferImageHeight = 0;
// 			copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
// 			copyRegion.imageSubresource.mipLevel = 0;
// 			copyRegion.imageSubresource.baseArrayLayer = 0;
// 			copyRegion.imageSubresource.layerCount = 1;
// 			copyRegion.imageExtent.width = size.x;
// 			copyRegion.imageExtent.height = size.y;
// 			copyRegion.imageExtent.depth = size.z;

// 			vkCmdCopyBufferToImage(cmd, uploadbuffer.buffer, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
// 			render::vulkan::util::transition_image(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
// 		});

// 		m_renderer->destroy_buffer(uploadbuffer);
// 		m_initialized = true;
// 	}


// 	void image::allocate_image(const glm::uvec3 size, const vk::Format format, const vk::ImageUsageFlags usage, const bool mipmapped) {

// 		m_allocated_image.width = size.x;
// 		m_allocated_image.height = size.y;
// 		m_allocated_image.size = size.z;

// 		vk::ImageCreateInfo img_info{};
//         img_info.pNext = nullptr;
//         img_info.imageType = vk::ImageType::e2D;
//         img_info.format = format;
//         img_info.extent.width = size.x;
//         img_info.extent.height = size.y;
//         img_info.extent.depth = size.z;
//         img_info.mipLevels = 1;
//         img_info.arrayLayers = 1;
//         img_info.samples = vk::SampleCountFlagBits::e1; // MSAA => will not be used by default, so default it to 1 sample per pixel
//         img_info.tiling = vk::ImageTiling::eOptimal;  	// optimal tiling, which means the image is stored on the best gpu format
//         img_info.usage = usage;

// 		if (mipmapped)
// 			img_info.mipLevels = static_cast<u32>(std::floor(std::log2(std::max(size.x, size.y)))) + 1;

// 		// always allocate images on dedicated GPU memory
// 		VmaAllocationCreateInfo allocinfo = {};
// 		allocinfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
// 		allocinfo.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
// 		VK_CHECK_S(vmaCreateImage(m_renderer->get_allocator(), &img_info, &allocinfo, &m_image, &m_allocation, nullptr));

// 		// if the format is a depth format, we will need to have it use the correct aspect flag
// 		vk::ImageAspectFlagBits aspect_flag = vk::ImageAspectFlagBits::eColor;
// 		if (format == vk::Format::eD32Sfloat)
// 			aspect_flag = vk::ImageAspectFlagBits::eDepth;

// 		// build a image-view for the image
//         vk::ImageViewCreateInfo view_info = {};
//         view_info.image = m_image;
//         view_info.viewType = vk::ImageViewType::e2D;
//         view_info.format = format;
//         view_info.subresourceRange.aspectMask = aspect_flag;
//         view_info.subresourceRange.baseMipLevel = 0;
//         view_info.subresourceRange.levelCount = 1;
//         view_info.subresourceRange.baseArrayLayer = 0;
//         view_info.subresourceRange.layerCount = img_info.mipLevels;
// 		view_info.subresourceRange.levelCount = img_info.mipLevels;
// 		// m_image_view = m_renderer->get_device().createImageView(view_info);

// 		m_initialized = true;
// 	}


// 	void image::release() {

// 		if (m_initialized) {

// 			m_renderer->submit_resource_free([
// 				image = m_image, 
// 				image_view = m_image_view, 
// 				allocation = m_allocated_image.allocation, 
// 				descriptor_set = m_descriptor_set] {

// 				vkDestroyImageView(m_renderer->get_device(), image_view, nullptr);
// 				vmaDestroyImage(m_renderer->get_allocator(), image, allocation);
// 				if (descriptor_set != nullptr)
// 					ImGui_ImplVulkan_RemoveTexture(descriptor_set);
// 			});

// 			m_image = {};
// 			m_image_view = {};
// 			m_allocated_image.allocation = nullptr;
// 			m_descriptor_set = {};
// 			m_initialized = false;
// 		}
// 	}

// }
