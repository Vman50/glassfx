// @name    vignette
// @pass    surface
// @params  radius=0.75 softness=0.45 color=#000000 strength=0.6

void main() {
    vec4 col = texture(u_tex, v_uv);
    float dist = length(v_uv - 0.5) * 2.0;
    float vig = smoothstep(u_radius, u_radius - u_softness, dist);
    vec3 vigColor = mix(u_color.rgb, col.rgb, vig * u_strength + (1.0 - u_strength));
    // remap: outside radius blend toward color
    float mask = 1.0 - smoothstep(u_radius - u_softness, u_radius + u_softness * 0.5, dist) * u_strength;
    fragColor = vec4(col.rgb * mask + u_color.rgb * (1.0 - mask), col.a * u_alpha);
}
