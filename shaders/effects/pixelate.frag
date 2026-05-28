// @name    pixelate
// @pass    surface
// @params  block_size=8.0

void main() {
    float block = max(u_block_size, 1.0);
    vec2 blockUv = floor(v_uv * u_resolution / block) * block / u_resolution;
    blockUv += (block * 0.5) / u_resolution;
    vec4 col = texture(u_tex, blockUv);
    fragColor = vec4(col.rgb, col.a * u_alpha);
}
