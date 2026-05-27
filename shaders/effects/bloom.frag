// @name    bloom
// @pass    fullscreen
// @params  threshold=0.7 intensity=0.4 blur_radius=8.0

void main() {
    vec2 uv = v_uv;
    vec4 col = texture(u_tex, uv);

    // Threshold pass
    float luma = dot(col.rgb, vec3(0.299, 0.587, 0.114));
    vec3 bright = col.rgb * smoothstep(u_threshold - 0.1, u_threshold + 0.1, luma);

    // Blur thresholded result (single-pass 9-tap approximation)
    vec2 px = vec2(u_blur_radius / u_resolution.x, u_blur_radius / u_resolution.y);
    vec3 bloomSum = vec3(0.0);
    float weights[9];
    weights[0] = 0.0625; weights[1] = 0.125; weights[2] = 0.0625;
    weights[3] = 0.125;  weights[4] = 0.25;  weights[5] = 0.125;
    weights[6] = 0.0625; weights[7] = 0.125; weights[8] = 0.0625;

    vec2 offsets[9];
    offsets[0] = vec2(-px.x, -px.y); offsets[1] = vec2(0.0, -px.y); offsets[2] = vec2(px.x, -px.y);
    offsets[3] = vec2(-px.x,  0.0);  offsets[4] = vec2(0.0,  0.0);  offsets[5] = vec2(px.x,  0.0);
    offsets[6] = vec2(-px.x,  px.y); offsets[7] = vec2(0.0,  px.y); offsets[8] = vec2(px.x,  px.y);

    for (int i = 0; i < 9; i++) {
        vec4 s = texture(u_tex, clamp(uv + offsets[i], 0.0, 1.0));
        float sl = dot(s.rgb, vec3(0.299, 0.587, 0.114));
        bloomSum += s.rgb * smoothstep(u_threshold - 0.1, u_threshold + 0.1, sl) * weights[i];
    }

    // Second pass for wider blur
    vec2 px2 = px * 3.0;
    for (int i = 0; i < 9; i++) {
        vec4 s = texture(u_tex, clamp(uv + offsets[i] * 3.0, 0.0, 1.0));
        float sl = dot(s.rgb, vec3(0.299, 0.587, 0.114));
        bloomSum += s.rgb * smoothstep(u_threshold - 0.1, u_threshold + 0.1, sl) * weights[i] * 0.5;
    }
    bloomSum /= 1.5;

    vec3 result = col.rgb + bloomSum * u_intensity;
    fragColor = vec4(result, col.a);
}
