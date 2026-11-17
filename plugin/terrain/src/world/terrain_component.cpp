
#include "util/pch.h"
#include "terrain_component.h"

#include <imgui.h>
#include <asset/i_asset_registry.h>
#include <world/i_world.h>
#include <world/owned_storage.h>

#include "util/ui/panel_collection.h"
#include "util/ui/asset_picker.h"

#include "asset/terrain_graph_asset.h"
#include "graph/graph.h"



namespace GLT::terrain {

    namespace {

        // Payload layout. Append-only: older (shorter) payloads load, missing trailing fields keep their defaults
        //   u32 version (1)
        //   str graph content path        (u32 length + bytes, empty = none)
        //   u64 seed
        //   u32 override count, then { str name, f32 value } per override
        //   u8 mode, u8 enabled
        //   f32 chunk_size, u32 chunk_quads, u32 lod_levels, f32 view_distance, f32 detail
        //   f32 bake_min.x, bake_min.y, bake_max.x, bake_max.y, u32 bake_level, str bake_folder
        constexpr u32                                   PAYLOAD_VERSION = 1;

        GLT::asset::type                                s_graph_type{ GLT::asset::core_types::invalid };
        terrain_component_actions                       s_actions{};

        struct writer {
            std::vector<std::byte>& out;
            template<typename T> void pod(const T& value) {
                const auto* bytes = reinterpret_cast<const std::byte*>(&value);
                out.insert(out.end(), bytes, bytes + sizeof(T));
            }
            void str(std::string_view text) {
                pod(static_cast<u32>(text.size()));
                const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
                out.insert(out.end(), bytes, bytes + text.size());
            }
        };

        struct reader {
            std::span<const std::byte> in;
            size_t offset = 0;
            template<typename T> bool pod(T& value) {
                if (offset + sizeof(T) > in.size())
                    return false;
                std::memcpy(&value, in.data() + offset, sizeof(T));
                offset += sizeof(T);
                return true;
            }
            bool str(std::string& text) {
                u32 length = 0;
                if (!pod(length) || offset + length > in.size())
                    return false;
                text.assign(reinterpret_cast<const char*>(in.data() + offset), length);
                offset += length;
                return true;
            }
        };


        void save(const terrain_component& c, std::vector<std::byte>& out) {

            writer w{ out };
            w.pod(PAYLOAD_VERSION);

            std::string path;
            if (auto* registry = GLT::asset::registry::get_ptr(); registry && c.graph.get() != INVALID_HANDLE)
                path = registry->info(c.graph.get()).virtual_path.generic_string();
            w.str(path);
            w.pod(c.seed);
            w.pod(static_cast<u32>(c.overrides.size()));
            for (const auto& [name, value] : c.overrides) {
                w.str(name);
                w.pod(value);
            }
            w.pod(static_cast<u8>(c.mode));
            w.pod(static_cast<u8>(c.enabled ? 1 : 0));
            w.pod(c.chunk_size);
            w.pod(c.chunk_quads);
            w.pod(c.lod_levels);
            w.pod(c.view_distance);
            w.pod(c.detail);
            w.pod(c.bake_min.x);
            w.pod(c.bake_min.y);
            w.pod(c.bake_max.x);
            w.pod(c.bake_max.y);
            w.pod(c.bake_level);
            w.str(c.bake_folder);
        }


        void load(terrain_component& c, std::span<const std::byte> in) {

            reader r{ in };
            u32 version = 0;
            if (!r.pod(version) || version == 0)
                return;

            std::string path;
            if (r.str(path) && !path.empty())
                if (auto* registry = GLT::asset::registry::get_ptr()) {
                    if (const GLT::asset::handle found = registry->find(path); found != INVALID_HANDLE)
                        c.graph = GLT::asset::asset_ref{ found };
                    else if (auto loaded = registry->load(path); loaded)
                        c.graph = std::move(*loaded);
                    else
                        LOG(warn, "terrain: the world's terrain graph [{}] could not be loaded", path)
                }
            r.pod(c.seed);

            u32 count = 0;
            if (r.pod(count))
                for (u32 i = 0; i < count && i < 4096; ++i) {
                    std::string name;
                    f32 value = 0.0f;
                    if (!r.str(name) || !r.pod(value))
                        break;
                    c.overrides.emplace_back(std::move(name), value);
                }

            u8 mode = 0, enabled = 1;
            if (r.pod(mode))
                c.mode = mode == 1 ? terrain_mode::baked : terrain_mode::live;
            if (r.pod(enabled))
                c.enabled = enabled != 0;
            r.pod(c.chunk_size);
            r.pod(c.chunk_quads);
            r.pod(c.lod_levels);
            r.pod(c.view_distance);
            r.pod(c.detail);
            r.pod(c.bake_min.x);
            r.pod(c.bake_min.y);
            r.pod(c.bake_max.x);
            r.pod(c.bake_max.y);
            r.pod(c.bake_level);
            r.str(c.bake_folder);

            // Never trust a file with sizes that would explode
            c.chunk_size = std::clamp(c.chunk_size, 1.0f, 1.0e5f);
            c.chunk_quads = std::clamp(c.chunk_quads, 4u, 256u);
            c.lod_levels = std::clamp(c.lod_levels, 1u, 16u);
            c.view_distance = std::clamp(c.view_distance, 10.0f, 1.0e7f);
            c.detail = std::clamp(c.detail, 0.5f, 8.0f);
            c.bake_level = std::min(c.bake_level, c.lod_levels - 1);
        }


        // The graph's exposed parameters: { name, default }
        std::vector<std::pair<std::string, f32>> exposed_parameters(const terrain_component& c) {

            std::vector<std::pair<std::string, f32>> out;
            auto* registry = GLT::asset::registry::get_ptr();
            if (!registry || c.graph.get() == INVALID_HANDLE)
                return out;
            const auto* asset = dynamic_cast<const terrain_graph_asset*>(registry->data(c.graph.get()));
            if (!asset)
                return out;
            const std::shared_ptr<const terrain_graph> graph = asset->graph();
            for (const auto& node : graph->nodes) {
                if (node.type != "terrain.parameter")
                    continue;
                const auto name = node.params.find("Name");
                const auto fallback = node.params.find("Default");
                out.emplace_back(name != node.params.end() ? detail::convert_param<std::string>(name->second) : std::string("parameter"),
                    fallback != node.params.end() ? detail::convert_param<f32>(fallback->second) : 0.0f);
            }
            return out;
        }


        void draw(GLT::world::i_world& world, GLT::world::entity_id id) {

            auto* c = GLT::world::component_registry::get<terrain_component>(world, id);
            if (!c)
                return;

            static const terrain_component defaults{};
            ImGui::PushID("terrain_component");

            // ---- source ----------------------------------------------------------------------------------------------
            if (s_graph_type != GLT::asset::core_types::invalid)
                GLT::UI::property_asset("Graph", c->graph, s_graph_type, "The terrain graph (its Surface output) that makes this world's terrain", false);
            GLT::UI::property_custom("Seed", [&](f32) {
                return ImGui::InputScalar("##seed", ImGuiDataType_U64, &c->seed);
            }, "Same graph, different seed: a different landscape");
            GLT::UI::property_toggle("Enabled", c->enabled, "Off: no terrain is generated (baked chunks stay)", &defaults.enabled);

            static constexpr const char* MODES[] = { "Live", "Baked" };
            i32 mode = static_cast<i32>(c->mode);
            const i32 default_mode = 0;
            if (GLT::UI::property_segmented("Mode", mode, MODES,
                "Live: chunks are generated around the streaming anchor at run time, nothing is saved.\nBaked: chunks are baked into mesh assets and saved with the world",
                &default_mode))
                c->mode = mode == 1 ? terrain_mode::baked : terrain_mode::live;

            // ---- exposed parameters ------------------------------------------------------------------------------------
            const auto parameters = exposed_parameters(*c);
            if (!parameters.empty()) {
                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                ImGui::TextDisabled("Exposed parameters");
                for (const auto& [name, fallback] : parameters) {
                    auto it = std::find_if(c->overrides.begin(), c->overrides.end(), [&](const auto& o) { return o.first == name; });
                    f32 value = it != c->overrides.end() ? it->second : fallback;
                    ImGui::PushID(name.c_str());
                    if (GLT::UI::property_drag(name.c_str(), value, std::max(std::abs(value) * 0.01f, 0.01f), 0.0f, 0.0f, "%.4g", nullptr, &fallback)) {
                        if (value == fallback) {
                            if (it != c->overrides.end())
                                c->overrides.erase(it);
                        } else if (it != c->overrides.end())
                            it->second = value;
                        else
                            c->overrides.emplace_back(name, value);
                    }
                    ImGui::PopID();
                }
            }

            // ---- chunks ----------------------------------------------------------------------------------------------
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            ImGui::TextDisabled("Chunks");
            GLT::UI::property_drag("Chunk size", c->chunk_size, 1.0f, 8.0f, 4096.0f, "%.0f m", "World size of the finest chunks", &defaults.chunk_size);
            i32 quads = static_cast<i32>(c->chunk_quads);
            const i32 default_quads = static_cast<i32>(defaults.chunk_quads);
            if (GLT::UI::property_slider("Resolution", quads, 8, 128, "%d quads", "Quads per chunk side (every level)", &default_quads))
                c->chunk_quads = static_cast<u32>(quads);
            ImGui::TextDisabled("  %.2f m between samples at the finest level", c->chunk_size / static_cast<f32>(std::max(c->chunk_quads, 1u)));

            if (c->mode == terrain_mode::live) {
                i32 levels = static_cast<i32>(c->lod_levels);
                const i32 default_levels = static_cast<i32>(defaults.lod_levels);
                if (GLT::UI::property_slider("Detail levels", levels, 1, 12, "%d", "Each level doubles chunk size and sample spacing", &default_levels))
                    c->lod_levels = static_cast<u32>(levels);
                GLT::UI::property_drag("View distance", c->view_distance, 10.0f, 100.0f, 1.0e6f, "%.0f m", "Terrain is generated this far around the streaming anchor", &defaults.view_distance);
                GLT::UI::property_slider("Detail", c->detail, 0.75f, 6.0f, "%.2f", "Higher: fine chunks reach further (more triangles)", &defaults.detail);
                if (GLT::UI::subtle_button("Regenerate"))
                    ++c->regenerate;
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Evaluate every chunk again (e.g. after a node pack was rebuilt)");
            } else {
                // ---- bake ----------------------------------------------------------------------------------------------
                ImGui::Dummy(ImVec2(0.0f, 2.0f));
                ImGui::TextDisabled("Bake");
                glm::vec2 lo = c->bake_min, hi = c->bake_max;
                bool changed = GLT::UI::property_drag("Extent min", lo, 16.0f, 0.0f, 0.0f, "%.0f", "World x / z of the baked area's corner", &defaults.bake_min);
                changed |= GLT::UI::property_drag("Extent max", hi, 16.0f, 0.0f, 0.0f, "%.0f", nullptr, &defaults.bake_max);
                if (changed) {
                    c->bake_min = glm::min(lo, hi);
                    c->bake_max = glm::max(lo, hi);
                }
                i32 level = static_cast<i32>(c->bake_level);
                const i32 default_level = 0;
                if (GLT::UI::property_slider("Bake level", level, 0, static_cast<i32>(c->lod_levels) - 1, "%d", "0 = finest chunks", &default_level))
                    c->bake_level = static_cast<u32>(level);
                GLT::UI::property_text("Folder", c->bake_folder, "Content folder the chunk meshes are written to", "terrain");

                const f32 size = c->chunk_size * static_cast<f32>(1u << c->bake_level);
                const glm::vec2 extent = c->bake_max - c->bake_min;
                const u32 chunks = static_cast<u32>(std::ceil(extent.x / size) * std::ceil(extent.y / size));
                ImGui::TextDisabled("  %u chunk mesh(es) of %.0f m", chunks, size);

                if (GLT::UI::accent_button("Bake") && s_actions.bake)
                    s_actions.bake(world);
                ImGui::SameLine();
                if (GLT::UI::subtle_button("Clear bake") && s_actions.clear_bake)
                    s_actions.clear_bake(world);
            }

            if (s_actions.status)
                if (const std::string status = s_actions.status(world); !status.empty())
                    ImGui::TextDisabled("%s", status.c_str());

            ImGui::PopID();
        }


        std::string summary(GLT::world::i_world& world, GLT::world::entity_id id) {

            const auto* c = GLT::world::component_registry::get<terrain_component>(world, id);
            if (!c)
                return {};
            if (c->graph.get() == INVALID_HANDLE)
                return "no graph";
            std::string name;
            if (auto* registry = GLT::asset::registry::get_ptr())
                name = registry->info(c->graph.get()).virtual_path.stem().string();
            return std::format("{} ({})", name, c->mode == terrain_mode::live ? "live" : "baked");
        }

    }

    // FUNCTION IMPLEMENTATION =========================================================================================

    void register_terrain_component(GLT::asset::type graph_type, terrain_component_actions actions) {

        s_graph_type = graph_type;
        s_actions = std::move(actions);
        GLT::world::component_registry::add_owned<terrain_component>(TERRAIN_COMPONENT_NAME, "World",
            draw, summary, save, load, {}, GLT::world::component::scope::world);
    }


    void unregister_terrain_component() {

        GLT::world::component_registry::remove_owned<terrain_component>();
        s_actions = {};
        s_graph_type = GLT::asset::core_types::invalid;
    }


    terrain_component* terrain_of(GLT::world::i_world& world) {

        return GLT::world::component_registry::get_world<terrain_component>(world);
    }


    terrain_component* ensure_terrain(GLT::world::i_world& world) {

        if (terrain_component* existing = terrain_of(world))
            return existing;
        const GLT::world::entity_id id = world.world_entity();
        const u64 hash = GLT::world::component_registry::g_type_hash<terrain_component>;
        if (!id.is_valid() || hash == 0)
            return nullptr;
        if (GLT::world::component::i_storage* storage = world.storage_for(hash))
            storage->add(id);
        return terrain_of(world);
    }


    u64 shape_hash(const terrain_component& c) {

        u64 h = hash_combine(c.seed, c.regenerate);
        for (const auto& [name, value] : c.overrides)
            h = hash_combine(hash_combine(h, hash_string(name)), std::bit_cast<u32>(value));
        h = hash_combine(h, std::bit_cast<u32>(c.chunk_size));
        h = hash_combine(h, c.chunk_quads);
        return h;
    }


    std::unordered_map<std::string, f32> override_map(const terrain_component& c) {

        std::unordered_map<std::string, f32> out;
        for (const auto& [name, value] : c.overrides)
            out[name] = value;
        return out;
    }

}
