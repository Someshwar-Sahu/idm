#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QStringList>
#include <string>
#include "../config/SettingsManager.h"

class DownloadInfoDialog : public QDialog {
    Q_OBJECT

public:
    explicit DownloadInfoDialog(const QString& initialUrl = QString(),
                                const QStringList& queues = {},
                                SettingsManager* settings = nullptr,
                                QWidget* parent = nullptr);

    // Getters for chosen download parameters
    std::string getUrl() const;
    std::string getDestinationPath() const;
    std::string getCategory() const;
    std::string getSelectedQueue() const;
    bool shouldStartImmediately() const;
    bool isSoundMuted() const;
    bool isSecurityOverride() const { return m_securityOverride; }

    void setCustomFilename(const QString& filename);
    void setFileSize(uint64_t bytes);
    void setHttpContext(const QString& cookies, const QString& referer, const QString& userAgent);
    void probeUrlAsync(const QString& urlStr);
    void setAvailableQueues(const QStringList& queues);

private slots:
    void onUrlChanged(const QString& newUrl);
    void onBrowseClicked();
    void onCategoryChanged(const QString& category);
    void onStartClicked();
    void onDownloadLaterClicked();

private:
    void setupUi();
    void autoDetectFileNameAndCategory(const QString& url);

    QLineEdit* m_urlEdit{nullptr};
    QComboBox* m_categoryCombo{nullptr};
    QComboBox* m_queueCombo{nullptr};
    QLineEdit* m_savePathEdit{nullptr};
    QPushButton* m_browseBtn{nullptr};
    QLabel* m_fileSizeLabel{nullptr};
    QCheckBox* m_muteSoundCheck{nullptr};

    QPushButton* m_startBtn{nullptr};
    QPushButton* m_laterBtn{nullptr};
    QPushButton* m_cancelBtn{nullptr};

    bool m_startImmediately{true};
    bool m_securityOverride{false};
    SettingsManager* m_settings{nullptr};
    QStringList m_availableQueues;
    QString m_cookies;
    QString m_referer;
    QString m_userAgent;
};

