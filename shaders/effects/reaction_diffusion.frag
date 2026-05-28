// @name    reaction_diffusion
// @pass    surface
// @params  feed=0.055 kill=0.062 diffuse_a=1.0 diffuse_b=0.5 color_a=#000000 color_b=#ffffff

// u_state is the previous state texture (ping-ponged from C++).
// u_state_pass = 1 during the state-update render, 0 during the display render.
// Gray-Scott reaction-diffusion: A in .r, B in .g.

uniform sampler2D u_state;

float lapl(sampler2D tex, vec2 uv, vec2 px, int ch) {
    vec4 c  = texture(tex, uv);
    vec4 n  = texture(tex, uv + vec2(0.0, px.y));
    vec4 s  = texture(tex, uv - vec2(0.0, px.y));
    vec4 e  = texture(tex, uv + vec2(px.x, 0.0));
    vec4 w  = texture(tex, uv - vec2(px.x, 0.0));
    vec4 ne = texture(tex, uv + px);
    vec4 sw = texture(tex, uv - px);
    vec4 se = texture(tex, uv + vec2(px.x, -px.y));
    vec4 nw = texture(tex, uv + vec2(-px.x, px.y));
    float center = (ch == 0) ? c.r : c.g;
    float sum    = (ch == 0)
        ? (0.2*(n.r+s.r+e.r+w.r) + 0.05*(ne.r+sw.r+se.r+nw.r))
        : (0.2*(n.g+s.g+e.g+w.g) + 0.05*(ne.g+sw.g+se.g+nw.g));
    return sum - center;
}

void main() {
    vec2 uv = v_uv;
    vec2 px = 1.0 / u_surface_size;

    vec2 prev = texture(u_state, uv).rg;
    float A = prev.r;
    float B = prev.g;

    float lA = lapl(u_state, uv, px, 0);
    float lB = lapl(u_state, uv, px, 1);

    float reaction = A * B * B;
    float newA = clamp(A + (u_diffuse_a * lA - reaction + u_feed * (1.0 - A)) * 0.5, 0.0, 1.0);
    float newB = clamp(B + (u_diffuse_b * lB + reaction - (u_kill + u_feed) * B) * 0.5, 0.0, 1.0);

    if (u_state_pass == 1) {
        // Persist new state for the next frame.
        fragColor = vec4(newA, newB, 0.0, 1.0);
        return;
    }

    // Display pass: composite simulation color over the window.
    float t = clamp(newB * 3.0, 0.0, 1.0);
    vec3 rdColor = mix(u_color_a.rgb, u_color_b.rgb, t);
    vec4 win = texture(u_tex, uv);
    vec3 result = mix(rdColor, win.rgb, win.a);
    fragColor = vec4(result, u_alpha);
}
