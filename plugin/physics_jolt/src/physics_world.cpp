
#include "util/pch.h"
#include "util/jolt_pch.h"
#include "physics_world.h"



// FORWARD DECLARATIONS ================================================================================================

namespace GLT::physics::jolt {

    // CONSTANTS =======================================================================================================

    // MACROS ==========================================================================================================

    // TYPES ===========================================================================================================

    namespace {

        // Only one broadphase layer exists, so no broadphase filtering is needed — the object-layer filter does the real work
        struct accept_all_bp_filter final : JPH::BroadPhaseLayerFilter {

            bool ShouldCollide(JPH::BroadPhaseLayer) const override { return true; }
        };


        // First query rule: (filter.layer_mask & body.object_layer) != 0
        // Jolt hands us the interned ObjectLayer id; we look up the raw object_layer and AND against the caller's mask
        class query_layer_filter final : public JPH::ObjectLayerFilter {
        public:

            query_layer_filter(const layer_registry& layers, u32 layer_mask) noexcept
                : m_layers(layers), m_layer_mask(layer_mask) {}

            bool ShouldCollide(JPH::ObjectLayer layer) const override { return (m_layer_mask & m_layers.object_layer_of(layer)) != 0; }

        private:

            const layer_registry&   m_layers;
            u32                     m_layer_mask;

        };


        // Second and third query rules:
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

            const JPH::BodyInterface&   m_bi;
            body_handle                 m_ignore;
            bool                        m_hit_sensors;

        };

    }

    // STATIC VARIABLES ================================================================================================

    // INTERNAL TEMPLATE DECLARATION ===================================================================================

    // INTERNAL FUNCTION DECLARATION ===================================================================================

    // INTERNAL TEMPLATE IMPLEMENTATION ================================================================================

    // INTERNAL FUNCTION IMPLEMENTATION ================================================================================

    // TEMPLATE IMPLEMENTATION =========================================================================================

    // FUNCTION IMPLEMENTATION =========================================================================================

    // CLASS IMPLEMENTATION ============================================================================================

    physics_world::physics_world(JPH::JobSystemThreadPool* job_system, const glm::vec3& gravity)
        : m_job_system(job_system)
        , m_gravity(gravity) {

        // Generous defaults for v0
        constexpr JPH::uint kMaxBodies = 65536;
        constexpr JPH::uint kNumBodyMutexes = 0;       // 0 = auto
        constexpr JPH::uint kMaxBodyPairs = 65536;
        constexpr JPH::uint kMaxContactConstraints = 8192;
        constexpr size_t    kTempAllocatorBytes = 16 * 1024 * 1024;

        m_system = GLT::create_unique_ref<JPH::PhysicsSystem>();
        m_system->Init(kMaxBodies, kNumBodyMutexes, kMaxBodyPairs, kMaxContactConstraints,
            m_layers, m_object_vs_bp, m_layers);
        m_system->SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
        m_system->SetContactListener(&m_contacts);

        m_temp_allocator = GLT::create_unique_ref<JPH::TempAllocatorImpl>(kTempAllocatorBytes);
    }


    physics_world::~physics_world() {

        // Detach the listener before tearing anything down.
        if (m_system)
            m_system->SetContactListener(nullptr);
        m_contacts.clear();

        // Destroy every body through our own iteration. Jolt's ~PhysicsSystem would also clean these up, but bypassing
        // destroy_body() skips the per-pair phase_end bookkeeping in contact_listener — that's the state we care about
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
        m_temp_allocator.reset();
    }

    // stepping --------------------------------------------------------------------------------------------------------

    void physics_world::step(f32 delta_time, u32 substeps) {

        const auto t0 = std::chrono::steady_clock::now();

        m_contacts.begin_step();
        m_system->Update(delta_time, substeps, m_temp_allocator.get(), m_job_system);

        const auto t1 = std::chrono::steady_clock::now();
        const f32 ms = std::chrono::duration<f32, std::milli>(t1 - t0).count();

        m_substeps_last_frame = substeps;
        m_step_ms_last = ms;
        m_step_ms_accum += ms;
        m_step_frame_count++;
        m_step_ms_avg = m_step_ms_accum / static_cast<f32>(m_step_frame_count);
    }


    glm::vec3 physics_world::get_gravity() const noexcept { return m_gravity; }


    void physics_world::set_gravity(const glm::vec3& gravity) {

        m_gravity = gravity;
        m_system->SetGravity(JPH::Vec3(gravity.x, gravity.y, gravity.z));
    }

    // shapes ----------------------------------------------------------------------------------------------------------

    std::expected<shape_handle, physics_error> physics_world::create_shape(const shape_desc& desc) { return m_shapes.create(desc); }


    void physics_world::release_shape(shape_handle h) noexcept { m_shapes.release(h); }

    // bodies ----------------------------------------------------------------------------------------------------------

    std::expected<body_handle, physics_error> physics_world::create_body(const body_desc& desc) {

        // Shape resolution: stale vs. unsupported are distinct outcomes
        const shape_entry* s = m_shapes.get(desc.shape);
        if (!s)
            return std::unexpected(physics_error::stale_handle);

        if ((s->motion_mask & motion_bit(desc.motion)) == 0)
            return std::unexpected(physics_error::unsupported_feature);

        if (desc.motion == motion_type::dynamic_body && desc.mass_override == 0.f)
            return std::unexpected(physics_error::invalid_argument);

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
            case motion_type::static_body:
                settings.mMotionType = JPH::EMotionType::Static;
                break;
            case motion_type::dynamic_body:
                settings.mMotionType = JPH::EMotionType::Dynamic;
                break;
            case motion_type::kinematic_body:
                settings.mMotionType = JPH::EMotionType::Kinematic;
                break;
        }

        // Mass properties for dynamic bodies
        f32 resolved_mass = 0.f;

        if (desc.motion == motion_type::dynamic_body) {

            JPH::MassProperties mp = s->shape->GetMassProperties();

            if (!(mp.mMass > 0.0f) || !std::isfinite(mp.mMass)) {
                const JPH::AABox bounds = s->shape->GetLocalBounds();
                const JPH::Vec3 half = JPH::Vec3::sMax(bounds.GetExtent() / 2.f, JPH::Vec3::sReplicate(0.01f));
                mp = JPH::MassProperties();
                mp.SetMassAndInertiaOfSolidBox(half * 2.0f, 1.0f);      // density 1 → mass ≈ volume
            }

            const f32 density = (s->material.density > 0.0001f) ? s->material.density : 1000.0f;
            mp.ScaleToMass(mp.mMass * (density / 1000.0f));

            if (desc.mass_override > 0.0f)
                mp.ScaleToMass(desc.mass_override);

            if (mp.mMass <= 0.0f || !std::isfinite(mp.mMass))
                return std::unexpected(physics_error::invalid_argument);

            resolved_mass = mp.mMass;
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
            settings.mMassPropertiesOverride = mp;
        }

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        JPH::Body* body = bi.CreateBody(settings);
        if (!body)
            return std::unexpected(physics_error::out_of_memory);

        const JPH::BodyID body_id = body->GetID();

        body_entry entry{
            .shape = desc.shape,
            .entity = desc.entity,
            .user_tag = desc.user_tag,
            .body_id = body_id,
            .resolved_mass = resolved_mass,
            .contact_mask = desc.contact_mask,
        };
        const body_handle handle = m_bodies.insert(std::move(entry));

        body->SetUserData(pack_body_handle(handle));

        const JPH::EActivation activation = (desc.motion == motion_type::dynamic_body)
            ? JPH::EActivation::Activate
            : JPH::EActivation::DontActivate;
        bi.AddBody(body_id, activation);

        m_shapes.add_ref(desc.shape);   // debug-only bookkeeping

        return handle;
    }


    void physics_world::destroy_body(body_handle h) noexcept {

        body_entry* entry = m_bodies.get(h);
        if (!entry)
            return;

        // Emit phase_end for every active pair involving h before ripping the body out. Jolt may or may not fire
        // OnContactRemoved during RemoveBody(); either way our bookkeeping is already correct
        m_contacts.destroy_body(h);

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        bi.RemoveBody(entry->body_id);
        bi.DestroyBody(entry->body_id);

        m_shapes.dec_ref(entry->shape);
        m_bodies.remove(h);
    }


    bool physics_world::is_alive(body_handle h) const noexcept { return m_bodies.get(h) != nullptr; }

    // state reads -----------------------------------------------------------------------------------------------------

    std::optional<body_state> physics_world::get_body_state(body_handle h) const {

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


    size_t physics_world::get_body_states(std::span<const body_handle> bodies, std::span<body_state> out) const {

        assert(bodies.size() == out.size() && "get_body_states: span size mismatch");
        const size_t n = std::min(bodies.size(), out.size());

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

    // state writes ----------------------------------------------------------------------------------------------------

    void physics_world::set_body_transform(body_handle h, const glm::vec3& pos, const glm::quat& rot) {

        body_entry* entry = m_bodies.get(h);
        if (!entry)
            return;

        // Contract: writes never wake. DontActivate is exactly that.
        m_system->GetBodyInterface().SetPositionAndRotation(entry->body_id, to_jolt_r(pos), to_jolt(rot),
            JPH::EActivation::DontActivate);
    }


    void physics_world::set_linear_velocity(body_handle h, const glm::vec3& v) {

        body_entry* entry = m_bodies.get(h);
        if (!entry)
            return;

        m_system->GetBodyInterface().SetLinearVelocity(entry->body_id, to_jolt(v));
    }


    void physics_world::set_angular_velocity(body_handle h, const glm::vec3& w) {

        body_entry* entry = m_bodies.get(h);
        if (!entry)
            return;

        m_system->GetBodyInterface().SetAngularVelocity(entry->body_id, to_jolt(w));
    }


    void physics_world::translate(body_handle h, const glm::vec3& delta) {

        body_entry* entry = m_bodies.get(h);
        if (!entry)
            return;

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        const JPH::RVec3 pos = bi.GetPosition(entry->body_id);
        const JPH::Quat rot = bi.GetRotation(entry->body_id);

        bi.SetPositionAndRotation(entry->body_id, pos + to_jolt_r(delta), rot, JPH::EActivation::DontActivate);
    }


    void physics_world::rotate(body_handle h, const glm::quat& delta) {

        body_entry* entry = m_bodies.get(h);
        if (!entry)
            return;

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        const JPH::RVec3 pos = bi.GetPosition(entry->body_id);
        const JPH::Quat rot = bi.GetRotation(entry->body_id);

        // World-space rotation: delta * rot. Local-space would be rot * delta; the interface doesn't specify, v0 picks world-space
        bi.SetPositionAndRotation(entry->body_id, pos, to_jolt(delta) * rot, JPH::EActivation::DontActivate);
    }


    void physics_world::set_body_transforms(std::span<const body_handle> bodies, std::span<const glm::vec3> positions,
        std::span<const glm::quat> rotations) {

        assert(bodies.size() == positions.size() &&
            bodies.size() == rotations.size() &&
            "set_body_transforms: span size mismatch");

        const size_t n = std::min({bodies.size(), positions.size(), rotations.size()});
        JPH::BodyInterface& bi = m_system->GetBodyInterface();

        for (size_t index = 0; index < n; index++) {
            body_entry* entry = m_bodies.get(bodies[index]);
            if (!entry)
                continue;
            bi.SetPositionAndRotation(entry->body_id, to_jolt_r(positions[index]), to_jolt(rotations[index]),
                JPH::EActivation::DontActivate);
        }
    }


    void physics_world::set_linear_velocities(std::span<const body_handle> bodies, std::span<const glm::vec3> velocities) {

        assert(bodies.size() == velocities.size() && "set_linear_velocities: span size mismatch");

        const size_t n = std::min(bodies.size(), velocities.size());
        JPH::BodyInterface& bi = m_system->GetBodyInterface();

        for (size_t index = 0; index < n; index++) {
            body_entry* entry = m_bodies.get(bodies[index]);
            if (!entry)
                continue;
            bi.SetLinearVelocity(entry->body_id, to_jolt(velocities[index]));
        }
    }


    void physics_world::set_angular_velocities(std::span<const body_handle> bodies, std::span<const glm::vec3> velocities) {

        assert(bodies.size() == velocities.size() && "set_angular_velocities: span size mismatch");

        const size_t n = std::min(bodies.size(), velocities.size());
        JPH::BodyInterface& bi = m_system->GetBodyInterface();

        for (size_t index = 0; index < n; index++) {
            body_entry* entry = m_bodies.get(bodies[index]);
            if (!entry)
                continue;
            bi.SetAngularVelocity(entry->body_id, to_jolt(velocities[index]));
        }
    }

    // forces ----------------------------------------------------------------------------------------------------------

    void physics_world::apply_force(body_handle h, const glm::vec3& force, std::optional<glm::vec3> point) {

        body_entry* entry = m_bodies.get(h);
        if (!entry) return;

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        wake_if_dynamic(bi, entry->body_id);

        if (point)
            bi.AddForce(entry->body_id, to_jolt(force), to_jolt_r(*point));
        else
            bi.AddForce(entry->body_id, to_jolt(force));
    }


    void physics_world::apply_impulse(body_handle h, const glm::vec3& impulse, std::optional<glm::vec3> point) {

        body_entry* entry = m_bodies.get(h);
        if (!entry) return;

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        wake_if_dynamic(bi, entry->body_id);

        if (point)
            bi.AddImpulse(entry->body_id, to_jolt(impulse), to_jolt_r(*point));
        else
            bi.AddImpulse(entry->body_id, to_jolt(impulse));
    }


    void physics_world::apply_torque(body_handle h, const glm::vec3& torque) {

        body_entry* entry = m_bodies.get(h);
        if (!entry) return;

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        wake_if_dynamic(bi, entry->body_id);
        bi.AddTorque(entry->body_id, to_jolt(torque));
    }


    void physics_world::apply_angular_impulse(body_handle h, const glm::vec3& imp) {

        body_entry* entry = m_bodies.get(h);
        if (!entry) return;

        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        wake_if_dynamic(bi, entry->body_id);
        bi.AddAngularImpulse(entry->body_id, to_jolt(imp));
    }

    // activation ------------------------------------------------------------------------------------------------------

    void physics_world::set_active(body_handle h, bool active) {

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


    bool physics_world::is_active(body_handle h) const {

        const body_entry* entry = m_bodies.get(h);
        if (!entry)
            return false;

        return m_system->GetBodyInterface().IsActive(entry->body_id);
    }

    // queries ---------------------------------------------------------------------------------------------------------

    ray_cast_hit physics_world::ray_cast(const glm::vec3& origin, const glm::vec3& direction, f32 max_distance,
        const query_filter& filter) const {

        ray_cast_hit out{};

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

        const JPH::BodyLockInterface& lock_iface = m_system->GetBodyLockInterface();
        JPH::BodyLockRead lock(lock_iface, result.mBodyID);
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


    size_t physics_world::ray_cast_multi(const glm::vec3& origin, const glm::vec3& direction, f32 max_distance,
        std::span<ray_cast_hit> out, const query_filter& filter) const {

        if (out.empty())
            return 0;

        assert(std::abs(glm::length(direction) - 1.f) < 1e-3f && "ray direction must be unit length");

        const JPH::RRayCast ray(to_jolt_r(origin), to_jolt(direction) * max_distance);
        JPH::RayCastSettings settings;
        settings.SetBackFaceMode(JPH::EBackFaceMode::IgnoreBackFaces);

        JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;

        accept_all_bp_filter bp;
        query_layer_filter layer(m_layers, filter.layer_mask);
        query_body_filter body_filter(m_system->GetBodyInterface(), filter.ignore, filter.hit_sensors);

        m_system->GetNarrowPhaseQuery().CastRay(ray, settings, collector, bp, layer, body_filter);

        const auto&  hits = collector.mHits;
        const size_t n = std::min(hits.size(), out.size());

        const JPH::BodyLockInterface& lock_iface = m_system->GetBodyLockInterface();

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


    size_t physics_world::overlap_sphere(const glm::vec3& center, f32 radius, std::span<overlap_hit> out,
        const query_filter& filter) const {

        if (out.empty())
            return 0;

        const JPH::SphereShape sphere(radius);
        JPH::CollideShapeSettings settings;
        accept_all_bp_filter bp;
        query_layer_filter layer(m_layers, filter.layer_mask);
        query_body_filter body_filter(m_system->GetBodyInterface(), filter.ignore, filter.hit_sensors);

        const JPH::RMat44 transform = JPH::RMat44::sTranslation(to_jolt_r(center));

        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        m_system->GetNarrowPhaseQuery().CollideShape(
            &sphere,
            JPH::Vec3::sReplicate(1.f),
            transform,
            settings,
            to_jolt_r(center),
            collector,
            bp,
            layer,
            body_filter);

        const auto& hits = collector.mHits;
        const size_t n = std::min(hits.size(), out.size());
        const JPH::BodyLockInterface& lock_iface = m_system->GetBodyLockInterface();
        for (size_t i = 0; i < n; ++i) {
            out[i] = overlap_hit{};
            // hits[i] is a CollideShapeResult; the overlapping body is mBodyID2
            JPH::BodyLockRead lock(lock_iface, hits[i].mBodyID2);
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


    size_t physics_world::overlap_box(const glm::vec3& center, const glm::quat& rotation, const glm::vec3& half_extents,
        std::span<overlap_hit> out, const query_filter& filter) const {

        if (out.empty())
            return 0;

        const JPH::BoxShape box(to_jolt(half_extents));
        JPH::CollideShapeSettings settings;
        accept_all_bp_filter bp;
        query_layer_filter layer(m_layers, filter.layer_mask);
        query_body_filter body_filter(m_system->GetBodyInterface(), filter.ignore, filter.hit_sensors);

        const JPH::RMat44 transform = JPH::RMat44::sRotationTranslation(to_jolt(rotation), to_jolt_r(center));

        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        m_system->GetNarrowPhaseQuery().CollideShape(
            &box,
            JPH::Vec3::sReplicate(1.f),
            transform,
            settings,
            to_jolt_r(center),
            collector,
            bp,
            layer,
            body_filter);

        const auto& hits = collector.mHits;
        const size_t n = std::min(hits.size(), out.size());
        const JPH::BodyLockInterface& lock_iface = m_system->GetBodyLockInterface();
        for (size_t i = 0; i < n; ++i) {
            out[i] = overlap_hit{};
            JPH::BodyLockRead lock(lock_iface, hits[i].mBodyID2);
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

    // contact events --------------------------------------------------------------------------------------------------

    void physics_world::enable_contact_events(body_handle h, contact_event_mask mask) {

        if (body_entry* entry = m_bodies.get(h))
            entry->contact_mask = mask;
    }


    std::span<const contact_event> physics_world::drain_contact_events() { return m_contacts.drain(); }

    // stats -----------------------------------------------------------------------------------------------------------

    physics_stats physics_world::get_stats() const noexcept {

        physics_stats stats{};
        stats.body_count = m_system->GetNumBodies();
        stats.active_body_count = m_system->GetNumActiveBodies(JPH::EBodyType::RigidBody);
        stats.shape_count = m_shapes.size();
        stats.contact_pair_count = m_contacts.active_pair_count();
        stats.substeps_last_frame = m_substeps_last_frame;
        stats.dropped_contact_events = m_contacts.dropped_count();
        stats.step_ms_last = m_step_ms_last;
        stats.step_ms_avg = m_step_ms_avg;
        return stats;
    }

    // private helpers -------------------------------------------------------------------------------------------------

    void physics_world::wake_if_dynamic(JPH::BodyInterface& bi, JPH::BodyID id) {

        if (bi.GetMotionType(id) == JPH::EMotionType::Dynamic)
            bi.ActivateBody(id);
    }

}
