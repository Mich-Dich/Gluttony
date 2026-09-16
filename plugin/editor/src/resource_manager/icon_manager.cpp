
#include "util/pch.h"
#include "icon_manager.h"

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wsign-compare"
    #pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#if defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif

#include <util/io/vfs.h>
#include <util/io/directory_iterator.h>
#include <plugin_system/plugin_manager.h>
#include <plugin_system/i_renderer_plugin.h>



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::editor::icon_manager {

    // CONSTANTS =======================================================================================================

    constexpr u32                                           PADDING = 0;                    // transparent border around each icon

    constexpr u32                                           THUMBNAIL_CELL_SIZE = 128;      // px, largest side of a thumbnail

    constexpr u32                                           THUMBNAIL_PAGE_COLS = 8;

    constexpr u32                                           THUMBNAIL_PAGE_ROWS = 8;

    constexpr u32                                           THUMBNAIL_PAGE_SLOTS = THUMBNAIL_PAGE_COLS * THUMBNAIL_PAGE_ROWS;

    constexpr u32                                           THUMBNAIL_PAGE_WIDTH = THUMBNAIL_PAGE_COLS * THUMBNAIL_CELL_SIZE;   // 1024

    constexpr u32                                           THUMBNAIL_PAGE_HEIGHT = THUMBNAIL_PAGE_ROWS * THUMBNAIL_CELL_SIZE;   // 1024

    // MACROS ==========================================================================================================

    #define OUTPUT_CPU_SIDE_THUMBNAIL_BUFFER                0

    // TYPES ===========================================================================================================

    struct grid_dimensions {

        u32                                                 cols = 0;
        u32                                                 rows = 0;

        u32 cell_count() const noexcept                     { return cols * rows; }

        auto operator<=>(const grid_dimensions&) const = default;   // gives ==, <, etc.

    };


    struct atlas_layout {

        u32                                                 texture_width = 0;
        u32                                                 texture_height = 0;
        grid_dimensions                                     grid{};
        u32                                                 cell_size = 0;   // icon + padding
        u32                                                 icon_padding = 0;
    };


    struct icon_source {

        icon                                                id{};
        std::filesystem::path                               path{};
    };

    // thumbnail -------------------------------------------------------------------------------------------------------

    struct dirty_rect {

        u32                                                 x = 0;
        u32                                                 y = 0;
        u32                                                 width = 0;
        u32                                                 height = 0;
    };


    struct thumbnail_page {

        GLT::unique_ref<GLT::render::image>                 image{};        // GPU texture
        ImTextureRef                                        tex_ref{};      // cached descriptor
        std::vector<u8>                                     cpu_pixels{};   // RGBA8, PAGE_WIDTH * PAGE_HEIGHT * 4
        std::vector<dirty_rect>                             dirty_rects{};  // regions written since last GPU upload
        u32                                                 used_slots = 0;
    };


    struct thumbnail_slot {

        thumbnail_state                                     state = thumbnail_state::pending;
        u32                                                 page  = 0;
        u32                                                 cell  = 0;
        ImVec2                                              image_size{};
        ImVec2                                              uv0{};
        ImVec2                                              uv1{};
    };


    struct thumbnail_job {

        thumbnail_handle                                    handle{};
        std::filesystem::path                               path{};
    };


    struct thumbnail_result {

        thumbnail_handle                                    handle = invalid_thumbnail_handle;
        bool                                                success = false;
        u32                                                 width = 0;
        u32                                                 height = 0;
        std::vector<u8>                                     pixels{};     // RGBA8, width*height*4
    };


    struct thumbnail_manager {

        // main thread only
        std::vector<thumbnail_page>                         pages{};
        std::vector<thumbnail_slot>                         slots{};          // index = handle - 1
        std::unordered_map<std::string, thumbnail_handle>   path_to_handle{};

        // worker -> main handoff
        std::mutex                                          result_mutex{};
        std::vector<thumbnail_result>                       results{};

        // main -> worker queue
        std::mutex                                          job_mutex{};
        std::condition_variable                             job_cv{};
        std::queue<thumbnail_job>                           jobs{};

        // worker thread
        std::thread                                         worker{};
        std::atomic<bool>                                   worker_stop{false};
    };


    thumbnail_manager& tm() {

        static thumbnail_manager instance;
        return instance;
    }

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

    // thumbnail -------------------------------------------------------------------------------------------------------

    void thumbnail_worker_main();

    void allocate_thumbnail_slot(thumbnail_handle handle);

    void reupload_thumbnail_page(u32 page_idx);

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

    // thumbnail -------------------------------------------------------------------------------------------------------

    void thumbnail_worker_main() {

        auto& state = tm();

        for (;;) {

            thumbnail_job job{};
            {
                std::unique_lock lock(state.job_mutex);
                state.job_cv.wait(lock, [&] {
                    return state.worker_stop.load() || !state.jobs.empty();
                });

                if (state.worker_stop.load() && state.jobs.empty())
                    return;

                job = std::move(state.jobs.front());
                state.jobs.pop();
            }

            thumbnail_result result{};
            result.handle = job.handle;

            int w = 0, h = 0, channels = 0;
            u8* raw = stbi_load(job.path.string().c_str(), &w, &h, &channels, 4);

            if (raw && w > 0 && h > 0) {

                // Scale so the largest side is <= THUMBNAIL_CELL_SIZE, preserving aspect ratio.
                const f32 scale = std::min(1.0f, (f32)THUMBNAIL_CELL_SIZE / (f32)std::max(w, h));
                const u32 nw = std::max(1u, (u32)std::lround((f32)w * scale));
                const u32 nh = std::max(1u, (u32)std::lround((f32)h * scale));

                result.success = true;
                result.width   = nw;
                result.height  = nh;
                result.pixels.resize((size_t)nw * nh * 4);

                // Simple box-filter downsample. Good enough for thumbnails, and avoids pulling in stb_image_resize.h.
                for (u32 y = 0; y < nh; ++y) {

                    const u32 sy0 = (y * (u32)h) / nh;
                    const u32 sy1 = std::max(sy0 + 1, ((y + 1) * (u32)h) / nh);

                    for (u32 x = 0; x < nw; ++x) {

                        const u32 sx0 = (x * (u32)w) / nw;
                        const u32 sx1 = std::max(sx0 + 1, ((x + 1) * (u32)w) / nw);

                        u32 r = 0, g = 0, b = 0, a = 0, count = 0;
                        for (u32 sy = sy0; sy < sy1; ++sy) {
                            for (u32 sx = sx0; sx < sx1; ++sx) {
                                const u8* p = raw + ((size_t)sy * w + sx) * 4;
                                r += p[0]; g += p[1]; b += p[2]; a += p[3];
                                ++count;
                            }
                        }

                        u8* dst = result.pixels.data() + ((size_t)y * nw + x) * 4;
                        dst[0] = (u8)(r / count);
                        dst[1] = (u8)(g / count);
                        dst[2] = (u8)(b / count);
                        dst[3] = (u8)(a / count);
                    }
                }
            }

            if (raw)
                stbi_image_free(raw);

            // std::this_thread::sleep_for(std::chrono::seconds(2));

            {
                std::lock_guard lock(state.result_mutex);
                state.results.push_back(std::move(result));
            }
        }
    }


    void allocate_thumbnail_slot(thumbnail_handle handle) {

        auto& state = tm();

        // Look for a page with a free slot.
        for (u32 page_idx = 0; page_idx < (u32)state.pages.size(); page_idx++) {

            if (state.pages[page_idx].used_slots < THUMBNAIL_PAGE_SLOTS) {

                const u32 cell = state.pages[page_idx].used_slots++;
                state.slots[handle - 1].page = page_idx;
                state.slots[handle - 1].cell = cell;
                return;
            }
        }

        // No room anywhere — create a new page.
        thumbnail_page page{};
        page.cpu_pixels.assign((size_t)THUMBNAIL_PAGE_WIDTH * THUMBNAIL_PAGE_HEIGHT * 4, 0);
        page.image = GLT::create_unique_ref<GLT::render::image>(page.cpu_pixels.data(), THUMBNAIL_PAGE_WIDTH, THUMBNAIL_PAGE_HEIGHT, false);
        page.tex_ref = page.image->get_descriptor_set();

        state.pages.push_back(std::move(page));
        const u32 page_idx = (u32)state.pages.size() - 1;
        state.pages[page_idx].used_slots = 1;
        state.slots[handle - 1].page = page_idx;
        state.slots[handle - 1].cell = 0;
    }


    void reupload_thumbnail_page(u32 page_idx) {

        auto& state = tm();
        auto& page  = state.pages[page_idx];

        if (!page.image || page.dirty_rects.empty())
            return;

        // update_region() expects tightly-packed rows, but cpu_pixels has
        // THUMBNAIL_PAGE_WIDTH*4 bytes per row. Copy each dirty rect into a
        // small scratch buffer before uploading.
        for (const auto& r : page.dirty_rects) {

            std::vector<u8> scratch((size_t)r.width * r.height * 4);
            for (u32 y = 0; y < r.height; ++y) {

                const u8* src = page.cpu_pixels.data() + ((size_t)(r.y + y) * THUMBNAIL_PAGE_WIDTH + r.x) * 4;
                u8* dst = scratch.data() + (size_t)y * r.width * 4;
                std::memcpy(dst, src, (size_t)r.width * 4);
            }

            page.image->update_region(scratch.data(), r.x, r.y, r.width, r.height, 0);
        }

        page.dirty_rects.clear();
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
        
        // build one atlas per size group
        LOG(trace, "Number of atlas to create [{}]", icons_sorted_by_atlas.size())
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

            LOG(trace, "Atlas built: [{}] icons of size [{}x{}] result in an atlas of size [{}x{}], layout [{}, {}]",
                count, icon_w, icon_h, layout.texture_width, layout.texture_height, layout.grid.rows, layout.grid.cols);

            s_atlases.push_back(std::move(atlas));
        }

        // Start the thumbnail worker thread.
        auto& state = tm();
        state.worker_stop = false;
        state.worker      = std::thread(thumbnail_worker_main);
    }


    icon_data get(const icon type) {

        const auto it = s_icons.find(type);
        if (it != s_icons.end())
            return it->second;

        LOG(error, "Icon [{}] not found in any atlas", GLT::util::enum_to_string(type));
        return icon_data{};     // empty — callers should check tex_ref before drawing
    }


    void shutdown() {

        auto& state = tm();

        // Ask the worker to stop and wait for it to finish. Must happen before we touch `results` or `jobs`.
        {
            std::lock_guard lock(state.job_mutex);
            state.worker_stop = true;
        }
        state.job_cv.notify_all();
        if (state.worker.joinable())
            state.worker.join();

        // Tear down thumbnail state.
        state.pages.clear();
        state.slots.clear();
        state.path_to_handle.clear();
        {
            std::lock_guard lock(state.result_mutex);
            state.results.clear();
        }
        {
            std::lock_guard lock(state.job_mutex);
            std::queue<thumbnail_job> empty{};
            state.jobs.swap(empty);
        }

        s_icons.clear();
        s_atlases.clear();      // destroys the GPU images
    }

    // thumbnail -------------------------------------------------------------------------------------------------------

    thumbnail_handle request_thumbnail(const std::filesystem::path& path) {

        if (path.empty())
            return invalid_thumbnail_handle;

        auto& state = tm();
        const std::string key = path.string();

        const auto it = state.path_to_handle.find(key);
        if (it != state.path_to_handle.end())
            return it->second;

        const thumbnail_handle handle = (thumbnail_handle)state.slots.size() + 1;

        state.slots.emplace_back();
        allocate_thumbnail_slot(handle);                                // reserves page + cell
        state.path_to_handle.emplace(key, handle);

        {
            std::lock_guard lock(state.job_mutex);
            state.jobs.push({ handle, path });
        }
        state.job_cv.notify_one();

        return handle;
    }


    thumbnail_data get_thumbnail(thumbnail_handle handle) {

        auto& state = tm();

        thumbnail_data out{};
        if (handle == invalid_thumbnail_handle || handle > state.slots.size()) {

            out.state = thumbnail_state::failed;
            return out;
        }

        const auto& slot = state.slots[handle - 1];

        out.state = slot.state;
        out.image_size = slot.image_size;
        out.uv0 = slot.uv0;
        out.uv1 = slot.uv1;

        if (slot.state == thumbnail_state::ready && slot.page < state.pages.size())
            out.tex_ref = state.pages[slot.page].tex_ref;

        return out;
    }


    thumbnail_data get_thumbnail(const std::filesystem::path& path) {

        return get_thumbnail(request_thumbnail(path));
    }


    void flush_thumbnail_uploads() {

        auto& state = tm();

        // Drain finished jobs -----------------------------------------------------------------------------------------
        std::vector<thumbnail_result> results{};
        {
            std::lock_guard lock(state.result_mutex);
            results.swap(state.results);
        }

        if (results.empty())
            return;

        std::vector<u32> dirty_pages{};
        dirty_pages.reserve(state.pages.size());

        // Blit each finished image into its atlas cell ----------------------------------------------------------------
        for (auto& r : results) {

            auto& slot = state.slots[r.handle - 1];

            if (!r.success) {
                slot.state = thumbnail_state::failed;
                continue;
            }

            auto& page = state.pages[slot.page];

            const u32 cx = slot.cell % THUMBNAIL_PAGE_COLS;
            const u32 cy = slot.cell / THUMBNAIL_PAGE_COLS;
            const u32 dst_x = cx * THUMBNAIL_CELL_SIZE + (THUMBNAIL_CELL_SIZE - r.width)  / 2;
            const u32 dst_y = cy * THUMBNAIL_CELL_SIZE + (THUMBNAIL_CELL_SIZE - r.height) / 2;

            for (u32 y = 0; y < r.height; y++) {

                u8* dst_row = page.cpu_pixels.data() + ((size_t)(dst_y + y) * THUMBNAIL_PAGE_WIDTH + dst_x) * 4;
                const u8* src_row = r.pixels.data() + (size_t)y * r.width * 4;
                std::memcpy(dst_row, src_row, (size_t)r.width * 4);
            }
            page.dirty_rects.push_back({ dst_x, dst_y, r.width, r.height });

            // Record UVs & mark the page dirty ------------------------------------------------------------------------
            const f32 inv_w = 1.0f / (f32)THUMBNAIL_PAGE_WIDTH;
            const f32 inv_h = 1.0f / (f32)THUMBNAIL_PAGE_HEIGHT;

            const f32 x0 = (f32)dst_x;
            const f32 y0 = (f32)dst_y;
            const f32 x1 = x0 + (f32)r.width;
            const f32 y1 = y0 + (f32)r.height;

            slot.image_size = { (f32)r.width, (f32)r.height };
            slot.uv0 = { x0 * inv_w,   y0 * inv_h   };
            slot.uv1 = { x1 * inv_w,   y1 * inv_h   };
            slot.state = thumbnail_state::ready;

            if (std::find(dirty_pages.begin(), dirty_pages.end(), slot.page) == dirty_pages.end())
                dirty_pages.push_back(slot.page);
        }

        // Re-upload each dirty page exactly once ----------------------------------------------------------------------
        for (u32 p : dirty_pages)
            reupload_thumbnail_page(p);

        #if OUTPUT_CPU_SIDE_THUMBNAIL_BUFFER

            // Output thumbnail pages (CPU side buffer)
            for (size_t index = 0; index < state.pages.size(); index++) {
                const auto file_name = std::string("/home/mich/Pictures/BUFFER/atlas_dump_CPU_") + GLT::util::to_string(index) + std::string(".png");
                stbi_write_png(file_name.c_str(), THUMBNAIL_PAGE_WIDTH, THUMBNAIL_PAGE_HEIGHT, 4, 
                state.pages[index].cpu_pixels.data(), THUMBNAIL_PAGE_WIDTH * 4);
            }

        #endif
    }


    u64 thumbnail_atlas_memory_usage() {

        const auto& state = tm();
        return (u64)state.pages.size() * THUMBNAIL_PAGE_WIDTH * THUMBNAIL_PAGE_HEIGHT * 4;
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

}
