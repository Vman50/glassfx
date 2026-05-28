#include <QQmlExtensionPlugin>
#include <qqml.h>

#include "GlassFXItem.hpp"

class GlassFXQmlPlugin : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)
public:
    void registerTypes(const char* uri) override {
        qmlRegisterType<GlassFXItem>(uri, 1, 0, "GlassFXItem");
    }
};

#include "plugin.moc"
