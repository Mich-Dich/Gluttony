
#include "util/pch.h"
#include "entity_preset.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::world {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    void register_core_presets() {

        auto& reg = preset_registry::get();

        // ---- Empty --------------------------------------------------------------------------------------------------

        entity_preset empty;
        empty.name = "Empty";
        empty.entity_name = "entity";
        empty.icon = "empty";
        empty.category = "General";
        empty.tooltip = "An empty entity with only a transform";
        reg.add(std::move(empty));

        // Rendering ---------------------------------------------------------------------------------------------------

        {
            // Camera positioned at (2, 1, 2), looking at the origin
            entity_preset cam;
            cam.name = "Camera";
            cam.entity_name = "camera";
            cam.icon = "camera";
            cam.category = "Rendering";
            cam.tooltip = "A camera at (2, 1, 2) looking at the origin";
            cam.position = glm::vec3(10.f, 6.5f, 10.f);
            // Yaw ≈ 45°, pitch ≈ -25° gives a look-at-origin direction close to (-0.67, -0.33, -0.67)
            cam.rotation = glm::vec3(glm::radians(-25.f), glm::radians(45.f), 0.f);

            component_spec c;
            c.name = "camera";
            c.set_number("fov", 45.0);
            cam.components.push_back(std::move(c));

            reg.add(std::move(cam));
        }

        {
            // Empty mesh. The mesh component defaults are valid (invalid handle, visible = true), so the entity just shows
            // up ready for the user to assign an asset from the details panel. Physics is skipped automatically while the
            // handle is invalid (create_physics_body_for bails out), so there's no ghost rigid body
            entity_preset mesh;
            mesh.name = "Mesh";
            mesh.entity_name = "mesh";
            mesh.icon = "mesh_asset";
            mesh.category = "Rendering";
            mesh.tooltip = "An empty mesh entity - pick a mesh asset in the details panel";

            component_spec m;
            m.name = "mesh";
            mesh.components.push_back(std::move(m));

            reg.add(std::move(mesh));
        }

        // lights ------------------------------------------------------------------------------------------------------

        // Directional light doesn't consume position, but a visible origin helps in the outliner. Rotation is what actually
        // points the light - this preset looks down-and-slightly-back, giving a natural morning sun
        {
            entity_preset sun;
            sun.name = "Sun";
            sun.entity_name = "sun";
            sun.icon = "sun";
            sun.category = "Lights";
            sun.tooltip = "A directional light pointing down and forward";
            sun.position = glm::vec3(0.f, 3.f, 0.f);
            sun.rotation = glm::vec3(glm::radians(-45.f), glm::radians(30.f), 0.f);

            component_spec light;
            light.name = "light";
            light.set_string("type", "directional")
                 .set_vec3("color", glm::vec3(1.f, 0.95f, 0.85f))
                 .set_number("intensity", 0.92f);
            sun.components.push_back(std::move(light));

            reg.add(std::move(sun));
        }

        {
            // Point light floating at (0, 3, 0). Generous radius so it lights nearby geometry out of the box
            entity_preset point;
            point.name = "Point Light";
            point.entity_name = "point light";
            point.icon = "point_light";
            point.category = "Lights";
            point.tooltip = "An omnidirectional light 3m above the ground";
            point.position = glm::vec3(0.f, 3.f, 0.f);

            component_spec light;
            light.name = "light";
            light.set_string("type", "point")
                 .set_vec3("color", glm::vec3(1.f))
                 .set_number("intensity", 5.0)
                 .set_number("radius", 15.0);
            point.components.push_back(std::move(light));

            reg.add(std::move(point));
        }

        {
            // Spot light above the origin, aimed straight down (pitch = -90° maps forward -Z to -Y)
            entity_preset spot;
            spot.name = "Spot Light";
            spot.entity_name = "spot light";
            spot.icon = "spot_light";
            spot.category = "Lights";
            spot.tooltip = "A downward-facing spot light 5m above the origin";
            spot.position = glm::vec3(0.f, 5.f, 0.f);
            spot.rotation = glm::vec3(glm::radians(-90.f), 0.f, 0.f);

            component_spec light;
            light.name = "light";
            light.set_string("type", "spot")
                 .set_vec3("color", glm::vec3(1.f, 0.95f, 0.85f))
                 .set_number("intensity", 5.0)
                 .set_number("radius", 20.0)
                 .set_number("inner_cone_angle", static_cast<f64>(glm::radians(20.f)))
                 .set_number("outer_cone_angle", static_cast<f64>(glm::radians(30.f)));
            spot.components.push_back(std::move(light));

            reg.add(std::move(spot));
        }

        // audio -------------------------------------------------------------------------------------------------------

        {
            // Empty audio source. Default source_config is valid (2D, volume 1, no attenuation), so the entity is usable the
            // moment the user drops a clip in. Sound only plays after the user hits Play or toggles autoplay
            entity_preset audio;
            audio.name = "Audio Source";
            audio.entity_name = "audio source";
            audio.icon = "sound";
            audio.category = "Audio";
            audio.tooltip = "An empty audio source - assign a clip in the details panel";

            component_spec a;
            a.name = "audio_source";
            audio.components.push_back(std::move(a));

            reg.add(std::move(audio));
        }
    }

    // CLASS IMPLEMENTATION ============================================================================================

    // CLASS PUBLIC ====================================================================================================

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void preset_registry::add(entity_preset preset) {

        for (const auto& existing : m_presets)
            if (existing.name == preset.name)
                return;                             // idempotent - allows register_core_presets() to be called freely

        m_presets.push_back(std::move(preset));
    }


    bool preset_registry::remove(std::string_view name) {

        const auto it = std::find_if(m_presets.begin(), m_presets.end(),
            [name](const entity_preset& p) { return p.name == name; });

        if (it == m_presets.end())
            return false;

        m_presets.erase(it);
        return true;
    }


    const entity_preset* preset_registry::find(std::string_view name) const noexcept {

        for (const auto& p : m_presets)
            if (p.name == name)
                return &p;
        return nullptr;
    }

}
