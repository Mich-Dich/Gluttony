
#pragma once

struct ImTextureData;



namespace GLT::terrain {

    // @brief An RGBA8 image the editor fills on the CPU and ImGui draws (map previews)
    //
    // Uses ImGui 1.92's texture protocol: the ImTextureData is registered as a user texture, the renderer backend creates /
    // updates / destroys the GPU side when the status asks for it. Destruction is asynchronous (frames in flight), so a
    // released texture is parked until the backend reports it Destroyed, then freed. If the plugin unloads first, the parked
    // ImTextureData stays allocated and registered on purpose: it is plain data, and the renderer still owns its GPU side
    class user_texture {
    public:

        user_texture() = default;
        ~user_texture();

        user_texture(const user_texture&) = delete;
        user_texture& operator=(const user_texture&) = delete;

        // (Re)creates the texture at this size if needed. Pixels start black
        void ensure(u32 width, u32 height);

        // Width * height RGBA8 pixels (row-major, top row first). Uploads the whole image this frame
        void upload(std::span<const u32> rgba);

        [[nodiscard]] bool valid() const noexcept { return m_tex != nullptr; }
        [[nodiscard]] u32 width() const noexcept { return m_width; }
        [[nodiscard]] u32 height() const noexcept { return m_height; }

        // For ImGui::Image / ImDrawList::AddImage
        [[nodiscard]] ImTextureData* data() const noexcept { return m_tex; }

        void release();

        // Frees parked textures whose GPU side is gone. Call once per frame from any editor window
        static void collect_released();

    private:

        ImTextureData*                                  m_tex = nullptr;
        u32                                             m_width = 0;
        u32                                             m_height = 0;
    };

}
