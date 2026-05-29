// @name    liquid_glass
// @pass    surface
// @params  refraction=0.35 rim_strength=2.8 specular_strength=1.3 aberration=0.007 frost=0.25

vec3 sampleBg(vec2 uv) {
    return texture(u_background, clamp(uv, 0.001, 0.999)).rgb;
}

void main() {
    vec2 uv = v_uv;
    vec2 toCenter = uv - 0.5;
    float edgeDist = length(toCenter);
    vec2 edgeDir = edgeDist > 0.001 ? normalize(toCenter) : vec2(0.0);

    // ── Lens warp: radial, quadratic — zero at center, strong at rim ──────────
    vec2 warp = edgeDir * edgeDist * edgeDist * u_refraction;

    // ── Chromatic dispersion: radially stronger at edges ──────────────────────
    float aberr = u_aberration * edgeDist * edgeDist;
    vec2 aOff = edgeDir * aberr;

    vec3 bg = vec3(
        sampleBg(uv + warp + aOff).r,
        sampleBg(uv + warp).g,
        sampleBg(uv + warp - aOff).b
    );

    // ── Frost: background tint + slight lightening toward frosted white ────────
    // Sample a few background points for dominant hue
    vec3 bgTint = (sampleBg(vec2(0.25, 0.25)) + sampleBg(vec2(0.75, 0.25))
                 + sampleBg(vec2(0.5,  0.75))) / 3.0;
    // Frost blends the warped bg toward a softened, slightly brightened version
    vec3 frost = mix(bg, bgTint * 1.25 + vec3(0.12), u_frost);

    // ── Edge SDF ──────────────────────────────────────────────────────────────
    float ex = min(uv.x, 1.0 - uv.x);
    float ey = min(uv.y, 1.0 - uv.y);
    float edgeSDF = min(ex, ey);

    // ── Rim glow — the primary glass signature ────────────────────────────────
    // Bright outer rim where the glass edge catches light
    float rimOuter = pow(1.0 - smoothstep(0.0, 0.028, edgeSDF), 1.8);

    // Subtle inner shadow just behind the rim — creates glass thickness feel
    float rimShadow = smoothstep(0.0, 0.018, edgeSDF)
                    * (1.0 - smoothstep(0.018, 0.055, edgeSDF)) * 0.35;

    // ── Hemisphere normal ─────────────────────────────────────────────────────
    vec2 p = toCenter * 2.0;
    float hz = sqrt(max(0.0, 1.0 - dot(p, p)));
    vec3 normal  = normalize(vec3(p, hz));
    vec3 viewDir = vec3(0.0, 0.0, 1.0);
    float NdotV  = max(0.0, dot(normal, viewDir));

    // ── Ambient specular: overhead light (top gleam, always present) ──────────
    vec3 ambLight = normalize(vec3(0.1, 0.7, 1.4));
    vec3 ambHalf  = normalize(ambLight + viewDir);
    float aNdotH  = max(0.0, dot(normal, ambHalf));
    float ambSpec = pow(aNdotH, 18.0) * 0.9;
    float ambSoft = pow(aNdotH,  3.0) * 0.35;  // wide glow across upper surface

    // ── Mouse-tracking specular: secondary highlight that follows cursor ───────
    vec2 mouseUv  = (u_mouse - u_surface_pos) / max(u_surface_size, vec2(1.0));
    vec3 mLight   = normalize(vec3((mouseUv - 0.5) * 2.0, 1.5));
    vec3 mHalf    = normalize(mLight + viewDir);
    float mNdotH  = max(0.0, dot(normal, mHalf));
    float mSpec   = pow(mNdotH, 26.0) * 0.6;
    float mSoft   = pow(mNdotH,  4.0) * 0.2;

    // ── Fresnel: edges reflect more ───────────────────────────────────────────
    float fresnel = pow(1.0 - NdotV, 3.0) * 0.45;

    float totalSpec = (ambSpec + ambSoft + mSpec + mSoft + fresnel) * u_specular_strength;

    // ── Compose glass surface ─────────────────────────────────────────────────
    vec3 glassColor = frost - vec3(rimShadow);

    // ── Composite terminal content over glass ──────────────────────────────────
    vec4 win   = texture(u_tex, uv);
    vec3 final = mix(glassColor, win.rgb, win.a);

    // Rim and specular sit on top of everything — visible over text too
    final += vec3(rimOuter) * u_rim_strength;
    final += vec3(totalSpec);

    fragColor = vec4(final, u_alpha);
}
