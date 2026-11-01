
#include "util/pch.h"
#include "image.h"

#include <GLFW/glfw3.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::launcher {

    // CLASS IMPLEMENTATION ============================================================================================

    image::~image() { release(); }


    image::image(image&& other) noexcept
        : m_texture(other.m_texture), m_size(other.m_size), m_channels(other.m_channels) {

        other.m_texture = 0;
        other.m_size = {0, 0};
        other.m_channels = 0;
    }


    image& image::operator=(image&& other) noexcept {

        if (this != &other) {
            release();
            m_texture = other.m_texture;
            m_size = other.m_size;
            m_channels = other.m_channels;
            other.m_texture = 0;
            other.m_size = {0, 0};
            other.m_channels = 0;
        }
        return *this;
    }

    // CLASS PUBLIC ====================================================================================================

    bool image::load_from_file(const std::filesystem::path& path) {

        int w = 0, h = 0, ch = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* pixels = stbi_load(path.string().c_str(), &w, &h, &ch, 0);
        VALIDATE(pixels, return false, "", "image: failed to load '{}': {}", (path.string(), stbi_failure_reason()))
        const bool ok = upload(pixels, w, h, ch);
        stbi_image_free(pixels);
        return ok;
    }


    bool image::load_from_memory(const void* data, size_t size) {

        int w = 0, h = 0, ch = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(data), static_cast<int>(size), &w, &h, &ch, 0);
        VALIDATE(pixels, return false, "", "image: failed to decode buffer: {}", (stbi_failure_reason()))
        const bool ok = upload(pixels, w, h, ch);
        stbi_image_free(pixels);
        return ok;
    }


    void image::release() {

        if (m_texture) {
            glDeleteTextures(1, &m_texture);
            m_texture = 0;
        }
        m_size = {0, 0};
        m_channels = 0;
    }

    // CLASS PRIVATE ===================================================================================================

    bool image::upload(const unsigned char* pixels, int w, int h, int channels) {

        VALIDATE(pixels && w > 0 && h > 0, return false, "", "image: upload called with invalid dimensions {}x{} ch={}", (w, h, channels))

        GLenum fmt = GL_RGBA;
        switch (channels) {
            case 1: fmt = GL_RED; break;
            case 2: fmt = GL_RG; break;
            case 3: fmt = GL_RGB; break;
            case 4: fmt = GL_RGBA; break;
            default:
                LOG(error, "image: unsupported channel count {}", channels);
                return false;
        }

        release();      // drop any previous texture first

        glGenTextures(1, &m_texture);
        if (!m_texture) {
            LOG(error, "image: glGenTextures failed");
            return false;
        }

        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // stb rows aren't 4-byte padded
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, pixels);
        glBindTexture(GL_TEXTURE_2D, 0);

        m_size = {w, h};
        m_channels = channels;
        return true;
    }

}
