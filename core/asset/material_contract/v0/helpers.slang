
import types;


vertex_output passthrough(vertex_input v) {

    vertex_output o;
    o.position_ws = v.position_ws;
    o.normal_ws   = v.normal_ws;
    o.tangent_ws  = v.tangent_ws;
    o.uv0         = v.uv0;
    o.uv1         = v.uv1;
    o.color       = v.color;
    return o;
}


surface_output make_default_surface(surface_input s) {

    surface_output o;
    o.base_color = float3(1.0, 1.0, 1.0);
    o.opacity    = 1.0;
    o.normal_ws  = s.normal_ws;
    o.metallic   = 0.0;
    o.roughness  = 0.5;
    o.emissive   = float3(0.0, 0.0, 0.0);
    o.ao         = 1.0;
    return o;
}


float4 sample_material(Texture2D t, SamplerState s, float2 uv) {

    return t.Sample(s, uv);
}


float4 sample_material_lod(Texture2D t, SamplerState s, float2 uv, float lod) {

    return t.SampleLevel(s, uv, lod);
}
