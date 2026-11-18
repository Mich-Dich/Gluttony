#pragma once

#include <scripting/i_script_plugin.h>
#include <scripting/script_context.h>
#include <scripting/script_binding_component.h>

// The per-session script host (scripting.md §5.3, §8, §14.4). Owns the live instances of a session:
//
//   - entity scripts: one slot per (entity, instance name) binding of the world's script_binding_components,
//     kept in sync by reconciliation at the start of every update
//   - globals: attached by code or as default globals of the loaded modules
//   - one-shot actions (editor_action, world_generator): created, run, destroyed in one call
//
// Instances only exist while the host runs (set_running). Hot reload: release_registrant() snapshots the [[=persist]]
// fields of a module's instances, stops and destroys them; when the module's classes are registered again the next
// reconciliation recreates the instances and gives them their state back (on_start with was_reloaded).



namespace GLT::scripting_cpp {

    class plugin;


    class script_host final : public GLT::script::i_script_host {
    public:

        explicit script_host(plugin& owner);
        ~script_host() override;

        DELETE_COPY_AND_MOVE_CONSTRUCTOR(script_host)

        // i_script_host -----------------------------------------------------------------------------------------------

        void set_context(const GLT::script::script_host_context& ctx) override;

        void refresh(GLT::world::entity_id entity) override;
        void refresh_all() override;

        void set_running(bool running) override;
        [[nodiscard]] bool is_running() const noexcept override { return m_running; }
        void release_registrant(std::string_view registrant) override;

        [[nodiscard]] std::expected<GLT::script::script_instance_id, GLT::script::script_error> attach_global(
            std::string_view script_name, std::string_view instance_name) override;
        void detach_global(GLT::script::script_instance_id id) override;

        void set_enabled(GLT::script::script_instance_id id, bool enabled) override;
        void wake(GLT::script::script_instance_id id) override;
        void control(GLT::script::script_instance_id id, control_op op, f32 seconds) override;
        [[nodiscard]] bool is_sleeping(GLT::script::script_instance_id id) const override;

        void* session_resource_erased(std::string_view key, u32 type_hash, u32 size, u32 alignment,
            void (*construct)(void* out, void* factory), void* factory, void (*destroy)(void*), std::string_view registrant) override;

        [[nodiscard]] std::expected<void, GLT::script::script_error> run_action(std::string_view script_name,
            GLT::world::entity_id context, std::span<const std::byte> params) override;
        [[nodiscard]] std::expected<void, GLT::script::script_error> call(std::string_view script_name, std::string_view method_name,
            GLT::world::entity_id entity, std::string_view instance_name, std::span<const std::byte> params) override;
        void run_world_generators() override;

        void update(f32 delta_time) override;

        [[nodiscard]] std::span<const GLT::script::script_instance_info> instances() const override;

        void* find_sibling_erased(std::string_view class_name, GLT::world::entity_id entity, std::string_view instance_name,
            std::string_view registrant) override;
        void sleep_until_event_erased(u32 event_type_hash) override;
        void wake_sibling_erased(std::string_view class_name, GLT::world::entity_id entity, std::string_view instance_name) override;

        void prepare_hot_reload(std::string_view registrant) override;
        [[nodiscard]] std::expected<std::vector<std::byte>, GLT::script::script_error> snapshot_state(std::string_view registrant) const override;
        [[nodiscard]] std::expected<std::vector<std::byte>, GLT::script::script_error> snapshot_and_release(std::string_view registrant) override;
        u32 restore_state(std::span<const std::byte> snapshot) override;

        // plugin side -------------------------------------------------------------------------------------------------

        [[nodiscard]] GLT::world::i_world* world() const noexcept { return m_env.world; }

        // The live instance of a binding (null if none): the inspector shows its state and editor_callable buttons
        [[nodiscard]] const GLT::script::script_instance_info* find_instance(GLT::world::entity_id entity,
            std::string_view instance_name) const;

    private:

        struct slot {
            GLT::script::script_instance_id             id = GLT::script::INVALID_SCRIPT_INSTANCE;
            GLT::world::entity_id                       entity = GLT::world::INVALID_ENTITY;     // invalid: global
            std::string                                 script_name{};
            std::string                                 instance_name{};
            GLT::script::script_class_info              cls{};              // copy: valid until its module unloads
            GLT::script::i_script*                      instance = nullptr;
            u64                                         settings_hash = 0;  // of the binding's editor_settings
            f32                                         sleep_timer = -1.0f; // > 0: sleep_for
            bool                                        enabled = true;
            bool                                        sleeping = false;
            bool                                        woke = false;       // next on_update gets woke_from_sleep
            bool                                        stopped = false;    // on_stop already ran (prepare_hot_reload)
            u64                                         update_count = 0;
            u64                                         total_update_ns = 0;
            u64                                         last_update_ns = 0;
        };

        // State of an instance between release and re-creation (hot reload, save files)
        struct pending_state {
            GLT::world::entity_id                       entity = GLT::world::INVALID_ENTITY;
            bool                                        global = false;
            std::string                                 script_name{};
            std::string                                 instance_name{};
            bool                                        enabled = true;
            bool                                        sleeping = false;
            f32                                         sleep_timer = -1.0f;
            std::vector<std::byte>                      persist{};
            GLT::script::context_flags                  flag = 0;           // was_reloaded / from_save
        };

        struct resource {
            u32                                         type_hash = 0;
            void*                                       memory = nullptr;
            u32                                         alignment = 0;
            void                                        (*destroy)(void*) = nullptr;
            std::string                                 registrant{};
        };

        [[nodiscard]] GLT::script::script_context make_context(const slot& s, GLT::script::context_flags flags, f32 dt = 0.0f);

        void reconcile(std::optional<GLT::world::entity_id> only = std::nullopt);
        slot* create_slot(GLT::world::entity_id entity, const GLT::script::script_class_info& cls, std::string_view instance_name,
            std::span<const std::byte> editor_settings, u64 settings_hash);
        void stop_slot(slot& s, GLT::script::context_flags extra);
        void destroy_slot(GLT::script::script_instance_id id, GLT::script::context_flags extra = 0);
        void stop_all();
        void attach_pending_globals();
        void process_destroy_requests();
        void destroy_resources(std::optional<std::string_view> registrant);

        [[nodiscard]] slot* find(GLT::script::script_instance_id id);
        [[nodiscard]] const slot* find(GLT::script::script_instance_id id) const;
        [[nodiscard]] std::optional<pending_state> take_pending(bool global, GLT::world::entity_id entity, std::string_view script_name,
            std::string_view instance_name);
        [[nodiscard]] std::vector<std::byte> write_records(std::span<const pending_state> records) const;
        [[nodiscard]] pending_state record_of(const slot& s) const;

        plugin&                                         m_plugin;
        GLT::script::script_host_context                m_env{};
        bool                                            m_running = false;
        f32                                             m_total_time = 0.0f;
        GLT::script::script_instance_id                 m_next_id = 1;
        std::vector<std::unique_ptr<slot>>              m_slots{};
        slot*                                           m_current = nullptr;    // slot whose callback runs right now
        std::vector<pending_state>                      m_pending{};
        std::vector<GLT::world::entity_id>              m_despawn_requests{};
        std::vector<GLT::script::script_instance_id>    m_detach_requests{};
        std::unordered_map<std::string, resource>       m_resources{};
        std::unordered_set<std::string>                 m_reported{};           // "class not registered" logged once
        std::unordered_set<std::string>                 m_failed{};             // on_start threw: not retried until the binding changes
        mutable std::vector<GLT::script::script_instance_info> m_info{};
    };

}
