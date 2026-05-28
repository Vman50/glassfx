// @name    fluid_sim
// @pass    surface
// @params  viscosity=0.98 dye_decay=0.99 injection_radius=0.04

// u_state holds velocity in .rg and dye in .b.
// u_state_pass = 1 during the state update render, 0 during display.

uniform sampler2D u_state;

void main() {
    vec2 uv = v_uv;
    vec2 px = 1.0 / u_surface_size;

    vec4 prev = texture(u_state, uv);
    vec2 vel  = prev.rg;

    vec2 advUv = clamp(uv - vel * px * 2.0, vec2(0.0), vec2(1.0));
    vec4 advected = texture(u_state, advUv);
    vec2 newVel = advected.rg * u_viscosity;
    float newDye = advected.b * u_dye_decay;

    // Mouse injection in window-local UV space.
    vec2 mouseUv = clamp((u_mouse - u_surface_pos) / u_surface_size, vec2(0.0), vec2(1.0));
    float mouseDist = length(uv - mouseUv);
    if (mouseDist < u_injection_radius) {
        float strength = 1.0 - mouseDist / u_injection_radius;
        strength = strength * strength;
        vec2 injectDir = normalize(uv - mouseUv + vec2(0.0001));
        newVel += injectDir * strength * 0.05;
        newDye = max(newDye, strength);
    }

    newVel = clamp(newVel, -0.5, 0.5);
    newDye = clamp(newDye, 0.0, 1.0);

    if (u_state_pass == 1) {
        fragColor = vec4(newVel, newDye, 1.0);
        return;
    }

    // Display pass: composite colored dye over the window.
    vec3 fluidColor = mix(vec3(0.0, 0.1, 0.3), vec3(0.0, 0.8, 1.0), newDye);
    vec4 win = texture(u_tex, uv);
    vec3 result = mix(fluidColor, win.rgb, win.a);
    fragColor = vec4(result, u_alpha);
}
