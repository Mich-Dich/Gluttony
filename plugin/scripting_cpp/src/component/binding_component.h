#pragma once

// The script_bindings component (core: scripting/script_binding_component.h) as a world component: payload format and
// the inspector (Details panel): add / remove scripts, pick the class, instance name, enabled, the [[=editor]] fields,
// and while the world runs scripts the live state and the [[=editor_callable]] buttons



namespace GLT::scripting_cpp {

    void register_binding_component();

    void unregister_binding_component();

}
