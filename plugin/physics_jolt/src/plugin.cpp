
#include "util/pch.h"
#include "util/jolt_pch.h"
#include "plugin.h"

#include "layers.h"
#include "util/handle_table.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    // Only one broadphase layer exists, so no broadphase filtering is needed. The object-layer filter below does the real work
    struct accept_all_bp_filter final : JPH::BroadPhaseLayerFilter {

        bool ShouldCollide(JPH::BroadPhaseLayer) const override { return true; }
    };

    // Implements the contract's first query rule:          (filter.layer_mask & body.object_layer) != 0
    // Jolt hands us the interned ObjectLayer id; we look up the raw object_layer and AND against the caller's mask
    class query_layer_filter final : public JPH::ObjectLayerFilter {
    public:

        query_layer_filter(const layer_registry& layers, u32 layer_mask) noexcept
            : m_layers(layers), m_layer_mask(layer_mask) {}


        bool ShouldCollide(JPH::ObjectLayer layer) const override { return (m_layer_mask & m_layers.object_layer_of(layer)) != 0; }

    private:

        const layer_registry&                           m_layers;
        u32                                             m_layer_mask;

    };

    // Implements the contract's second and third query rules:
    //   body != filter.ignore
    //   !body.is_sensor || filter.hit_sensors
    class query_body_filter final : public JPH::BodyFilter {
    public:

        query_body_filter(const JPH::BodyInterface& bi, body_handle ignore, bool hit_sensors) noexcept
            : m_bi(bi), m_ignore(ignore), m_hit_sensors(hit_sensors) {}


        bool ShouldCollide(const JPH::BodyID& id) const override {

            if (m_ignore.is_valid()) {
                const body_handle h = unpack_body_handle(m_bi.GetUserData(id));
                if (h == m_ignore)
                    return false;
            }
            if (!m_hit_sensors && m_bi.IsSensor(id))
                return false;
            return true;
        }

    private:

        const JPH::BodyInterface&                       m_bi;
        body_handle                                     m_ignore;
        bool                                            m_hit_sensors;

    };

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    plugin::plugin() = default;


    plugin::~plugin() = default;

    // CLASS PUBLIC ====================================================================================================

    std::string_view plugin::backend_name() const noexcept { return "Jolt"; }


    void plugin::on_load() {

        // Process-global Jolt setup. Do this exactly once per plugin load. If you ever ship two physics plugins in one process
        // this needs to become refcounted; for now the plugin_manager'stats one-plugin-per-interface rule guarantees a single owner
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }


    void plugin::on_unload() {

        if (JPH::Factory::sInstance) {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }


    std::expected<void, physics_error> plugin::init(const glm::vec3& gravity) {

        if (m_initialized)
            return std::unexpected(physics_error::invalid_argument);

        m_gravity = gravity;

        // Jolt'stats default PhysicsSystem limits — generous defaults for v0.
        constexpr JPH::uint kMaxBodies = 65536;
        constexpr JPH::uint kNumBodyMutexes = 0;      // 0 = auto
        constexpr JPH::uint kMaxBodyPairs = 65536;
        constexpr JPH::uint kMaxContactConstraints = 8192;

        m_system = GLT::create_unique_ref<JPH::PhysicsSystem>();
        m_system->Init(kMaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints, m_layers, m_object_vs_bp,
            m_layers);

        m_system->SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
        m_system->SetContactListener(&m_contacts);
        m_temp_allocator = GLT::create_unique_ref<JPH::TempAllocatorImpl>(16 * 1024 * 1024);

        const u32 hw = std::max(1u, std::thread::hardware_concurrency());
        m_job_system = GLT::create_unique_ref<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
            static_cast<int>(hw > 1 ? hw - 1 : 1));

        m_initialized = true;
        return {};
    }


    void plugin::shutdown() noexcept {

        if (!m_initialized)
            return;

        // Detach the listener before tearing anything down.
        if (m_system)
            m_system->SetContactListener(nullptr);
        m_contacts.clear();

        // Remove and destroy every body via our own iteration. Then drop the
        // side table before dropping shapes, since body entries hold shape refs.
        if (m_system) {
            JPH::BodyInterface& bi = m_system->GetBodyInterface();
            m_bodies.for_each([&](body_handle, const body_entry& e) {
                bi.RemoveBody(e.body_id);
                bi.DestroyBody(e.body_id);
            });
        }

        m_bodies.clear();
        m_shapes.clear();

        m_system.reset();
        m_job_system.reset();
        m_temp_allocator.reset();

        m_substeps_last_frame = 0;
        m_step_ms_last = 0.f;
        m_step_ms_avg = 0.f;
        m_step_ms_accum = 0.f;
        m_step_frame_count = 0;

        m_initialized = false;
    }

    // stepping --------------------------------------------------------------------------------------------------------

    void plugin::step(f32 delta_time, u32 substeps) {

        if (!m_initialized)
            return;

        const auto t0 = std::chrono::steady_clock::now();

        m_contacts.begin_step();
        m_system->Update(delta_time, substeps, m_temp_allocator.get(), m_job_system.get());

        const auto t1 = std::chrono::steady_clock::now();
        const f32 ms = std::chrono::duration<f32, std::milli>(t1 - t0).count();

        m_substeps_last_frame = substeps;
        m_step_ms_last = ms;
        m_step_ms_accum      += ms;
        m_step_frame_count++;
        m_step_ms_avg = m_step_ms_accum / static_cast<f32>(m_step_frame_count);
    }


    glm::vec3 plugin::get_gravity() const noexcept { return m_gravity; }


    void plugin::set_gravity(const glm::vec3& gravity) {

        m_gravity = gravity;
        if (m_initialized)
            m_system->SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
    }

    // shapes -----------------------------------------------------------------------------------------------------------

    std::expected<shape_handle, physics_error> plugin::create_shape(const shape_desc& desc) {

        if (!m_initialized)
            return std::unexpected(physics_error::invalid_argument);

        return m_shapes.create(desc);
    }


    void plugin::release_shape(shape_handle h) noexcept {

        if (!m_initialized)
            return;

        m_shapes.release(h);
    }


    std::expected<body_handle, physics_error> plugin::create_body(const body_desc& desc) {

        if (!m_initialized)
            return std::unexpected(physics_error::invalid_argument);

        // --- shape resolution: stale vs. unsupported are distinct ----------------
        const shape_entry* s = m_shapes.get(desc.shape);
        if (!s)
            return std::unexpected(physics_error::stale_handle);

        if ((s->motion_mask & motion_bit(desc.motion)) == 0)
            return std::unexpected(physics_error::unsupported_feature);

        // --- dynamic-body validation --------------------------------------------
        if (desc.motion == motion_type::dynamic_body && desc.mass_override == 0.f)
            return std::unexpected(physics_error::invalid_argument);

        // --- Jolt settings -------------------------------------------------------
        JPH::BodyCreationSettings settings;
        settings.SetShape(s->shape.GetPtr());
        settings.mFriction = s->material.friction;
        settings.mRestitution = s->material.restitution;
        settings.mObjectLayer = m_layers.intern(desc.filter);
        settings.mPosition = to_jolt_r(desc.position);
        settings.mRotation = to_jolt(desc.rotation);
        settings.mLinearVelocity = to_jolt(desc.linear_velocity);
        settings.mAngularVelocity = to_jolt(desc.angular_velocity);
        settings.mLinearDamping = desc.linear_damping;
        settings.mAngularDamping = desc.angular_damping;
        settings.mGravityFactor = desc.gravity_factor;
        settings.mAllowSleeping = desc.allow_sleep;
        settings.mIsSensor = desc.is_sensor;
        settings.mMotionQuality = desc.continuous ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;

        switch (desc.motion) {
            case motion_type::static_body:    settings.mMotionType = JPH::EMotionType::Static; break;
            case motion_type::dynamic_body:   settings.mMotionType = JPH::EMotionType::Dynamic; break;
            case motion_type::kinematic_body: settings.mMotionType = JPH::EMotionType::Kinematic; break;
        }

        // --- mass properties for dynamic bodies ---------------------------------
        f32 resolved_mass = 0.f;

        // Jolt builds default mass from the shape's density (which defaults to 1000 kg/m³). We rescale to the material density,
        // then apply the explicit override if one is present. MassAndInertiaProvided gives us full control without touching Jolt's
        // global shape settings
        if (desc.motion == motion_type::dynamic_body) {

            JPH::MassProperties mp = s->shape->GetMassProperties();
            mp.ScaleToMass(mp.mMass * (s->material.density / 1000.f));
            if (desc.mass_override > 0.f)
                mp.ScaleToMass(desc.mass_override);

            resolved_mass = mp.mMass;

            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride = mp;
        }

        // --- create the Jolt body but don't add it yet --------------------------
        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        JPH::Body* body = bi.CreateBody(settings);
        if (!body)
            return std::unexpected(physics_error::out_of_memory);

        const JPH::BodyID body_id = body->GetID();

        // --- register in the side table so we can pack the handle --------------
        body_entry entry{
            .shape = desc.shape,
            .entity = desc.entity,
            .user_tag = desc.user_tag,
            .body_id = body_id,
            .resolved_mass = resolved_mass,
            .contact_mask = desc.contact_mask,
        };
        const body_handle handle = m_bodies.insert(std::move(entry));

        // Hand the handle to Jolt so queries and contacts can recover it.
        body->SetUserData(pack_body_handle(handle));

        bi.AddBody(body_id, JPH::EActivation::DontActivate);

        m_shapes.add_ref(desc.shape);   // debug-only bookkeeping

        return handle;
    }


    void plugin::destroy_body(body_handle h) noexcept {

        if (!m_initialized)
            return;

        body_entry* entry = m_bodies.get(h);
        if (!entry)
            return;

        // Emit phase_end for every active pair involving h before ripping the body out. Jolt may or may not fire OnContactRemoved
        // during RemoveBody(); either way our bookkeeping is already correct
        m_contacts.destroy_body(h);

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        bi.RemoveBody(entry->body_id);
        bi.DestroyBody(entry->body_id);

        m_shapes.dec_ref(entry->shape);
        m_bodies.remove(h);
    }


    bool plugin::is_alive(body_handle h) const noexcept { return m_initialized && m_bodies.get(h) != nullptr; }


    std::optional<body_state> plugin::get_body_state(body_handle h) const {

        if (!m_initialized)
            return std::nullopt;

        const body_entry* entry = m_bodies.get(h);
        if (!entry)
            return std::nullopt;

        JPH::BodyInterface& bi = m_system->GetBodyInterface();

        body_state s{};
        s.position = from_jolt(bi.GetPosition(entry->body_id));
        s.rotation = from_jolt(bi.GetRotation(entry->body_id));
        s.linear_velocity = from_jolt(bi.GetLinearVelocity(entry->body_id));
        s.angular_velocity = from_jolt(bi.GetAngularVelocity(entry->body_id));
        s.mass = entry->resolved_mass;      // 0 for static/kinematic
        s.is_active = bi.IsActive(entry->body_id);
        s.valid = true;
        return s;
    }


    size_t plugin::get_body_states(std::span<const body_handle> bodies, std::span<body_state> out) const {

        assert(bodies.size() == out.size() && "get_body_states: span size mismatch");
        const size_t n = std::min(bodies.size(), out.size());

        if (!m_initialized) {
            for (size_t i = 0; i < n; ++i) {
                out[i] = body_state{};
                out[i].rotation = glm::quat{ 1.f, 0.f, 0.f, 0.f };
                out[i].valid = false;
            }
            return n;
        }

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        for (size_t i = 0; i < n; ++i) {
            const body_entry* entry = m_bodies.get(bodies[i]);
            if (!entry) {
                out[i] = body_state{};
                out[i].rotation = glm::quat{ 1.f, 0.f, 0.f, 0.f };
                out[i].valid = false;
                continue;
            }

            const JPH::BodyID id = entry->body_id;
            out[i].position = from_jolt(bi.GetPosition(id));
            out[i].rotation = from_jolt(bi.GetRotation(id));
            out[i].linear_velocity = from_jolt(bi.GetLinearVelocity(id));
            out[i].angular_velocity = from_jolt(bi.GetAngularVelocity(id));
            out[i].mass = entry->resolved_mass;
            out[i].is_active = bi.IsActive(id);
            out[i].valid = true;
        }
        return n;
    }


    void plugin::set_body_transform(body_handle h, const glm::vec3& pos, const glm::quat& rot) {

        if (!m_initialized)
            return;

        body_entry* entry = m_bodies.get(h);
        if (!entry)
            return;

        // Contract: writes never wake. DontActivate is exactly that. v0 note: kinematic bodies teleport here — dynamic push-through is a v1
        // feature (needs MoveKinematic + tracked delta_time). The transform still updates correctly and dynamic bodies still collide with it
        m_system->GetBodyInterface().SetPositionAndRotation(entry->body_id, to_jolt_r(pos), to_jolt(rot), JPH::EActivation::DontActivate);
    }


    void plugin::set_linear_velocity(body_handle h, const glm::vec3& v) {

        if (!m_initialized)
            return;

        body_entry* entry = m_bodies.get(h);
        if (!entry)
            return;

        // Jolt's BodyInterface::SetLinearVelocity only writes the field; it does not activate the body. This satisfies the contract's §Wake semantics
        m_system->GetBodyInterface().SetLinearVelocity(entry->body_id, to_jolt(v));
    }


    void plugin::set_angular_velocity(body_handle h, const glm::vec3& w) {

        if (!m_initialized)
            return;

        body_entry* entry = m_bodies.get(h);
        if (!entry)
            return;

        m_system->GetBodyInterface().SetAngularVelocity(entry->body_id, to_jolt(w));
    }


    void plugin::translate(body_handle h, const glm::vec3& delta) {

        if (!m_initialized)
            return;

        body_entry* entry = m_bodies.get(h);
        if (!entry)
            return;

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        const JPH::RVec3 pos = bi.GetPosition(entry->body_id);
        const JPH::Quat rot = bi.GetRotation(entry->body_id);

        bi.SetPositionAndRotation(entry->body_id, pos + to_jolt_r(delta), rot, JPH::EActivation::DontActivate);
    }


    void plugin::rotate(body_handle h, const glm::quat& delta) {

        if (!m_initialized)
            return;

        body_entry* entry = m_bodies.get(h);
        if (!entry)
            return;

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        const JPH::RVec3 pos = bi.GetPosition(entry->body_id);
        const JPH::Quat rot = bi.GetRotation(entry->body_id);

        // Applied in world space: delta * rot. Local-space rotation would be rot * delta; the contract doesn't specify, v0 picks world-space
        bi.SetPositionAndRotation(entry->body_id, pos, to_jolt(delta) * rot, JPH::EActivation::DontActivate);
    }


    void plugin::set_body_transforms(std::span<const body_handle> bodies, std::span<const glm::vec3> positions, 
        std::span<const glm::quat> rotations) {

        assert(bodies.size() == positions.size() &&
            bodies.size() == rotations.size() &&
            "set_body_transforms: span size mismatch");
        if (!m_initialized)
            return;

        const size_t n = std::min({bodies.size(), positions.size(), rotations.size()});
        JPH::BodyInterface& bi = m_system->GetBodyInterface();

        for (size_t index = 0; index < n; index++) {
            body_entry* entry = m_bodies.get(bodies[index]);
            if (!entry)
                continue;
            bi.SetPositionAndRotation(entry->body_id, to_jolt_r(positions[index]), to_jolt(rotations[index]), JPH::EActivation::DontActivate);
        }
    }


    void plugin::set_linear_velocities(std::span<const body_handle> bodies, std::span<const glm::vec3> velocities) {

        assert(bodies.size() == velocities.size() && "set_linear_velocities: span size mismatch");
        if (!m_initialized)
            return;

        const size_t n = std::min(bodies.size(), velocities.size());
        JPH::BodyInterface& bi = m_system->GetBodyInterface();

        for (size_t index = 0; index < n; index++) {
            body_entry* entry = m_bodies.get(bodies[index]);
            if (!entry)
                continue;
            bi.SetLinearVelocity(entry->body_id, to_jolt(velocities[index]));
        }
    }


    void plugin::set_angular_velocities(std::span<const body_handle> bodies, std::span<const glm::vec3> velocities) {

        assert(bodies.size() == velocities.size() && "set_angular_velocities: span size mismatch");
        if (!m_initialized)
            return;

        const size_t n = std::min(bodies.size(), velocities.size());
        JPH::BodyInterface& bi = m_system->GetBodyInterface();

        for (size_t index = 0; index < n; index++) {
            body_entry* entry = m_bodies.get(bodies[index]);
            if (!entry)
                continue;
            bi.SetAngularVelocity(entry->body_id, to_jolt(velocities[index]));
        }
    }


    void plugin::apply_force(body_handle h, const glm::vec3& force, std::optional<glm::vec3> point) {

        if (!m_initialized) return;
        body_entry* entry = m_bodies.get(h);
        if (!entry) return;

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        wake_if_dynamic(bi, entry->body_id);

        if (point)
            bi.AddForce(entry->body_id, to_jolt(force), to_jolt_r(*point));
        else
            bi.AddForce(entry->body_id, to_jolt(force));
    }


    void plugin::apply_impulse(body_handle h, const glm::vec3& impulse, std::optional<glm::vec3> point) {

        if (!m_initialized) return;
        body_entry* entry = m_bodies.get(h);
        if (!entry) return;

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        wake_if_dynamic(bi, entry->body_id);

        if (point)
            bi.AddImpulse(entry->body_id, to_jolt(impulse), to_jolt_r(*point));
        else
            bi.AddImpulse(entry->body_id, to_jolt(impulse));
    }


    void plugin::apply_torque(body_handle h, const glm::vec3& torque) {

        if (!m_initialized) return;
        body_entry* entry = m_bodies.get(h);
        if (!entry) return;

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        wake_if_dynamic(bi, entry->body_id);
        bi.AddTorque(entry->body_id, to_jolt(torque));
    }


    void plugin::apply_angular_impulse(body_handle h, const glm::vec3& imp) {

        if (!m_initialized) return;
        body_entry* entry = m_bodies.get(h);
        if (!entry) return;

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        wake_if_dynamic(bi, entry->body_id);
        bi.AddAngularImpulse(entry->body_id, to_jolt(imp));
    }


    void plugin::set_active(body_handle h, bool active) {

        if (!m_initialized)
            return;

        body_entry* entry = m_bodies.get(h);
        if (!entry)
            return;

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        const JPH::EMotionType mt = bi.GetMotionType(entry->body_id);
        if (mt == JPH::EMotionType::Static)
            return;   // static bodies are always inactive; both calls would no-op

        if (active)
            bi.ActivateBody(entry->body_id);
        else
            bi.DeactivateBody(entry->body_id);
    }


    bool plugin::is_active(body_handle h) const {

        if (!m_initialized)
            return false;

        const body_entry* entry = m_bodies.get(h);
        if (!entry)
            return false;

        return m_system->GetBodyInterface().IsActive(entry->body_id);
    }


    ray_cast_hit plugin::ray_cast(const glm::vec3& origin, const glm::vec3& direction, f32 max_distance, const query_filter& filter) const {

        ray_cast_hit out{};
        if (!m_initialized || !m_system)
            return out;

        assert(std::abs(glm::length(direction) - 1.f) < 1e-3f && "ray direction must be unit length");

        const JPH::RRayCast ray(to_jolt_r(origin), to_jolt(direction) * max_distance);

        accept_all_bp_filter bp;
        query_layer_filter layer(m_layers, filter.layer_mask);
        query_body_filter body_filter(m_system->GetBodyInterface(), filter.ignore, filter.hit_sensors);

        JPH::RayCastResult result;
        if (!m_system->GetNarrowPhaseQuery().CastRay(ray, result, bp, layer, body_filter))
            return out;

        out.hit = true;
        out.distance = result.mFraction * max_distance;
        out.position = from_jolt(ray.GetPointOnRay(result.mFraction));

        JPH::BodyLockRead lock(m_system->GetBodyLockInterface(), result.mBodyID);
        if (lock.Succeeded()) {
            const JPH::Body& jolt_body = lock.GetBody();
            out.body = unpack_body_handle(jolt_body.GetUserData());
            if (const body_entry* e = m_bodies.get(out.body)) {
                out.entity = e->entity;
                out.user_tag = e->user_tag;
            }
            out.normal = from_jolt(jolt_body.GetWorldSpaceSurfaceNormal(
                result.mSubShapeID2, ray.GetPointOnRay(result.mFraction)));
        }

        return out;
    }


    size_t plugin::ray_cast_multi(const glm::vec3& origin, const glm::vec3& direction, f32 max_distance,
        std::span<ray_cast_hit> out, const query_filter& filter) const {

        if (!m_initialized || !m_system || out.empty())
            return 0;

        assert(std::abs(glm::length(direction) - 1.f) < 1e-3f && "ray direction must be unit length");

        const JPH::RRayCast ray(to_jolt_r(origin), to_jolt(direction) * max_distance);
        JPH::RayCastSettings settings;
        settings.SetBackFaceMode(JPH::EBackFaceMode::IgnoreBackFaces);

        JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;

        accept_all_bp_filter bp;
        query_layer_filter layer(m_layers, filter.layer_mask);
        query_body_filter body_filter(m_system->GetBodyInterface(), filter.ignore, filter.hit_sensors);

        m_system->GetNarrowPhaseQuery().CastRay(ray, settings, bp, layer, body_filter, collector);

        const auto& hits = collector.mHits;
        const size_t n = std::min(hits.size(), out.size());

        JPH::BodyLockInterface& lock_iface = m_system->GetBodyLockInterface();

        // Contract §Queries — Multi-hit: ordering is unspecified. We keep
        // Jolt's discovery order (no sort). Callers needing the nearest hit
        // call ray_cast() instead.
        for (size_t i = 0; i < n; ++i) {
            const JPH::RayCastResult& result = hits[i];

            out[i] = ray_cast_hit{};
            out[i].hit = true;
            out[i].distance = result.mFraction * max_distance;
            out[i].position = from_jolt(ray.GetPointOnRay(result.mFraction));

            JPH::BodyLockRead lock(lock_iface, result.mBodyID);
            if (lock.Succeeded()) {
                const JPH::Body& jolt_body = lock.GetBody();
                out[i].body = unpack_body_handle(jolt_body.GetUserData());
                if (const body_entry* e = m_bodies.get(out[i].body)) {
                    out[i].entity = e->entity;
                    out[i].user_tag = e->user_tag;
                }
                out[i].normal = from_jolt(jolt_body.GetWorldSpaceSurfaceNormal(
                    result.mSubShapeID2, ray.GetPointOnRay(result.mFraction)));
            }
        }

        return n;
    }


    size_t plugin::overlap_sphere(const glm::vec3& center, f32 radius,
        std::span<overlap_hit> out, const query_filter& filter) const {

        if (!m_initialized || !m_system || out.empty())
            return 0;

        const JPH::SphereShape sphere(radius);

        JPH::CollideShapeSettings settings;
        JPH::AllHitCollisionCollector<JPH::CollideShapeBodyCollector> collector;

        accept_all_bp_filter bp;
        query_layer_filter layer(m_layers, filter.layer_mask);
        query_body_filter body_filter(m_system->GetBodyInterface(), filter.ignore, filter.hit_sensors);

        const JPH::RMat44 transform = JPH::RMat44::sTranslation(to_jolt_r(center));

        m_system->GetNarrowPhaseQuery().CollideShape(
            &sphere,
            JPH::Vec3::sReplicate(1.f),
            transform,
            settings,
            to_jolt_r(center),
            bp,
            layer,
            body_filter,
            collector);

        const auto& hits = collector.mHits;
        const size_t n = std::min(hits.size(), out.size());

        JPH::BodyLockInterface& lock_iface = m_system->GetBodyLockInterface();

        for (size_t i = 0; i < n; ++i) {
            out[i] = overlap_hit{};
            JPH::BodyLockRead lock(lock_iface, hits[i]);
            if (lock.Succeeded()) {
                const JPH::Body& jolt_body = lock.GetBody();
                out[i].body = unpack_body_handle(jolt_body.GetUserData());
                if (const body_entry* e = m_bodies.get(out[i].body)) {
                    out[i].entity = e->entity;
                    out[i].user_tag = e->user_tag;
                }
            }
        }

        return n;
    }


    size_t plugin::overlap_box(const glm::vec3& center, const glm::quat& rotation,
        const glm::vec3& half_extents, std::span<overlap_hit> out, const query_filter& filter) const {

        if (!m_initialized || !m_system || out.empty())
            return 0;

        const JPH::BoxShape box(to_jolt(half_extents));

        JPH::CollideShapeSettings settings;
        JPH::AllHitCollisionCollector<JPH::CollideShapeBodyCollector> collector;

        accept_all_bp_filter bp;
        query_layer_filter layer(m_layers, filter.layer_mask);
        query_body_filter body_filter(m_system->GetBodyInterface(), filter.ignore, filter.hit_sensors);

        const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(
            to_jolt(rotation), to_jolt_r(center));

        m_system->GetNarrowPhaseQuery().CollideShape(
            &box,
            JPH::Vec3::sReplicate(1.f),
            transform,
            settings,
            to_jolt_r(center),
            bp,
            layer,
            body_filter,
            collector);

        const auto& hits = collector.mHits;
        const size_t n = std::min(hits.size(), out.size());

        JPH::BodyLockInterface& lock_iface = m_system->GetBodyLockInterface();

        for (size_t i = 0; i < n; ++i) {
            out[i] = overlap_hit{};
            JPH::BodyLockRead lock(lock_iface, hits[i]);
            if (lock.Succeeded()) {
                const JPH::Body& jolt_body = lock.GetBody();
                out[i].body = unpack_body_handle(jolt_body.GetUserData());
                if (const body_entry* e = m_bodies.get(out[i].body)) {
                    out[i].entity = e->entity;
                    out[i].user_tag = e->user_tag;
                }
            }
        }

        return n;
    }


    void plugin::enable_contact_events(body_handle h, contact_event_mask mask) {

        if (!m_initialized)
            return;
        if (body_entry* entry = m_bodies.get(h))
            entry->contact_mask = mask;
    }


    std::span<const contact_event> plugin::drain_contact_events() { return m_contacts.drain(); }


    physics_stats plugin::get_stats() const noexcept {

        physics_stats stats{};
        if (m_initialized && m_system) {
            stats.body_count = m_system->GetNumBodies();
            stats.active_body_count = m_system->GetNumActiveBodies(JPH::EBodyType::RigidBody);
            stats.shape_count = m_shapes.size();
            stats.contact_pair_count = m_contacts.active_pair_count();
        }
        stats.substeps_last_frame = m_substeps_last_frame;
        stats.dropped_contact_events = m_contacts.dropped_count();
        stats.step_ms_last = m_step_ms_last;
        stats.step_ms_avg = m_step_ms_avg;
        return stats;
    }

    // CLASS PROTECTED =================================================================================================

    // CLASS PRIVATE ===================================================================================================

    void wake_if_dynamic(JPH::BodyInterface& bi, JPH::BodyID id) {

        if (bi.GetMotionType(id) == JPH::EMotionType::Dynamic)
            bi.ActivateBody(id);
    }

}
