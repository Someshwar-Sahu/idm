#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>

#include "../config/SettingsManager.h"

class OptionsDialog : public QDialog {
    Q_OBJECT

public:
    explicit OptionsDialog(SettingsManager* settings, QWidget* parent = nullptr);
    ~OptionsDialog() override = default;

signals:
    void settingsApplied();

private slots:
    void onBrowseDefaultDir();
    void onBrowseTempDir();
    void onBrowseAntivirus();
    void onSpeedModeChanged(int index);
    void onApplyClicked();
    void onOkClicked();
    void onAddWhitelistDomain();
    void onRemoveWhitelistEntry();

private:
    void setupUi();
    void loadSettings();
    void saveSettings();
    void refreshWhitelistTable();

    SettingsManager* m_settings{nullptr};

    QTabWidget* m_tabWidget{nullptr};

    // Tab 1: General
    QCheckBox* m_launchOnStartupCheck{nullptr};
    QCheckBox* m_integrateChromeCheck{nullptr};
    QCheckBox* m_integrateEdgeCheck{nullptr};
    QCheckBox* m_integrateFirefoxCheck{nullptr};
    QCheckBox* m_integrateBraveCheck{nullptr};
    QCheckBox* m_clipboardMonitorCheck{nullptr};
    QCheckBox* m_showStartDialogCheck{nullptr};

    // Tab 2: File Types
    QLineEdit* m_fileTypesEdit{nullptr};
    QLineEdit* m_exceptionSitesEdit{nullptr};

    // Tab 3: Save To
    QLineEdit* m_defaultDirEdit{nullptr};
    QPushButton* m_browseDefaultDirBtn{nullptr};
    QLineEdit* m_tempDirEdit{nullptr};
    QPushButton* m_browseTempDirBtn{nullptr};
    QSpinBox* m_maxCacheLimitSpin{nullptr};
    QSpinBox* m_cacheExpirySpin{nullptr};

    // Tab 4: Connection
    QComboBox* m_connectionTypeCombo{nullptr};
    QSpinBox* m_maxConcurrentSpin{nullptr};
    QComboBox* m_connectionsPerTaskCombo{nullptr};
    QComboBox* m_speedModeCombo{nullptr};
    QSpinBox* m_manualSpeedLimitSpin{nullptr};
    QLabel* m_speedUnitLabel{nullptr};

    // Tab 5: Downloads
    QCheckBox* m_autoStartCompletedCheck{nullptr};

    // Tab 6: Security & Whitelist
    QCheckBox* m_antivirusScanCheck{nullptr};
    QLineEdit* m_antivirusPathEdit{nullptr};
    QPushButton* m_browseAntivirusBtn{nullptr};
    QTableWidget* m_whitelistTable{nullptr};
    QPushButton* m_addWhitelistBtn{nullptr};
    QPushButton* m_removeWhitelistBtn{nullptr};

    // Action Buttons
    QPushButton* m_okBtn{nullptr};
    QPushButton* m_cancelBtn{nullptr};
    QPushButton* m_applyBtn{nullptr};
};
