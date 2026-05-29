# GlassFX — Per-window GLSL shader system for Hyprland

GlassFX is a Hyprland plugin that adds a generic, extensible per-window and fullscreen GLSL
shader system. It uses `IWindowTransformer` for per-window effects and the render stage event
for fullscreen post-processing.

## Requirements

- Hyprland 0.55.2
- SceneFX 0.4
- GLES 3.2 / EGL

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cmake --install build
```

## Load with hyprpm

In `hyprland.conf`:
```ini
exec-once = hyprpm reload
```

Or load manually:
```bash
hyprctl plugin load ~/.config/hypr/plugins/libglassfx.so
```

## Configuration (hyprland.conf)

```ini
plugin {
    glassfx {
        # Fullscreen post-process shader (applied after all windows)
        fullscreen_shader = bloom

        # Auto-desaturate unfocused windows
        desaturate_unfocused = 1
        desaturate_amount    = 0.6

        # Per-window rules: "shader,class_pattern" separated by semicolons
        windowrules = liquid_glass,firefox;crt,steam;heat_shimmer,kitty
    }
}
```

## Dispatchers

```bash
# Assign shader to focused window
hyprctl dispatch glassfx_set shader:liquid_glass

# Assign to specific window by address
hyprctl dispatch glassfx_set "address:0xADDR shader:crt"

# Set a shader parameter (key:value syntax)
hyprctl dispatch glassfx_param "shader:liquid_glass param:refraction value:0.6"

# Set a shader parameter (shorthand — 3 space-separated args)
hyprctl dispatch glassfx_tune "liquid_glass refraction 0.6"

# Hot-reload all shaders from disk
hyprctl dispatch glassfx_reload

# Remove shader from focused window
hyprctl dispatch glassfx_clear

# List loaded shaders and params
hyprctl dispatch glassfx_list
```

## Shader library

| Shader | Pass | Description |
|--------|------|-------------|
| `liquid_glass` | surface | Blurred background with refraction, specular highlights, chromatic aberration |
| `crt` | surface | CRT monitor emulation: barrel distortion, scanlines, phosphor glow |
| `heat_shimmer` | surface | Animated heat distortion via turbulence |
| `film_grain` | surface | Animated film grain with luma/chroma separation and vignette |
| `chromatic_aberration` | surface | Edge-weighted RGB channel separation |
| `desaturate` | surface | Grayscale with optional tint |
| `vignette` | surface | Smooth radial darkening |
| `pixelate` | surface | Pixelation with configurable block size |
| `reaction_diffusion` | surface | Gray-Scott RD simulation (stateful, per-window) |
| `fluid_sim` | surface | Advection fluid with mouse-driven dye injection (stateful, per-window) |
| `bloom` | fullscreen | Threshold + blur additive bloom pass |

---

## Shader parameter reference

### `liquid_glass`

Apple-style glass pane effect. Lens-warps the blurred background, adds a bright rim glow,
specular highlights, and chromatic fringing at the edges.

> **Tip:** window must **not** have `opaque true` in its windowrule — that strips the alpha
> channel and makes the effect invisible.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `refraction` | `0.35` | Lens distortion strength. Background UVs are displaced radially — zero at the center, strongest at the rim. Higher values bend the background more aggressively. Range: `0.0`–`1.0`. |
| `rim_strength` | `2.8` | Brightness of the white glow at the window edge. This is the primary visual signature of glass. Values above `1.0` overbright (HDR-style). Range: `0.0`–`5.0+`. |
| `specular_strength` | `1.3` | Master multiplier for all highlight sources: the ambient top gleam, the mouse-tracking spot, and the Fresnel edge glow. `0.0` disables all specular. |
| `aberration` | `0.007` | Chromatic aberration — the R, G, B channels are offset radially by slightly different amounts near the edges, producing a prismatic fringe. `0.0` disables it. |
| `frost` | `0.25` | Frosted glass opacity. Blends the warped background toward a softened average of the surrounding scene, slightly brightened (milky-white). `0.0` = clear glass, `1.0` = fully frosted. |

```bash
# Live tune example
hyprctl dispatch glassfx_tune "liquid_glass rim_strength 3.5"
hyprctl dispatch glassfx_tune "liquid_glass frost 0.4"
```

---

### `bloom` _(fullscreen)_

Extracts bright areas of the whole screen, blurs them, and adds the result back — creates a
soft glow around highlights.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `threshold` | `0.7` | Luminance cutoff. Only pixels brighter than this contribute to bloom. `1.0` = no bloom, `0.0` = everything blooms. |
| `intensity` | `0.4` | How strongly the bloom halo is added back onto the frame. |
| `blur_radius` | `8.0` | Pixel spread radius of the bloom glow. Larger = wider halo. |

---

### `crt`

Simulates a CRT monitor with barrel distortion, scanlines, an RGB phosphor shadow mask, and a
phosphor glow on bright areas.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `curvature` | `0.08` | Barrel distortion — curves the image like a CRT tube. `0.0` = flat, higher = more curved. Areas outside the curved boundary render black. |
| `scanline_intensity` | `0.25` | Strength of horizontal dark bands every other row. `0.0` = none, `1.0` = every other row fully black. |
| `mask_strength` | `0.3` | RGB tricolor phosphor dot pattern intensity. `0.0` = off, `1.0` = heavy mask. |
| `glow_amount` | `0.15` | Phosphor glow added around bright areas. |

---

### `heat_shimmer`

Animates the background with sinusoidal turbulence to simulate rising hot air. Window content
is rendered on top; only the background shimmers.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `amplitude` | `0.006` | How far pixels are displaced (UV space). Higher = more dramatic wobble. |
| `frequency` | `4.0` | Spatial frequency of the wave pattern. Higher = tighter, smaller ripples. |
| `speed` | `1.2` | Animation speed multiplier applied to `u_time`. |

---

### `film_grain`

Adds animated photographic grain with separate luma and chroma components, plus an optional
radial vignette.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `luma_grain` | `0.05` | Brightness noise added equally to all channels — classic monochrome grain. |
| `chroma_grain` | `0.02` | Per-channel noise — adds colorful speckle on top of luma grain. |
| `vignette_strength` | `0.4` | Corner darkening intensity. `0.0` = no vignette, `1.0` = fully dark corners. |

---

### `chromatic_aberration`

Separates the R and B channels radially outward from the center, with configurable falloff.
G stays centered.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `offset` | `0.004` | Channel separation distance in UV space. Higher = more visible colour fringe. |
| `edge_falloff` | `0.15` | Normalized distance from center below which aberration fades to zero. Keeps the middle of the window clean. |

---

### `desaturate`

Converts the window toward grayscale, optionally tinted with a colour. Used by the
`desaturate_unfocused` config feature on unfocused windows.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `amount` | `0.6` | Desaturation strength. `0.0` = original colour, `1.0` = fully grayscale. |
| `tint` | `#ffffff` | Colour to mix with the gray. Default white = neutral gray. Use e.g. `#aabbff` for a cool-tinted desaturate. |

---

### `vignette`

Darkens (or colourises) window edges with a smooth radial gradient.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `radius` | `0.75` | Normalized distance from center where darkening begins. `0.5` = starts at window edge, `1.0` = starts beyond window. |
| `softness` | `0.45` | Gradient width — how gradually the effect ramps in. Higher = smoother fade. |
| `color` | `#000000` | Vignette overlay colour. Default black; try `#0000ff` for a blue tint. |
| `strength` | `0.6` | Overall opacity of the vignette layer. `0.0` = invisible, `1.0` = full overlay. |

---

### `pixelate`

Snaps the window texture to a block grid, creating a low-resolution mosaic look.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `block_size` | `8.0` | Size of each pixel block in screen pixels. `1.0` = no change, `32.0` = very chunky. |

---

### `reaction_diffusion`

Gray-Scott reaction-diffusion simulation running per-window (stateful — state accumulates
across frames). Creates organic blob/stripe/spot patterns.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `feed` | `0.055` | Feed rate of chemical A. Controls how fast the pattern grows. |
| `kill` | `0.062` | Kill rate of chemical B. Together with `feed`, determines the pattern type (spots, stripes, holes — see Gray-Scott parameter map). |
| `diffuse_a` | `1.0` | Diffusion rate of chemical A (activator). |
| `diffuse_b` | `0.5` | Diffusion rate of chemical B (inhibitor). Lower than A gives classic Turing patterns. |
| `color_a` | `#000000` | Colour of the empty (A-dominant) regions. |
| `color_b` | `#ffffff` | Colour of the filled (B-dominant) regions. |

---

### `fluid_sim`

Real-time advection fluid simulation. Move your cursor over the window to inject dye and
velocity. State is per-window and persists across frames.

| Parameter | Default | Description |
|-----------|---------|-------------|
| `viscosity` | `0.98` | Per-frame velocity damping factor. `1.0` = no damping (fluid never slows), lower values dissipate momentum faster. |
| `dye_decay` | `0.99` | Per-frame dye fade factor. Lower values make colour trails disappear faster. |
| `injection_radius` | `0.04` | Normalized radius around the cursor that injects velocity and dye. Larger = wider brush. |

---

## Custom shaders

Place `.frag` files in `~/.config/hypr/shaders/glassfx/`. They are loaded on startup and
hot-reloaded when changed (inotify watch — no restart needed).

Metadata header:
```glsl
// @name    my_effect
// @pass    surface          (or: fullscreen)
// @params  intensity=0.5 color=#ff000088
```

### Standard uniforms available in every shader

```glsl
uniform sampler2D u_tex;          // window rendered texture
uniform sampler2D u_background;   // blur output behind window
uniform sampler2D u_noise;        // 256x256 blue noise, tiled
uniform vec2      u_resolution;   // monitor resolution
uniform vec2      u_surface_pos;  // window top-left in screen space
uniform vec2      u_surface_size; // window size in pixels
uniform float     u_time;         // seconds since plugin load
uniform float     u_alpha;        // window opacity
uniform int       u_focused;      // 1 if window has keyboard focus
uniform vec2      u_mouse;        // cursor position in screen coords
in vec2 v_uv;
out vec4 fragColor;
```

### Param types

| Syntax in `@params` | GLSL uniform type | Example |
|---------------------|-------------------|---------|
| `name=0.5` | `float` | `intensity=0.5` |
| `name=1,2` | `vec2` | `offset=0.5,0.5` |
| `name=1,2,3` | `vec3` | `light=0.1,0.7,1.4` |
| `name=#rrggbb` or `name=#rrggbbaa` | `vec4` | `color=#ff000080` |
