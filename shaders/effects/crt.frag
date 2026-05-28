// @name    crt
// @pass    surface
// @params  curvature=0.08 scanline_intensity=0.25 mask_strength=0.3 glow_amount=0.15

vec2 barrelDistort(vec2 uv, float k) {
    vec2 c = uv - 0.5;
    float r2 = dot(c, c);
    return uv + c * (r2 * k);
}

void main() {
    vec2 uv = barrelDistort(v_uv, u_curvature);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        fragColor = vec4(0.0, 0.0, 0.0, u_alpha);
        return;
    }

    vec2 bgUv = barrelDistort(v_uv, u_curvature * 0.5);
    vec4 col = texture(u_tex, uv);
    vec4 bg  = texture(u_background, bgUv);

    // Scanlines
    float scan = sin(uv.y * u_resolution.y * 3.14159) * 0.5 + 0.5;
    scan = 1.0 - u_scanline_intensity * (1.0 - scan);
    col.rgb *= scan;
    bg.rgb  *= scan;

    // RGB tricolor shadow mask
    float px = mod(uv.x * u_resolution.x, 3.0);
    vec3 mask = vec3(1.0);
    if (px < 1.0)      mask = vec3(1.0, 1.0 - u_mask_strength, 1.0 - u_mask_strength);
    else if (px < 2.0) mask = vec3(1.0 - u_mask_strength, 1.0, 1.0 - u_mask_strength);
    else               mask = vec3(1.0 - u_mask_strength, 1.0 - u_mask_strength, 1.0);
    col.rgb *= mask;

    // Phosphor glow
    float luma = dot(col.rgb, vec3(0.299, 0.587, 0.114));
    float glowMask = smoothstep(0.5, 0.9, luma);
    vec3 glow = col.rgb * glowMask * u_glow_amount;
    // Blur glow via 5-tap
    vec2 dx = vec2(2.0 / u_resolution.x, 0.0);
    vec2 dy = vec2(0.0, 2.0 / u_resolution.y);
    vec3 blurred = (texture(u_tex, uv+dx).rgb + texture(u_tex, uv-dx).rgb +
                    texture(u_tex, uv+dy).rgb + texture(u_tex, uv-dy).rgb) * 0.25;
    float blurLuma = dot(blurred, vec3(0.299, 0.587, 0.114));
    glow += blurred * smoothstep(0.5, 0.9, blurLuma) * u_glow_amount;

    vec3 finalCol = mix(bg.rgb, col.rgb, col.a);
    finalCol += glow;
    fragColor = vec4(finalCol, u_alpha);
}
