
#include "util/pch.h"
#include "user_texture.h"

#include <imgui.h>
#include <imgui_internal.h>         // RegisterUserTexture / UnregisterUserTexture



namespace GLT::terrain {

    namespace {

        // Released textures waiting for the backend to destroy their GPU side. Main thread only (ImGui)
        std::vector<ImTextureData*>& graveyard() {
            static std::vector<ImTextureData*> s_textures;
            return s_textures;
        }

    }


    user_texture::~user_texture() { release(); }


    void user_texture::ensure(u32 width, u32 height) {

        if (m_tex && m_width == width && m_height == height)
            return;
        release();
        if (width == 0 || height == 0)
            return;

        m_tex = IM_NEW(ImTextureData)();
        m_tex->Create(ImTextureFormat_RGBA32, static_cast<int>(width), static_cast<int>(height));     // status WantCreate
        ImGui::RegisterUserTexture(m_tex);
        m_width = width;
        m_height = height;
    }


    void user_texture::upload(std::span<const u32> rgba) {

        if (!m_tex || rgba.size() != static_cast<size_t>(m_width) * m_height)
            return;
        std::memcpy(m_tex->GetPixels(), rgba.data(), rgba.size_bytes());

        // Not created yet: the creation uploads every pixel anyway. Otherwise one update of the whole image
        if (m_tex->Status == ImTextureStatus_OK || m_tex->Status == ImTextureStatus_WantUpdates) {
            ImTextureRect full{};
            full.x = 0;
            full.y = 0;
            full.w = static_cast<unsigned short>(m_width);
            full.h = static_cast<unsigned short>(m_height);
            m_tex->Updates.resize(0);
            m_tex->Updates.push_back(full);
            m_tex->UpdateRect = full;
            m_tex->SetStatus(ImTextureStatus_WantUpdates);
        }
    }


    void user_texture::release() {

        if (!m_tex)
            return;
        m_tex->WantDestroyNextFrame = true;         // NewFrame turns it into WantDestroy; the backend answers with Destroyed
        graveyard().push_back(m_tex);
        m_tex = nullptr;
        m_width = m_height = 0;
    }


    void user_texture::collect_released() {

        auto& parked = graveyard();
        for (auto it = parked.begin(); it != parked.end(); ) {
            ImTextureData* tex = *it;
            if (tex->Status != ImTextureStatus_Destroyed) {
                ++it;
                continue;
            }
            ImGui::UnregisterUserTexture(tex);
            IM_DELETE(tex);
            it = parked.erase(it);
        }
    }

}
