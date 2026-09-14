
#include "util/pch.h"
#include "icon_manager.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <util/io/vfs.h>
#include <util/io/directory_iterator.h>
#include "plugin_system/i_renderer_plugin.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor::icon_manager {

    // CONSTANTS =======================================================================================================

    constexpr u32                                               PADDING = 0;    // transparent border around each icon

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    struct grid_dimensions {

        u32                                                     cols = 0;
        u32                                                     rows = 0;

        u32 cell_count() const noexcept                         { return cols * rows; }

        auto operator<=>(const grid_dimensions&) const = default;   // gives ==, <, etc.

    };


    struct atlas_layout {

        u32                                                     texture_width  = 0;
        u32                                                     texture_height = 0;
        grid_dimensions                                         grid{};
        u32                                                     cell_size      = 0;   // icon + padding
        u32                                                     icon_padding   = 0;
    };


    struct icon_source {

        icon                                                    id;
        std::filesystem::path                                   path;
    };

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // Chooses the grid layout for `icon_count` equal-sized icons.
    //
    // Strategy:
    //   1. Minimize max(cols, rows)    -> keeps the texture "size class" small.
    //   2. Minimize cols * rows        -> among equal size classes, waste less.
    //
    // Both objectives matter for a GPU texture: (1) keeps the texture small
    // in the dimension that determines VRAM cost and cache footprint,
    // (2) avoids wasting cells when a tighter packing exists in the same class.
    FORCE_INLINE_R grid_dimensions compute_icon_grid(u32 icon_count);


    atlas_layout plan_atlas(u32 icon_count, u32 icon_size, u32 padding);

    // STATIC VARIABLES ================================================================================================

    static std::unordered_map<icon, icon_data>                  s_icons{};

    static std::vector<GLT::unique_ref<GLT::render::image>>     s_atlases{};

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    FORCE_INLINE_R grid_dimensions compute_icon_grid(u32 icon_count) {

        ASSERT(icon_count > 0, "", "compute_icon_grid() called with zero icons");

        grid_dimensions best{1, 1};
        u32 best_max_dim = std::numeric_limits<u32>::max();
        u32 best_area    = std::numeric_limits<u32>::max();

        for (u32 cols = 1; cols <= icon_count; ++cols) {

            const u32 rows = (icon_count + cols - 1) / cols;      // ceil division
            const u32 max_dim = std::max(cols, rows);
            const u32 area = cols * rows;

            if (max_dim < best_max_dim ||
               (max_dim == best_max_dim && area < best_area)) {

                best = { cols, rows };
                best_max_dim = max_dim;
                best_area = area;
            }
        }
        return best;
    }


    atlas_layout plan_atlas(u32 icon_count, u32 icon_size, u32 padding) {

        const grid_dimensions g = compute_icon_grid(icon_count);
        const u32 cell = icon_size + padding * 2;

        atlas_layout out;
        out.grid           = g;
        out.cell_size      = cell;
        out.icon_padding   = padding;
        out.texture_width  = g.cols * cell;
        out.texture_height = g.rows * cell;
        return out;
    }

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void init() { 

        std::error_code error{};
        const auto base_path = GLT::util::get_executable_path() / GLT::config::ASSET_DIR / "image";
        std::map<grid_dimensions, std::vector<icon_source>> icons_sorted_by_atlas{};

        for (icon i : GLT::util::enum_values<icon>) {

            const auto file_name  = std::string(GLT::util::enum_to_string(i)) + ".png";
            const auto image_path = base_path / file_name;
            const bool image_exists = GLT::vfs::exists(image_path, error);
            VALIDATE(!error && image_exists, continue, "",
                "Image [{}] was not at expected location [{}]",
                GLT::util::enum_to_string(i), image_path.generic_string())

            int channels = 0, width = 0, height = 0;
            u8* data = stbi_load(image_path.string().c_str(), &width, &height, &channels, 4);
            VALIDATE(data, continue, "", "stbi_load failed for [{}]", image_path.generic_string())
            stbi_image_free(data);

            icons_sorted_by_atlas[{ (u32)width, (u32)height }].push_back({ i, image_path });
        }
        
        LOG(trace, "Number of atlas to create [{}]", icons_sorted_by_atlas.size())
        for (const auto& entry : icons_sorted_by_atlas) {

            LOG(trace, "Icon size [{}, {}] number od icons [{}]", entry.first.cols, entry.first.rows, entry.second.size())
            for (auto& source : entry.second)
                LOG(trace, "    path [{}]", source.path.generic_string())
        }

        // build one atlas per size group
        for (const auto& [size, sources] : icons_sorted_by_atlas) {

            const u32 icon_w = size.cols;
            const u32 icon_h = size.rows;
            ASSERT(icon_w == icon_h, "", "Only square icons supported for now");

            const u32 count  = (u32)sources.size();
            const atlas_layout layout = plan_atlas(count, icon_w, PADDING);

            // RGBA8, zero-initialised (so the padding ring is transparent)
            std::vector<u8> buffer((size_t)layout.texture_width * layout.texture_height * 4, 0);

            for (u32 idx = 0; idx < count; ++idx) {                         // Blit each icon into its cell

                int w = 0, h = 0, channels = 0;
                u8* pixels = stbi_load(sources[idx].path.string().c_str(), &w, &h, &channels, 4);
                
                VALIDATE(pixels, continue, "", "Failed to load icon [{}] for atlas", sources[idx].path.generic_string())
                ASSERT((u32)w == icon_w && (u32)h == icon_h, "", "Icon [{}] changed size between passes", sources[idx].path.generic_string());

                const u32 cx    = idx % layout.grid.cols;
                const u32 cy    = idx / layout.grid.cols;
                const u32 dst_x = cx * layout.cell_size + layout.icon_padding;
                const u32 dst_y = cy * layout.cell_size + layout.icon_padding;

                // Row-by-row copy: source rows are tight (w*4 bytes), destination
                // rows are texture_width*4 bytes apart.
                for (u32 y = 0; y < icon_h; ++y) {

                    u8* dst_row = buffer.data() + ((size_t)(dst_y + y) * layout.texture_width + dst_x) * 4;
                    const u8* src_row = pixels + (size_t)y * icon_w * 4;
                    std::memcpy(dst_row, src_row, (size_t)icon_w * 4);
                }

                stbi_image_free(pixels);
            }

            auto atlas = GLT::create_unique_ref<GLT::render::image>(        // Upload as a single GPU image, no mipmaps
                buffer.data(),
                layout.texture_width,
                layout.texture_height,
                false);

            const ImTextureRef tex_ref = atlas->get_descriptor_set();

            // Record UVs for every icon that lives in this atlas
            const f32 inv_w = 1.0f / (f32)layout.texture_width;
            const f32 inv_h = 1.0f / (f32)layout.texture_height;
            for (u32 idx = 0; idx < count; ++idx) {

                const u32 cx = idx % layout.grid.cols;
                const u32 cy = idx / layout.grid.cols;
                const f32 x0 = (f32)(cx * layout.cell_size + layout.icon_padding);
                const f32 y0 = (f32)(cy * layout.cell_size + layout.icon_padding);
                const f32 x1 = x0 + (f32)icon_w;
                const f32 y1 = y0 + (f32)icon_h;

                s_icons[sources[idx].id] = icon_data{
                    .tex_ref = tex_ref,
                    .image_size = { (f32)icon_w, (f32)icon_h },
                    .uv0 = { x0 * inv_w, y0 * inv_h },
                    .uv1 = { x1 * inv_w, y1 * inv_h },
                };
            }

            LOG(trace, "Atlas built: {}x{} px, {} icons of {}x{}",
                layout.texture_width, layout.texture_height, count, icon_w, icon_h);

            s_atlases.push_back(std::move(atlas));
        }

    }


    icon_data get(const icon type) {

        const auto it = s_icons.find(type);
        if (it != s_icons.end())
            return it->second;

        LOG(error, "Icon [{}] not found in any atlas", GLT::util::enum_to_string(type));
        return icon_data{};     // empty — callers should check tex_ref before drawing
    }


    void shutdown() {

        s_icons.clear();
        s_atlases.clear();      // destroys the GPU images
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
