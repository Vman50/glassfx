// @name    desaturate
// @pass    surface
// @params  amount=0.6 tint=#ffffff

void main() {
    vec4 col = texture(u_tex, v_uv);
    float luma = dot(col.rgb, vec3(0.299, 0.587, 0.114));
    vec3 gray = vec3(luma);
    vec3 tinted = gray * u_tint.rgb;
    vec3 result = mix(col.rgb, tinted, u_amount);
    fragColor = vec4(result, col.a * u_alpha);
}
