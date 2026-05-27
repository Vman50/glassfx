// @name    liquid_glass
// @pass    surface
// @params  refraction=0.4 tint=#ffffff22 tint_strength=0.15 grain_amount=0.03 specular_strength=0.4 aberration=0.003

vec3 sampleBackground(vec2 uv) {
    return texture(u_background, uv).rgb;
}

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 uv = v_uv;
    vec2 res = u_resolution;

    // Blue noise for refraction warp
    float noise1 = texture(u_noise, uv * 3.0 + u_time * 0.02).r;
    float noise2 = texture(u_noise, uv * 3.0 + u_time * 0.02 + 0.5).r;
    vec2 warp = (vec2(noise1, noise2) - 0.5) * 2.0 * u_refraction;

    // Chromatic aberration on background
    float dist = length(uv - 0.5) * 2.0;
    float aberr = u_aberration * smoothstep(0.4, 0.9, dist);
    vec2 uvBg = uv + warp;
    float bgR = sampleBackground(uvBg + vec2(aberr, 0.0)).r;
    float bgG = sampleBackground(uvBg).g;
    float bgB = sampleBackground(uvBg - vec2(aberr, 0.0)).b;
    vec3 bgColor = vec3(bgR, bgG, bgB);

    // Specular highlights (light from top-left)
    vec2 lightDir = normalize(vec2(1.0, 1.0));
    float spec1 = pow(max(0.0, dot(normalize(uv - 0.5), lightDir)), 4.0) * 0.5;
    float spec2 = pow(max(0.0, dot(normalize(uv - 0.5), lightDir)), 32.0) * 0.8;
    vec3 specular = vec3(spec1 + spec2) * u_specular_strength;

    // Tint
    vec4 tintColor = u_tint;
    vec3 glassColor = mix(bgColor, tintColor.rgb, tintColor.a * u_tint_strength);
    glassColor += specular;

    // Film grain
    float t = u_time * 1337.0;
    float grain = texture(u_noise, uv * 7.0 + vec2(fract(t * 0.01), fract(t * 0.007))).r;
    grain = (grain - 0.5) * u_grain_amount;
    glassColor += grain;

    // Composite window over glass
    vec4 winTex = texture(u_tex, uv);
    vec3 finalColor = mix(glassColor, winTex.rgb, winTex.a);
    fragColor = vec4(finalColor, u_alpha);
}
