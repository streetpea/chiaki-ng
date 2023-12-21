#pragma once

#include <qpa/qplatforminputcontext.h>
#include <qpa/qplatforminputcontextplugin_p.h>

class SDInputContextPlugin : public QPlatformInputContextPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformInputContextFactoryInterface_iid FILE "sdinput.json")

public:
    QPlatformInputContext *create(const QString &key, const QStringList &paramList) override;
};

class SDInputContext : public QPlatformInputContext
{
    Q_OBJECT

public:
    SDInputContext();
    bool isValid() const override;
    void showInputPanel() override;
    void hideInputPanel() override;
    bool isInputPanelVisible() const override;
    void setFocusObject(QObject *object) override;

private:
    bool is_visible = false;
};
