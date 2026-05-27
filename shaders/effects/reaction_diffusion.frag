// @name    reaction_diffusion
// @pass    surface
// @params  feed=0.055 kill=0.062 diffuse_a=1.0 diffuse_b=0.5 color_a=#000000 color_b=#ffffff

// u_state is the previous state texture (bound from ShaderManager ping-pong)
uniform sampler2D u_state;

// Gray-Scott reaction-diffusion simulation + display pass
// When stateTex is ping-ponged, this shader both simulates and renders.
// The first 2 channels (RG) of u_state store A and B concentrations.

float laplace(sampler2D tex, vec2 uv, int ch) {
    vec2 px = 1.0 / u_surface_size;
    float center = (ch == 0) ? texture(tex, uv).r : texture(tex, uv).g;
    float n  = (ch == 0) ? texture(tex, uv + vec2(0, px.y)).r : texture(tex, uv + vec2(0, px.y)).g;
    float s  = (ch == 0) ? texture(tex, uv - vec2(0, px.y)).r : texture(tex, uv - vec2(0, px.y)).g;
    float e  = (ch == 0) ? texture(tex, uv + vec2(px.x, 0)).r : texture(tex, uv + vec2(px.x, 0)).g;
    float w  = (ch == 0) ? texture(tex, uv - vec2(px.x, 0)).r : texture(tex, uv - vec2(px.x, 0)).g;
    float ne = (ch == 0) ? texture(tex, uv + px).r : texture(tex, uv + px).g;
    float sw = (ch == 0) ? texture(tex, uv - px).r : texture(tex, uv - px).g;
    float se = (ch == 0) ? texture(tex, uv + vec2(px.x, -px.y)).r : texture(tex, uv + vec2(px.x, -px.y)).g;
    float nw = (ch == 0) ? texture(tex, uv + vec2(-px.x, px.y)).r : texture(tex, uv + vec2(-px.x, px.y)).g;
    return -center + 0.05*(n+s+e+w) + 0.2*(center) +
           0.2*(n+s+e+w) - 0.8*center + 0.05*(ne+sw+se+nw);
}

void main() {
    vec2 uv = v_uv;
    vec2 state = texture(u_state, uv).rg;
    float A = state.r;
    float B = state.g;

    float lA = laplace(u_state, uv, 0);
    float lB = laplace(u_state, uv, 1);

    float reaction = A * B * B;
    float newA = clamp(A + (u_diffuse_a * lA - reaction + u_feed * (1.0 - A)) * 0.5, 0.0, 1.0);
    float newB = clamp(B + (u_diffuse_b * lB + reaction - (u_kill + u_feed) * B) * 0.5, 0.0, 1.0);

    // Map to color gradient
    float t = clamp(newB * 3.0, 0.0, 1.0);
    vec3 rdColor = mix(u_color_a.rgb, u_color_b.rgb, t);

    // Composite with window texture
    vec4 win = texture(u_tex, uv);
    vec3 result = mix(rdColor, win.rgb, win.a * 0.5);

    // Output both simulation state (in RG) and display color (in BA... no)
    // Since we're rendering to a display fb, just output the color.
    // The ping-pong state is written separately in the state fbo.
    fragColor = vec4(result, u_alpha);

    // Write updated state to state fbo (when this shader is used as state update):
    // The C++ code runs this shader twice: once to stateFbo, once to outFbo.
    // When writing to stateFbo, we need just the new RG state.
    // We use a compile-time define to differentiate... or we always output
    // display color and C++ copies the color to both.
    // Simpler: always output display color. State update pass ignores the visual output.
    // C++ code will sample u_tex (which is stateTex[src]) and write new state as color.
    // So: when running as STATE update pass, output new state in fragColor:
    //   fragColor = vec4(newA, newB, 0.0, 1.0);
    // When running as DISPLAY pass, output visual color.
    // We signal with a uniform from C++ side, or we just always output state
    // and do a separate display-only pass. For simplicity: output state here,
    // C++ maps state colors to visuals in a secondary blit.
    // Actually: let's output state always, and in C++ do a simple display mapping
    // on the final output. This keeps the shader simple.
    fragColor = vec4(newA, newB, t, u_alpha);
}
