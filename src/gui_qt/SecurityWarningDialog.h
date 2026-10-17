#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QString>
#include <memory>

class DownloadTask;

class SecurityWarningDialog : public QDialog {
    Q_OBJECT

public:
    enum class Mode {
        PreDownload,  // Before download starts (SmartScreen / link reputation alert)
        PostDownload  // After file downloaded (Antivirus engine detected threat payload)
    };

    explicit SecurityWarningDialog(Mode mode,
                                  const QString& target,
                                  const QString& threatOrDetails,
                                  const QString& secondaryInfo = QString(),
                                  QWidget* parent = nullptr);
    ~SecurityWarningDialog() override = default;

    bool isAlwaysTrustDomainChecked() const;

private slots:
    void onOverrideClicked();
    void onActionCancelOrDeleteClicked();

private:
    void setupUi();

    Mode m_mode;
    QString m_target;
    QString m_threatOrDetails;
    QString m_secondaryInfo;

    QCheckBox* m_alwaysTrustCheck{nullptr};
    QPushButton* m_safeActionBtn{nullptr};
    QPushButton* m_overrideBtn{nullptr};
};
