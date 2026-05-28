#pragma once

#include <QQuickFramebufferObject>
#include <QPointF>
#include <QString>
#include <QSizeF>

class GlassFXItem : public QQuickFramebufferObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString  shaderName READ shaderName WRITE setShaderName NOTIFY shaderNameChanged)
    Q_PROPERTY(qreal    alpha      READ alpha      WRITE setAlpha      NOTIFY alphaChanged)
    Q_PROPERTY(bool     focused    READ focused    WRITE setFocused    NOTIFY focusedChanged)
    Q_PROPERTY(QPointF  mousePos   READ mousePos   WRITE setMousePos   NOTIFY mousePosChanged)

public:
    explicit GlassFXItem(QQuickItem* parent = nullptr);

    Renderer* createRenderer() const override;

    QString  shaderName() const { return m_shaderName; }
    qreal    alpha()      const { return m_alpha;       }
    bool     focused()    const { return m_focused;     }
    QPointF  mousePos()   const { return m_mousePos;    }

    void setShaderName(const QString& v);
    void setAlpha(qreal v);
    void setFocused(bool v);
    void setMousePos(const QPointF& v);

    Q_INVOKABLE void setParam(const QString& name, const QString& value);

    // Shader parameter staging list (name→value pairs, consumed by renderer on sync)
    struct ParamChange { QString name; QString value; };
    QList<ParamChange> takeParamChanges();

signals:
    void shaderNameChanged();
    void alphaChanged();
    void focusedChanged();
    void mousePosChanged();

private:
    QString  m_shaderName;
    qreal    m_alpha    = 1.0;
    bool     m_focused  = false;
    QPointF  m_mousePos;

    QList<ParamChange> m_pendingParams;
};
