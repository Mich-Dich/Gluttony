
#pragma once

#include <glm/glm.hpp>
#include <imgui.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::launcher {

    // CLASS DECLARATION ===============================================================================================

    // Owns an OpenGL 2D texture plus CPU-side metadata. Move-only (owns a GPU resource). Returns an ImTextureID ready for ImGui::Image()
    class image {
    public:

        image() = default;
        ~image();

        DELETE_COPY_CONSTRUCTOR(image)
        image(image&& other) noexcept;
        image& operator=(image&& other) noexcept;

        // Load from disk (PNG/JPG/TGA/BMP/HDR/PSD/GIF via stb_image)
        bool load_from_file(const std::filesystem::path& path);

        // Load from an in-memory buffer (PNG/JPG/etc.)
        bool load_from_memory(const void* data, size_t size);

        // Destroy the GL texture. Idempotent
        void release();

        bool is_valid() const { return m_texture != 0; }
        ImTextureID tex_id() const { return (ImTextureID)(intptr_t)m_texture; }
        glm::ivec2 size() const { return m_size; }
        int width() const { return m_size.x; }
        int height() const { return m_size.y; }
        int channels() const { return m_channels; }

    private:

        bool upload(const unsigned char* pixels, int w, int h, int channels);

        unsigned int                                m_texture = 0;
        glm::ivec2                                  m_size = {0, 0};
        int                                         m_channels = 0;

    };

}
