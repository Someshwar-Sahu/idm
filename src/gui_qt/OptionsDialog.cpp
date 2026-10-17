#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "OptionsDialog.h"
#include "../native_host/NativeHost.h"
#include <filesystem>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>

OptionsDialog::OptionsDialog(SettingsManager* settings, QWidget* parent)
    : QDialog(parent), m_settings(settings) {
    setWindowTitle(QStringLiteral("Configuration"));
    resize(580, 480);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    setStyleSheet(QStringLiteral(
        "QDialog { background: #1c1c20; color: #f4f4f5; }"
        "QTabWidget::pane { border: 1px solid #27272a; background: #18181b; border-radius: 6px; padding: 10px; }"
        "QTabBar::tab { background: #27272a; color: #a1a1aa; padding: 8px 14px; border-top-left-radius: 5px; border-top-right-radius: 5px; margin-right: 2px; font-size: 12px; }"
        "QTabBar::tab:selected { background: #18181b; color: #38bdf8; font-weight: bold; border-bottom: 2px solid #38bdf8; }"
        "QGroupBox { color: #38bdf8; font-weight: bold; border: 1px solid #27272a; border-radius: 6px; margin-top: 14px; padding-top: 16px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QLabel, QCheckBox { font-size: 12px; color: #e4e4e7; }"
        "QLineEdit, QComboBox, QSpinBox { background: #27272a; color: #f4f4f5; border: 1px solid #3f3f46; border-radius: 4px; padding: 5px 8px; font-size: 12px; }"
        "QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: #3b82f6; }"
        "QPushButton { background: #27272a; color: #f4f4f5; border: 1px solid #3f3f46; border-radius: 5px; padding: 6px 16px; font-size: 12px; font-weight: 500; }"
        "QPushButton:hover { background: #3f3f46; border-color: #60a5fa; }"
        "QPushButton:pressed { background: #18181b; }"
    ));

    setupUi();
    loadSettings();
}

void OptionsDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    m_tabWidget = new QTabWidget(this);

    // ==========================================
    // Tab 1: General
    // ==========================================
    auto* tabGeneral = new QWidget(this);
    auto* generalLayout = new QVBoxLayout(tabGeneral);
    generalLayout->setSpacing(12);

    auto* startupGroup = new QGroupBox(QStringLiteral("Startup & System"), tabGeneral);
    auto* startupLayout = new QVBoxLayout(startupGroup);
    m_launchOnStartupCheck = new QCheckBox(QStringLiteral("Launch Internet Download Manager on Windows startup"), startupGroup);
    startupLayout->addWidget(m_launchOnStartupCheck);
    generalLayout->addWidget(startupGroup);

    auto* browserGroup = new QGroupBox(QStringLiteral("Browser Integration"), tabGeneral);
    auto* browserLayout = new QVBoxLayout(browserGroup);
    browserLayout->setSpacing(8);
    m_integrateChromeCheck = new QCheckBox(QStringLiteral("Integrate into Google Chrome"), browserGroup);
    m_integrateEdgeCheck = new QCheckBox(QStringLiteral("Integrate into Microsoft Edge"), browserGroup);
    m_integrateFirefoxCheck = new QCheckBox(QStringLiteral("Integrate into Mozilla Firefox"), browserGroup);
    m_integrateBraveCheck = new QCheckBox(QStringLiteral("Integrate into Brave Browser"), browserGroup);
    browserLayout->addWidget(m_integrateChromeCheck);
    browserLayout->addWidget(m_integrateEdgeCheck);
    browserLayout->addWidget(m_integrateFirefoxCheck);
    browserLayout->addWidget(m_integrateBraveCheck);
    generalLayout->addWidget(browserGroup);

    auto* clipGroup = new QGroupBox(QStringLiteral("Clipboard Monitoring"), tabGeneral);
    auto* clipLayout = new QVBoxLayout(clipGroup);
    m_clipboardMonitorCheck = new QCheckBox(QStringLiteral("Automatically detect downloadable links from clipboard"), clipGroup);
    clipLayout->addWidget(m_clipboardMonitorCheck);
    generalLayout->addWidget(clipGroup);
    generalLayout->addStretch(1);

    m_tabWidget->addTab(tabGeneral, QStringLiteral("General"));

    // ==========================================
    // Tab 2: File Types
    // ==========================================
    auto* tabFileTypes = new QWidget(this);
    auto* ftLayout = new QVBoxLayout(tabFileTypes);
    ftLayout->setSpacing(12);

    auto* ftGroup = new QGroupBox(QStringLiteral("Automatic Downloads"), tabFileTypes);
    auto* ftGroupLayout = new QVBoxLayout(ftGroup);
    ftGroupLayout->addWidget(new QLabel(QStringLiteral("Automatically start downloading the following file types:"), ftGroup));
    m_fileTypesEdit = new QLineEdit(ftGroup);
    ftGroupLayout->addWidget(m_fileTypesEdit);
    ftLayout->addWidget(ftGroup);

    auto* exGroup = new QGroupBox(QStringLiteral("Exceptions"), tabFileTypes);
    auto* exGroupLayout = new QVBoxLayout(exGroup);
    exGroupLayout->addWidget(new QLabel(QStringLiteral("Don't start downloading automatically from the following addresses:"), exGroup));
    m_exceptionSitesEdit = new QLineEdit(exGroup);
    m_exceptionSitesEdit->setPlaceholderText(QStringLiteral("example.com/stream/*"));
    exGroupLayout->addWidget(m_exceptionSitesEdit);
    ftLayout->addWidget(exGroup);
    ftLayout->addStretch(1);

    m_tabWidget->addTab(tabFileTypes, QStringLiteral("File Types"));

    // ==========================================
    // Tab 3: Save To
    // ==========================================
    auto* tabSaveTo = new QWidget(this);
    auto* saveLayout = new QVBoxLayout(tabSaveTo);
    saveLayout->setSpacing(12);

    auto* dirGroup = new QGroupBox(QStringLiteral("Download Folders"), tabSaveTo);
    auto* dirGrid = new QGridLayout(dirGroup);
    dirGrid->setHorizontalSpacing(10);
    dirGrid->setVerticalSpacing(10);

    dirGrid->addWidget(new QLabel(QStringLiteral("Default download directory:"), dirGroup), 0, 0);
    m_defaultDirEdit = new QLineEdit(dirGroup);
    dirGrid->addWidget(m_defaultDirEdit, 0, 1);
    m_browseDefaultDirBtn = new QPushButton(QStringLiteral("Browse"), dirGroup);
    connect(m_browseDefaultDirBtn, &QPushButton::clicked, this, &OptionsDialog::onBrowseDefaultDir);
    dirGrid->addWidget(m_browseDefaultDirBtn, 0, 2);

    dirGrid->addWidget(new QLabel(QStringLiteral("Temporary directory (cache):"), dirGroup), 1, 0);
    m_tempDirEdit = new QLineEdit(dirGroup);
    dirGrid->addWidget(m_tempDirEdit, 1, 1);
    m_browseTempDirBtn = new QPushButton(QStringLiteral("Browse"), dirGroup);
    connect(m_browseTempDirBtn, &QPushButton::clicked, this, &OptionsDialog::onBrowseTempDir);
    dirGrid->addWidget(m_browseTempDirBtn, 1, 2);

    saveLayout->addWidget(dirGroup);

    auto* catDescGroup = new QGroupBox(QStringLiteral("Category Subdirectories"), tabSaveTo);
    auto* catDescLayout = new QVBoxLayout(catDescGroup);
    catDescLayout->addWidget(new QLabel(
        QStringLiteral("Downloads are automatically sorted into category subfolders:\n"
                       "  - Compressed: /Compressed\n"
                       "  - Documents: /Documents\n"
                       "  - Music: /Music\n"
                       "  - Programs: /Programs\n"
                       "  - Videos: /Videos"),
        catDescGroup
    ));
    saveLayout->addWidget(catDescGroup);
    saveLayout->addStretch(1);

    m_tabWidget->addTab(tabSaveTo, QStringLiteral("Save To"));

    // ==========================================
    // Tab 4: Connection
    // ==========================================
    auto* tabConn = new QWidget(this);
    auto* connLayout = new QVBoxLayout(tabConn);
    connLayout->setSpacing(12);

    auto* connGroup = new QGroupBox(QStringLiteral("Connection & Concurrency"), tabConn);
    auto* connGrid = new QGridLayout(connGroup);
    connGrid->setHorizontalSpacing(10);
    connGrid->setVerticalSpacing(10);

    connGrid->addWidget(new QLabel(QStringLiteral("Connection Type/Speed:"), connGroup), 0, 0);
    m_connectionTypeCombo = new QComboBox(connGroup);
    m_connectionTypeCombo->addItems({
        QStringLiteral("High Speed: Direct connection (Ethernet/Cable) / Wi-Fi"),
        QStringLiteral("Medium Speed: 4G / DSL"),
        QStringLiteral("Low Speed: Dial-up")
    });
    connGrid->addWidget(m_connectionTypeCombo, 0, 1);

    connGrid->addWidget(new QLabel(QStringLiteral("Max concurrent downloads:"), connGroup), 1, 0);
    m_maxConcurrentSpin = new QSpinBox(connGroup);
    m_maxConcurrentSpin->setRange(1, 10);
    m_maxConcurrentSpin->setValue(3);
    connGrid->addWidget(m_maxConcurrentSpin, 1, 1);

    connGrid->addWidget(new QLabel(QStringLiteral("Max connection number (per file):"), connGroup), 2, 0);
    m_connectionsPerTaskCombo = new QComboBox(connGroup);
    m_connectionsPerTaskCombo->addItems({
        QStringLiteral("4"),
        QStringLiteral("8"),
        QStringLiteral("16"),
        QStringLiteral("24"),
        QStringLiteral("32")
    });
    m_connectionsPerTaskCombo->setCurrentText(QStringLiteral("8"));
    connGrid->addWidget(m_connectionsPerTaskCombo, 2, 1);

    connLayout->addWidget(connGroup);

    auto* speedGroup = new QGroupBox(QStringLiteral("Speed Limiter"), tabConn);
    auto* speedGrid = new QGridLayout(speedGroup);
    speedGrid->setHorizontalSpacing(10);
    speedGrid->setVerticalSpacing(10);

    speedGrid->addWidget(new QLabel(QStringLiteral("Speed Mode:"), speedGroup), 0, 0);
    m_speedModeCombo = new QComboBox(speedGroup);
    m_speedModeCombo->addItems({
        QStringLiteral("Uncapped (Maximum bandwidth)"),
        QStringLiteral("Smart Auto-QoS (Dynamic Auto Cap)"),
        QStringLiteral("Manual Cap")
    });
    connect(m_speedModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &OptionsDialog::onSpeedModeChanged);
    speedGrid->addWidget(m_speedModeCombo, 0, 1);

    speedGrid->addWidget(new QLabel(QStringLiteral("Maximum speed limit:"), speedGroup), 1, 0);
    auto* speedInputLayout = new QHBoxLayout();
    m_manualSpeedLimitSpin = new QSpinBox(speedGroup);
    m_manualSpeedLimitSpin->setRange(10, 100000);
    m_manualSpeedLimitSpin->setValue(500);
    m_manualSpeedLimitSpin->setEnabled(false);
    speedInputLayout->addWidget(m_manualSpeedLimitSpin);
    m_speedUnitLabel = new QLabel(QStringLiteral("KB/s"), speedGroup);
    speedInputLayout->addWidget(m_speedUnitLabel);
    speedInputLayout->addStretch(1);
    speedGrid->addLayout(speedInputLayout, 1, 1);

    connLayout->addWidget(speedGroup);
    connLayout->addStretch(1);

    m_tabWidget->addTab(tabConn, QStringLiteral("Connection"));

    // ==========================================
    // Tab 5: Downloads & Security
    // ==========================================
    auto* tabDownloads = new QWidget(this);
    auto* dlLayout = new QVBoxLayout(tabDownloads);
    dlLayout->setSpacing(12);

    auto* promptGroup = new QGroupBox(QStringLiteral("Confirmation Dialogs"), tabDownloads);
    auto* promptLayout = new QVBoxLayout(promptGroup);
    m_showStartDialogCheck = new QCheckBox(QStringLiteral("Show 'Start Download' dialog before downloading"), promptGroup);
    promptLayout->addWidget(m_showStartDialogCheck);
    dlLayout->addWidget(promptGroup);

    auto* avGroup = new QGroupBox(QStringLiteral("Antivirus Scanning"), tabDownloads);
    auto* avGrid = new QGridLayout(avGroup);
    avGrid->setHorizontalSpacing(10);
    avGrid->setVerticalSpacing(10);

    m_antivirusScanCheck = new QCheckBox(QStringLiteral("Perform antivirus scan on completed downloads"), avGroup);
    avGrid->addWidget(m_antivirusScanCheck, 0, 0, 1, 3);

    avGrid->addWidget(new QLabel(QStringLiteral("Scanner program:"), avGroup), 1, 0);
    m_antivirusPathEdit = new QLineEdit(avGroup);
    m_antivirusPathEdit->setPlaceholderText(QStringLiteral("C:/Program Files/Windows Defender/MpCmdRun.exe"));
    avGrid->addWidget(m_antivirusPathEdit, 1, 1);
    m_browseAntivirusBtn = new QPushButton(QStringLiteral("Browse"), avGroup);
    connect(m_browseAntivirusBtn, &QPushButton::clicked, this, &OptionsDialog::onBrowseAntivirus);
    avGrid->addWidget(m_browseAntivirusBtn, 1, 2);

    dlLayout->addWidget(avGroup);
    dlLayout->addStretch(1);

    m_tabWidget->addTab(tabDownloads, QStringLiteral("Downloads"));

    mainLayout->addWidget(m_tabWidget);

    // ==========================================
    // Bottom Action Buttons
    // ==========================================
    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch(1);

    m_okBtn = new QPushButton(QStringLiteral("OK"), this);
    m_okBtn->setStyleSheet(QStringLiteral("background: #2563eb; color: #ffffff; font-weight: bold;"));
    connect(m_okBtn, &QPushButton::clicked, this, &OptionsDialog::onOkClicked);
    btnLayout->addWidget(m_okBtn);

    m_cancelBtn = new QPushButton(QStringLiteral("Cancel"), this);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(m_cancelBtn);

    m_applyBtn = new QPushButton(QStringLiteral("Apply"), this);
    connect(m_applyBtn, &QPushButton::clicked, this, &OptionsDialog::onApplyClicked);
    btnLayout->addWidget(m_applyBtn);

    mainLayout->addLayout(btnLayout);
}

void OptionsDialog::loadSettings() {
    if (!m_settings) return;
    auto s = m_settings->get_settings();

    // General
    m_launchOnStartupCheck->setChecked(s.launch_on_startup);
    m_integrateChromeCheck->setChecked(s.integrate_chrome);
    m_integrateEdgeCheck->setChecked(s.integrate_edge);
    m_integrateFirefoxCheck->setChecked(s.integrate_firefox);
    m_integrateBraveCheck->setChecked(s.integrate_brave);
    m_clipboardMonitorCheck->setChecked(s.monitor_clipboard);

    // File Types
    m_fileTypesEdit->setText(QString::fromStdString(s.file_types));
    m_exceptionSitesEdit->setText(QString::fromStdString(s.exception_sites));

    // Save To
    m_defaultDirEdit->setText(QString::fromStdString(s.default_download_directory));
    m_tempDirEdit->setText(QString::fromStdString(s.cache_directory));

    // Connection
    m_maxConcurrentSpin->setValue(s.max_concurrent_download > 0 ? s.max_concurrent_download : 3);
    m_connectionsPerTaskCombo->setCurrentText(QString::number(s.default_connections_per_task > 0 ? s.default_connections_per_task : 8));

    if (s.speed_mode == SpeedLimiterMode::ManualCap) {
        m_speedModeCombo->setCurrentIndex(2);
        m_manualSpeedLimitSpin->setEnabled(true);
        m_manualSpeedLimitSpin->setValue(static_cast<int>(s.manual_speed_limit_bps / 1024));
    } else if (s.speed_mode == SpeedLimiterMode::AutoQoS) {
        m_speedModeCombo->setCurrentIndex(1);
        m_manualSpeedLimitSpin->setEnabled(false);
    } else {
        m_speedModeCombo->setCurrentIndex(0);
        m_manualSpeedLimitSpin->setEnabled(false);
    }

    // Downloads & Security
    m_showStartDialogCheck->setChecked(s.show_start_download_dialog);
    m_antivirusScanCheck->setChecked(s.antivirus_scan_enabled);
    m_antivirusPathEdit->setText(QString::fromStdString(s.antivirus_executable_path));
}

void OptionsDialog::saveSettings() {
    if (!m_settings) return;
    auto s = m_settings->get_settings();

    s.launch_on_startup = m_launchOnStartupCheck->isChecked();
    s.integrate_chrome = m_integrateChromeCheck->isChecked();
    s.integrate_edge = m_integrateEdgeCheck->isChecked();
    s.integrate_firefox = m_integrateFirefoxCheck->isChecked();
    s.integrate_brave = m_integrateBraveCheck->isChecked();
    s.enable_browser_interception = s.integrate_chrome || s.integrate_edge || s.integrate_firefox || s.integrate_brave;
    s.monitor_clipboard = m_clipboardMonitorCheck->isChecked();

    s.file_types = m_fileTypesEdit->text().trimmed().toStdString();
    s.exception_sites = m_exceptionSitesEdit->text().trimmed().toStdString();

    QString defaultDir = m_defaultDirEdit->text().trimmed();
    if (!defaultDir.isEmpty()) {
        s.default_download_directory = defaultDir.toStdString();
        QDir().mkpath(defaultDir);
    }

    QString tempDir = m_tempDirEdit->text().trimmed();
    if (!tempDir.isEmpty()) {
        s.cache_directory = tempDir.toStdString();
        QDir().mkpath(tempDir);
    }

    s.max_concurrent_download = m_maxConcurrentSpin->value();
    s.default_connections_per_task = m_connectionsPerTaskCombo->currentText().toInt();

    if (m_speedModeCombo->currentIndex() == 2) {
        s.speed_mode = SpeedLimiterMode::ManualCap;
        s.manual_speed_limit_bps = static_cast<uint64_t>(m_manualSpeedLimitSpin->value()) * 1024;
    } else if (m_speedModeCombo->currentIndex() == 1) {
        s.speed_mode = SpeedLimiterMode::AutoQoS;
        s.manual_speed_limit_bps = 0;
    } else {
        s.speed_mode = SpeedLimiterMode::Uncapped;
        s.manual_speed_limit_bps = 0;
    }

    s.show_start_download_dialog = m_showStartDialogCheck->isChecked();
    s.antivirus_scan_enabled = m_antivirusScanCheck->isChecked();
    s.antivirus_executable_path = m_antivirusPathEdit->text().trimmed().toStdString();

    m_settings->update_settings(s);
    m_settings->save();

#ifdef _WIN32
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        if (s.launch_on_startup) {
            char exePath[MAX_PATH];
            if (GetModuleFileNameA(NULL, exePath, MAX_PATH) > 0) {
                RegSetValueExA(hKey, "IDMan", 0, REG_SZ, reinterpret_cast<const BYTE*>(exePath), static_cast<DWORD>(strlen(exePath) + 1));
            }
        } else {
            RegDeleteValueA(hKey, "IDMan");
        }
        RegCloseKey(hKey);
    }

    std::filesystem::path manifestPath;
    char curExePath[MAX_PATH];
    if (GetModuleFileNameA(NULL, curExePath, MAX_PATH) > 0) {
        std::filesystem::path exeDir = std::filesystem::path(curExePath).parent_path();
        if (std::filesystem::exists(exeDir / "com.idm.native_host.json")) {
            manifestPath = exeDir / "com.idm.native_host.json";
        } else if (std::filesystem::exists(exeDir.parent_path() / "extension" / "com.idm.native_host.json")) {
            manifestPath = exeDir.parent_path() / "extension" / "com.idm.native_host.json";
        }
    }

    if (s.integrate_edge && !manifestPath.empty()) {
        idm::NativeHost::install_native_host_registry(idm::BrowserType::Edge, manifestPath.string());
    } else {
        idm::NativeHost::uninstall_native_host_registry(idm::BrowserType::Edge);
    }

    if (s.integrate_chrome && !manifestPath.empty()) {
        idm::NativeHost::install_native_host_registry(idm::BrowserType::Chrome, manifestPath.string());
    } else {
        idm::NativeHost::uninstall_native_host_registry(idm::BrowserType::Chrome);
    }

    if (s.integrate_firefox && !manifestPath.empty()) {
        idm::NativeHost::install_native_host_registry(idm::BrowserType::Firefox, manifestPath.string());
    } else {
        idm::NativeHost::uninstall_native_host_registry(idm::BrowserType::Firefox);
    }
#endif
}

void OptionsDialog::onSpeedModeChanged(int index) {
    m_manualSpeedLimitSpin->setEnabled(index == 2);
}

void OptionsDialog::onBrowseDefaultDir() {
    QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Default Download Directory"), m_defaultDirEdit->text());
    if (!dir.isEmpty()) {
        m_defaultDirEdit->setText(dir);
    }
}

void OptionsDialog::onBrowseTempDir() {
    QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Temporary Directory"), m_tempDirEdit->text());
    if (!dir.isEmpty()) {
        m_tempDirEdit->setText(dir);
    }
}

void OptionsDialog::onBrowseAntivirus() {
    QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Select Antivirus Executable"), m_antivirusPathEdit->text(), QStringLiteral("Executables (*.exe)"));
    if (!file.isEmpty()) {
        m_antivirusPathEdit->setText(file);
    }
}

void OptionsDialog::onApplyClicked() {
    saveSettings();
    emit settingsApplied();
    if (m_applyBtn) {
        m_applyBtn->setText(QStringLiteral("Applied ✓"));
        QTimer::singleShot(1500, this, [this]() {
            if (m_applyBtn) m_applyBtn->setText(QStringLiteral("Apply"));
        });
    }
}

void OptionsDialog::onOkClicked() {
    saveSettings();
    emit settingsApplied();
    accept();
}
