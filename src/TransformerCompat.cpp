// Hyprland compiles with -fvisibility=hidden, so _ZTI18IWindowTransformer is
// not in its dynamic symbol table. By defining preWindowRender() here (the key
// function for IWindowTransformer), the compiler emits the vtable + typeinfo in
// our .so with default visibility, allowing dlopen to resolve the symbol.
#include <hyprland/src/render/Transformer.hpp>

void IWindowTransformer::preWindowRender(CSurfacePassElement::SRenderData*) {}
