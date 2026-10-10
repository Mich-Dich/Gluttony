
#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::renderer::vk_rasterizer::graph {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Handles  (opaque 32-bit IDs; cheap to copy, easy to compare) ----------------------------------------------------

    struct texture_handle {

        u32                                                     id = UINT32_MAX;
        bool is_valid() const { return id != UINT32_MAX; }
        bool operator==(const texture_handle& o) const;
    };


    struct buffer_handle {

        u32                                                     id = UINT32_MAX;
        bool is_valid() const { return id != UINT32_MAX; }
        bool operator==(const buffer_handle& o) const;
    };

    // Usages  (what a pass does with a resource) ----------------------------------------------------------------------

    enum class texture_usage : u8 {

        sampled,             // read in a shader via texture()
        storage_read,        // read via imageLoad
        storage_readwrite,   // read+write via imageLoad/Store
        color_attachment,    // written as a colour target
        depth_sampled,       // depth-aspect view read in a shader (sampler2D)
        depth_attachment,    // written as a depth target
        depth_readonly,      // depth test, no write
        transfer_src,
        transfer_dst,
        present,
    };


    enum class buffer_usage : u8 {

        uniform_read,
        storage_read,
        storage_readwrite,
        transfer_src,
        transfer_dst,
        vertex,
        index,
        indirect,
    };

    // Resource state  (Sync2 representation) --------------------------------------------------------------------------

    struct resource_state {

        vk::ImageLayout                                         layout = vk::ImageLayout::eUndefined;
        vk::AccessFlags2                                        access = {};
        vk::PipelineStageFlags2                                 stage  = vk::PipelineStageFlagBits2::eNone;

        bool operator==(const resource_state& o) const;
    };

    // Descriptors -----------------------------------------------------------------------------------------------------

    struct texture_desc {

        std::string                                             name;
        vk::Format                                              format = vk::Format::eUndefined;
        u32                                                     width = 0;
        u32                                                     height = 0;
        u32                                                     depth = 1;
        u32                                                     mip_levels = 1;
        u32                                                     array_layers = 1;
        vk::SampleCountFlagBits                                 samples = vk::SampleCountFlagBits::e1;
        vk::ImageUsageFlags                                     usage = {};      // auto-derived if empty
        // persistent resources survive end_frame(); their handle stays stable across frames (lookup by name)
        bool                                                    persistent = false;
    };


    struct buffer_desc {

        std::string                                             name;
        vk::DeviceSize                                          size = 0;
        vk::BufferUsageFlags                                    usage = {};
        VmaAllocationCreateFlags                                vma_flags = 0;
        bool                                                    persistent = false;
    };

    // pass_builder  (used inside the setup lambda) --------------------------------------------------------------------

    struct attachment_info {

        texture_handle                                          handle;
        vk::AttachmentLoadOp                                    load_op  = vk::AttachmentLoadOp::eClear;
        vk::AttachmentStoreOp                                   store_op = vk::AttachmentStoreOp::eStore;
        vk::ClearValue                                          clear_value = {};
    };

    // STATIC VARIABLES ================================================================================================

    // FUNCTION DECLARATION ============================================================================================

    // Convenience: turn a usage into the state a pass requires
    resource_state derive_state(texture_usage u);


    resource_state derive_state(buffer_usage  u);


    // Merge two usage-derived states into one. Layout takes the "more general/more writable" of the two; access and stage are unioned
    resource_state merge(texture_usage a, texture_usage b);

    // TEMPLATE DECLARATION ============================================================================================

    // CLASS DECLARATION ===============================================================================================

}
