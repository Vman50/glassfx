// @name    heat_shimmer
// @pass    surface
// @params  amplitude=0.006 frequency=4.0 speed=1.2

float turbulence(vec2 uv, float t) {
    float v = 0.0;
    v += sin(uv.x * u_frequency + t * u_speed) * 0.5;
    v += sin(uv.y * u_frequency * 1.3 + t * u_speed * 0.8) * 0.3;
    v += sin((uv.x + uv.y) * u_frequency * 0.7 + t * u_speed * 1.1) * 0.2;
    return v;
}

void main() {
    vec2 uv = v_uv;
    float t = u_time;

    float wx = turbulence(uv, t);
    float wy = turbulence(uv + vec2(1.7, 3.1), t);

    vec2 warpedUv = uv + vec2(wx, wy) * u_amplitude;
    warpedUv = clamp(warpedUv, 0.0, 1.0);

    vec4 bg  = texture(u_background, warpedUv);
    vec4 win = texture(u_tex, uv);

    vec3 finalColor = mix(bg.rgb, win.rgb, win.a);
    fragColor = vec4(finalColor, u_alpha);
}
