#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <cstdint>
#include <memory>
#include <optional>

#include "core/catalog/CatalogModels.h"
#include "core/catalog/ContentLanguages.h"

namespace lmp::core {
class PlaybackClock;
class SettingsStore;
namespace account {
class AccountClient;
class AccountSessionService;
} // namespace account
namespace catalog {
class CatalogClient;
class CatalogService;
class ContentLanguageController;
} // namespace catalog
} // namespace lmp::core

namespace lmp::ui {

class ToastQueue;

// The music region and recommendation languages in Settings
// (App.regionLanguages), Account mode only. The region is the Settings value
// CatalogStorefront (two lower-case letters), which travels with the account
// as a preference; changing it clears the catalogue, so Home and Browse load
// again. The languages are the account's contentLanguages preference, saved
// through core::catalog::ContentLanguageController.
//
// GUI thread only.
class RegionLanguagesViewModel final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.regionLanguages")

    // Region names, in the service's order; the current one is regionIndex.
    Q_PROPERTY(QStringList regionNames READ regionNames NOTIFY regionsChanged FINAL)
    Q_PROPERTY(int regionIndex READ regionIndex NOTIFY regionsChanged FINAL)
    Q_PROPERTY(QString regionName READ regionName NOTIFY regionsChanged FINAL)
    Q_PROPERTY(QString regionError READ regionError NOTIFY regionsChanged FINAL)

    // "auto", "custom" or "off".
    Q_PROPERTY(QString mode READ mode NOTIFY languagesChanged FINAL)
    // One map per language: code, name, nativeName (empty when it repeats
    // the name), removable.
    Q_PROPERTY(QVariantList detected READ detected NOTIFY languagesChanged FINAL)
    Q_PROPERTY(QString detectedAgo READ detectedAgo NOTIFY languagesChanged FINAL)
    Q_PROPERTY(QVariantList regionLanguages READ regionLanguages NOTIFY languagesChanged FINAL)
    Q_PROPERTY(QVariantList chosen READ chosen NOTIFY languagesChanged FINAL)
    Q_PROPERTY(bool detecting READ detecting NOTIFY languagesChanged FINAL)
    Q_PROPERTY(QString detectError READ detectError NOTIFY languagesChanged FINAL)
    // The editor: open, its search, and its groups (label, languages with
    // code, name, nativeName, checked, refused).
    Q_PROPERTY(bool editorOpen READ editorOpen NOTIFY languagesChanged FINAL)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY languagesChanged FINAL)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY languagesChanged FINAL)
    Q_PROPERTY(QString hint READ hint NOTIFY languagesChanged FINAL)
    Q_PROPERTY(QString countText READ countText NOTIFY languagesChanged FINAL)
    Q_PROPERTY(bool catalogLoading READ catalogLoading NOTIFY languagesChanged FINAL)
    Q_PROPERTY(QString catalogError READ catalogError NOTIFY languagesChanged FINAL)
    Q_PROPERTY(bool catalogEmpty READ catalogEmpty NOTIFY languagesChanged FINAL)

public:
    struct Parts {
        core::catalog::CatalogService& catalog;
        core::catalog::CatalogClient& client;
        core::account::AccountClient& accountClient;
        core::account::AccountSessionService& session;
        core::SettingsStore& settings;
        core::PlaybackClock& clock;
        ToastQueue& toasts;
    };

    // Everything in `parts` outlives this object.
    explicit RegionLanguagesViewModel(Parts parts, QObject* parent = nullptr);
    ~RegionLanguagesViewModel() override;

    [[nodiscard]] QStringList regionNames() const;
    [[nodiscard]] int regionIndex() const;
    [[nodiscard]] QString regionName() const;
    [[nodiscard]] QString regionError() const { return m_regionError; }

    [[nodiscard]] QString mode() const;
    [[nodiscard]] QVariantList detected() const;
    [[nodiscard]] QString detectedAgo() const;
    [[nodiscard]] QVariantList regionLanguages() const;
    [[nodiscard]] QVariantList chosen() const;
    [[nodiscard]] bool detecting() const;
    [[nodiscard]] QString detectError() const;
    [[nodiscard]] bool editorOpen() const;
    [[nodiscard]] QString search() const { return m_search; }
    void setSearch(const QString& search);
    [[nodiscard]] QVariantList groups() const;
    [[nodiscard]] QString hint() const;
    [[nodiscard]] QString countText() const;
    [[nodiscard]] bool catalogLoading() const { return m_catalogLoading; }
    [[nodiscard]] QString catalogError() const { return m_catalogError; }
    [[nodiscard]] bool catalogEmpty() const { return m_catalogLoaded && m_catalog.isEmpty(); }

    // Loads the regions, the language catalogue and the saved setting; then
    // detection runs when it is due. Called when Settings opens.
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void chooseRegion(int index);
    // "auto", "custom" or "off".
    Q_INVOKABLE void selectMode(const QString& mode);
    Q_INVOKABLE void toggleLanguage(const QString& code);
    Q_INVOKABLE void detect();
    Q_INVOKABLE void toggleEditor();
    Q_INVOKABLE void retryCatalog();

signals:
    void regionsChanged();
    void languagesChanged();

private:
    void loadRegions();
    void loadCatalog();
    void loadSetting();
    void onOwnerChanged();
    [[nodiscard]] QVariantList languageList(const QStringList& codes, bool removable) const;
    [[nodiscard]] core::catalog::LanguageInfo languageInfo(const QString& code) const;
    [[nodiscard]] QStringList savedRegionLanguages() const;

    core::catalog::CatalogService& m_catalogService;
    core::catalog::CatalogClient& m_client;
    core::account::AccountClient& m_accountClient;
    core::account::AccountSessionService& m_session;
    core::SettingsStore& m_settings;
    ToastQueue& m_toasts;

    QList<core::catalog::Region> m_regions;
    QString m_regionError;

    std::unique_ptr<core::catalog::ContentLanguageController> m_controller;
    std::optional<core::catalog::LanguageSetting> m_setting;
    QList<core::catalog::LanguageInfo> m_catalog;
    std::optional<core::catalog::RegionLanguageTable> m_regionTable;
    bool m_catalogLoaded = false;
    bool m_catalogLoading = false;
    QString m_catalogError;
    bool m_settingLoaded = false;
    bool m_editorOpen = false;
    QString m_search;
    // Bumped on an owner change; answers from before it are dropped.
    std::uint64_t m_epoch = 0;
};

} // namespace lmp::ui
