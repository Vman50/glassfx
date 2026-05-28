#include "GlassFXItem.hpp"
#include "GlassFXRenderer.hpp"

GlassFXItem::GlassFXItem(QQuickItem* parent) : QQuickFramebufferObject(parent) {
    setTextureFollowsItemSize(true);
    setMirrorVertically(false);
}

QQuickFramebufferObject::Renderer* GlassFXItem::createRenderer() const {
    return new GlassFXRenderer();
}

void GlassFXItem::setShaderName(const QString& v) {
    if (m_shaderName == v) return;
    m_shaderName = v;
    emit shaderNameChanged();
    update();
}

void GlassFXItem::setAlpha(qreal v) {
    if (qFuzzyCompare(m_alpha, v)) return;
    m_alpha = v;
    emit alphaChanged();
    update();
}

void GlassFXItem::setFocused(bool v) {
    if (m_focused == v) return;
    m_focused = v;
    emit focusedChanged();
    update();
}

void GlassFXItem::setMousePos(const QPointF& v) {
    if (m_mousePos == v) return;
    m_mousePos = v;
    emit mousePosChanged();
    update();
}

void GlassFXItem::setParam(const QString& name, const QString& value) {
    m_pendingParams.append({name, value});
    update();
}

QList<GlassFXItem::ParamChange> GlassFXItem::takeParamChanges() {
    return std::exchange(m_pendingParams, {});
}
