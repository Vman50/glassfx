// @name    film_grain
// @pass    surface
// @params  luma_grain=0.05 chroma_grain=0.02 vignette_strength=0.4

void main() {
    vec2 uv = v_uv;
    vec4 col = texture(u_tex, uv);

    float t = fract(u_time * 0.03);
    float t2 = fract(u_time * 0.017 + 0.5);

    // Animated luma grain
    float g1 = texture(u_noise, uv * 6.0 + vec2(t, t * 0.7)).r;
    float g2 = texture(u_noise, uv * 8.0 + vec2(t2 * 0.9, t2)).r;
    float lumaGrain = (g1 - 0.5) * u_luma_grain;

    // Chroma grain (separate per channel)
    float cr = (texture(u_noise, uv * 5.0 + vec2(t,  0.1)).r - 0.5) * u_chroma_grain;
    float cg = (texture(u_noise, uv * 5.0 + vec2(0.3, t )).r - 0.5) * u_chroma_grain;
    float cb = (texture(u_noise, uv * 5.0 + vec2(t2, 0.6)).r - 0.5) * u_chroma_grain;

    col.r = clamp(col.r + lumaGrain + cr, 0.0, 1.0);
    col.g = clamp(col.g + lumaGrain + cg, 0.0, 1.0);
    col.b = clamp(col.b + lumaGrain + cb, 0.0, 1.0);

    // Radial vignette
    float dist = length(uv - 0.5) * 2.0;
    float vig = 1.0 - smoothstep(0.6, 1.4, dist) * u_vignette_strength;
    col.rgb *= vig;

    fragColor = vec4(col.rgb, u_alpha);
}
