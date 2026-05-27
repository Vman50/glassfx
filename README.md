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

# Set a shader parameter
hyprctl dispatch glassfx_param "shader:liquid_glass param:refraction value:0.6"

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

## Custom shaders

Place `.frag` files in `~/.config/hypr/shaders/glassfx/`. They are loaded on startup and
hot-reloaded when changed.

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
