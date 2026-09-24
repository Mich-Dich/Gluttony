
#include "util/pch.h"

#include <plugin_system/i_plugin.h>
#include <plugin_system/i_asset_registry_plugin.h>
#include <plugin_system/i_asset_handler_plugin.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::asset::registry_default {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Everything the registry owns for one loaded asset. Lives in a deque so
    // element addresses stay stable even as more assets are loaded (that's
    // what `info.chunks`, `info.dependencies`, etc. point into).
    struct slot {

        u32                                                     generation{ 1 };
        bool                                                    alive{ false };

        std::filesystem::path                                   canonical_path;

        info                                                    asset_info;
        std::vector<chunk_entry>                                chunks_storage;       // info.chunks
        std::vector<GLT::asset::handle>                         deps_storage;         // info.dependencies
        std::vector<GLT::asset::handle>                         dependents_storage;   // info.dependents

        GLT::unique_ref<i_runtime_asset>                        data;
        GLT::asset::i_asset_handler*                            handler{ nullptr };
    };


    // Concrete chunk_reader bound to one file's memory + chunk table.
    class chunk_reader_impl final : public chunk_reader {
    public:

        chunk_reader_impl(std::span<const std::byte> bytes, std::span<const chunk_entry> table) noexcept;


        FORCE_INLINE_R bool has(chunk_id id) const noexcept override;


        FORCE_INLINE_R std::span<const std::byte> get(chunk_id id) const override;


        FORCE_INLINE_R std::span<const chunk_id> available() const noexcept override;

    private:

        FORCE_INLINE_R const chunk_entry* find(chunk_id id) const noexcept;

        std::span<const std::byte>                              m_bytes;
        std::span<const chunk_entry>                            m_chunks;
        mutable std::vector<chunk_id>                           m_id_cache;
    };


    class asset_writer_impl final : public GLT::asset::asset_writer {
    public:

        void write_chunk(GLT::asset::chunk_id id, std::span<const std::byte> data, u32 compression = 0) override;


        void declare_dependency(const UUID id);


        void declare_dependency(std::string_view virtual_path, GLT::asset::type target_type);


        void declare_dependency(const UUID id, std::string_view virtual_path, GLT::asset::type target_type);


        void set_name(std::string_view name);


        struct record_chunk {

            GLT::asset::chunk_id                                id{};
            u32                                                 compression{};
            std::vector<std::byte>                              bytes;
        };


        struct record_dep {

            UUID                                                id{};
            std::string                                         virtual_path;
            GLT::asset::type                                    target_type{ GLT::asset::core_types::invalid };
            bool                                                by_path{ false };
        };

        [[nodiscard]] const std::string& name() const noexcept;
        [[nodiscard]] const std::vector<record_chunk>& chunks() const noexcept;
        [[nodiscard]] const std::vector<record_dep>& deps() const noexcept;

    private:

        std::string                                             m_name{};
        std::vector<record_chunk>                               m_chunks{};
        std::vector<record_dep>                                 m_deps{};

    };

    // STATIC VARIABLES ================================================================================================

    static constexpr const char*                                dependencies_names[] = {

        nullptr
    };

    static constexpr GLT::plugin_manager::interface             dependencies_interfaces[] = {

        GLT::plugin_manager::interface::virtual_file_system,
    };

    static constexpr GLT::plugin_manager::plugin_descriptor     descriptor = {

        .name                                                   = GLT_MODULE_NAME,
        .load_phase                                             = GLT::plugin_manager::phase::pre_application,
        .unload_phase                                           = GLT::plugin_manager::phase::post_application_shutdown,
        .target                                                 = GLT::plugin_manager::interface::asset_registry,
        .dependency_names_count                                 = ARRAY_SIZE(dependencies_names),
        .dependency_names                                       = dependencies_names,
        .dependency_interface_count                             = ARRAY_SIZE(dependencies_interfaces),
        .dependency_interfaces                                  = dependencies_interfaces,
    };

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // FUNCTION DECLARATION ============================================================================================

    FORCE_INLINE_R constexpr u64 make_handle(u32 idx, u32 gen) noexcept;

    FORCE_INLINE_R constexpr u32 handle_index(u64 h) noexcept;

    FORCE_INLINE_R constexpr u32 handle_generation(u64 h) noexcept;

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    class plugin final : public GLT::asset::i_asset_registry_plugin {
    public:

        // ---- i_plugin ----

        void on_load()   override;


        void on_unload() override;

        // ---- lifecycle ----

        [[nodiscard]] std::expected<GLT::asset::handle, GLT::asset::load_error> load(std::filesystem::path path) override;


        void unload(GLT::asset::handle h) noexcept override;

        // persistence -------------------------------------------------------------------------------------------------

        [[nodiscard]] std::expected<void, GLT::asset::load_error> save(GLT::asset::handle h) override;


        [[nodiscard]] std::expected<void, GLT::asset::load_error> save_as(GLT::asset::handle h, const std::filesystem::path& new_path) override;


        [[nodiscard]] bool is_loaded(GLT::asset::handle h) const override;


        [[nodiscard]] GLT::asset::handle find(const std::filesystem::path&) const override;

        // ---- queries ----

        [[nodiscard]] const GLT::asset::info& info(GLT::asset::handle h) const override;


        [[nodiscard]] GLT::asset::i_runtime_asset* data(GLT::asset::handle h) noexcept override;


        [[nodiscard]] const GLT::asset::i_runtime_asset* data(GLT::asset::handle h) const noexcept override;

        // ---- handler registration ----

        void register_handler(GLT::asset::i_asset_handler* handler) override;


        void unregister_handler(GLT::asset::i_asset_handler* handler) noexcept override;


        [[nodiscard]] std::span<const GLT::asset::type> registered_types() const override;

        // factory registration ----------------------------------------------------------------------------------------

        void register_factory(GLT::asset::factory::i_asset_factory_plugin* factory) override;


        void unregister_factory(GLT::asset::factory::i_asset_factory_plugin* factory) noexcept override;

        // import (editor / build-time) --------------------------------------------------------------------------------

        // Routes to whichever factory binds (source_extension, target_type).
        // If out_path is empty, the registry derives one next to the source (or under a configured import root). 
        // On success the imported asset is loaded and its handle returned - the editor basically always wants a preview right away.
        [[nodiscard]] virtual std::expected<GLT::asset::handle, GLT::asset::import_error> import(const std::filesystem::path& source, 
            GLT::asset::type target_type, const std::filesystem::path& out_path = {}, const GLT::asset::import_options& opts = {}) override;


        // Cheap probe: which factories could handle this file? The editor uses this to populate the "Import as…" context menu.
        [[nodiscard]] virtual std::span<const GLT::asset::factory::binding> candidate_imports(const std::filesystem::path& source) const override;

        // ---- type registry ----

        [[nodiscard]] GLT::asset::type reserve_type(std::string_view name) override;


        [[nodiscard]] std::optional<GLT::asset::type> type_from_name(std::string_view name) const override;


        [[nodiscard]] std::string_view name_from_type(GLT::asset::type t) const override;

        // ---- dependency graph ----

        void add_dependency(GLT::asset::handle from, GLT::asset::handle to) override;


        [[nodiscard]] std::span<const GLT::asset::handle> dependencies(GLT::asset::handle h) const override;

        // ---- async ----

        [[nodiscard]] std::future<std::expected<GLT::asset::handle, GLT::asset::load_error>> load_async(std::filesystem::path path) override;

    private:

        [[nodiscard]] slot* slot_for(GLT::asset::handle h) noexcept;


        [[nodiscard]] const slot* slot_for(GLT::asset::handle h) const noexcept;


        [[nodiscard]] GLT::asset::handle acquire_slot();


        void release_slot(GLT::asset::handle h) noexcept;


        // Parses header + tables into out_params; recurses into deps.
        [[nodiscard]] std::expected<GLT::asset::handle, GLT::asset::load_error> load_unlocked(const std::filesystem::path& path, 
            std::unordered_set<std::string>& in_flight);


        [[nodiscard]] std::expected<std::vector<std::byte>, GLT::asset::load_error> read_file(const std::filesystem::path& path) const;


        // --- storage ---
        std::deque<slot>                                                        m_slots{};             // stable addresses
        std::vector<u32>                                                        m_free_indices{};      // recycled slots

        std::unordered_map<std::string, GLT::asset::handle>                     m_by_path{};
        std::unordered_map<GLT::asset::type, GLT::asset::i_asset_handler*>      m_handlers;

        // type registry
        std::vector<std::string>                                                m_type_names{};        // indexed by type.value
        std::unordered_map<std::string, GLT::asset::type>                       m_type_name_to_id{};
        u32                                                                     m_next_custom_type{ CUSTOM_TYPE_BEGIN };
        mutable std::vector<GLT::asset::type>                                   m_registered_types_cache{};

        // --- concurrency ---
        mutable std::shared_mutex                                               m_mutex{};

        // in the class, next to m_by_path:
        std::unordered_map<UUID, GLT::asset::handle>                            m_by_id{};
        std::vector<GLT::asset::factory::i_asset_factory_plugin*>               m_factories{};
        mutable std::vector<GLT::asset::factory::binding>                       m_candidate_cache{};

        // in the plugin class, in the private section:
        struct source_record {
            UUID                                                                id{};
            std::filesystem::path                                               output{};
            GLT::asset::content_hash                                            source_hash{};
            GLT::asset::content_hash                                            payload_hash{};
        };
        std::unordered_map<std::string, source_record>                          m_source_index{};

    };

}

#include "type.inl"
#include "asset_writer_impl.inl"
#include "chunk_reader_impl.inl"
#include "plugin.inl"

EXPORT_PLUGIN_CLASS(GLT::asset::registry_default::plugin, GLT::asset::registry_default::descriptor)
