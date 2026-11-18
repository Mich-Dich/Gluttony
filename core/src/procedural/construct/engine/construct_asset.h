#pragma once

#include "asset/i_asset_handler.h"
#include "procedural/construct/serialize.h"

// Construct assets: the runtime form of a construct file (".glt_construct") and the handler for it. The core registers
// the handler (editor/integration.h); packs may name older asset types of theirs that have the same format.



namespace GLT::construct {

    // CONSTANTS =======================================================================================================

    inline constexpr GLT::asset::chunk_id               CHUNK_CONSTRUCT = 0x54534e43;   // "CNST": construct_data (serialize.h)
    inline constexpr GLT::asset::chunk_id               CHUNK_CONSTRUCT_PREVIEW = 0x56525043;  // "CPRV": editor preview settings
    inline constexpr GLT::asset::chunk_id               CHUNK_CONSTRUCT_EXPORT = 0x50584543;   // "CEXP": last static mesh export path

    // TYPES ===========================================================================================================

    // @brief Editor state stored with the construct (not part of what it generates)
    struct construct_preview_settings {

        u64                                             seed = 1;
        glm::vec3                                       camera_target{ 0.0f };
        f32                                             camera_distance = 0.0f;     // 0: frame the construct on open
        f32                                             camera_yaw = 0.7f;
        f32                                             camera_pitch = -0.45f;
        bool                                            show_guides = true;
        std::filesystem::path                           export_path{};              // last "Export static mesh" target (content relative)
    };

    // CLASS DECLARATION ===============================================================================================

    // @brief The runtime form of a construct file
    //
    // [data] is shared and immutable once published: an edit builds new data and swaps the pointer (revision++), so
    // evaluations in flight keep a consistent snapshot
    class construct_asset final : public GLT::asset::i_runtime_asset {
    public:

        explicit construct_asset(GLT::asset::type type) : m_type(type) {}

        [[nodiscard]] GLT::asset::type type() const noexcept override { return m_type; }
        [[nodiscard]] u64 memory_usage() const noexcept override;

        [[nodiscard]] std::shared_ptr<const construct_data> data() const;
        void set_data(construct_data data);
        [[nodiscard]] u64 revision() const noexcept { return m_revision.load(std::memory_order_acquire); }

        construct_preview_settings                      preview{};

    private:

        GLT::asset::type                                m_type;
        mutable std::mutex                              m_mutex{};
        std::shared_ptr<const construct_data>           m_data = std::make_shared<const construct_data>();
        std::atomic<u64>                                m_revision{ 1 };
    };


    // @brief Handler of the construct asset types (".glt_construct", plus older per tool types such as
    // ".glt_construct_plant": same format)
    class construct_asset_handler final : public GLT::asset::i_asset_handler {
    public:

        void set_types(std::vector<GLT::asset::type> types) { m_types = std::move(types); }
        [[nodiscard]] bool handles(GLT::asset::type type) const noexcept { return std::ranges::find(m_types, type) != m_types.end(); }

        void on_load() override {}
        void on_unload() override {}

        [[nodiscard]] std::span<const GLT::asset::type> types() const noexcept override { return m_types; }

        [[nodiscard]] std::expected<GLT::unique_ref<GLT::asset::i_runtime_asset>, GLT::asset::load_error> deserialize(
            const GLT::asset::info& info, GLT::asset::chunk_reader& reader) override;

        [[nodiscard]] std::expected<void, GLT::asset::load_error> serialize(const GLT::asset::info& info,
            const GLT::asset::i_runtime_asset& asset, GLT::asset::asset_writer& out) const override;

    private:

        std::vector<GLT::asset::type>                   m_types{};
    };

}
