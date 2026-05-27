// @name    pixelate
// @pass    surface
// @params  block_size=8.0

void main() {
    vec2 blockUv = floor(v_uv * u_resolution / u_block_size) * u_block_size / u_resolution;
    blockUv += (u_block_size * 0.5) / u_resolution;
    vec4 col = texture(u_tex, blockUv);
    fragColor = vec4(col.rgb, col.a * u_alpha);
}
