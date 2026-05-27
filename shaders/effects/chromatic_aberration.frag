// @name    chromatic_aberration
// @pass    surface
// @params  offset=0.004 edge_falloff=0.15

void main() {
    vec2 uv = v_uv;
    float dist = length(uv - 0.5);
    float edgeWeight = 1.0 - smoothstep(u_edge_falloff, 0.5, dist);
    float strength = u_offset * edgeWeight;

    vec2 dir = normalize(uv - 0.5 + 0.0001);
    float r = texture(u_tex, uv + dir * strength).r;
    float g = texture(u_tex, uv).g;
    float b = texture(u_tex, uv - dir * strength).b;
    float a = texture(u_tex, uv).a;

    fragColor = vec4(r, g, b, a * u_alpha);
}
