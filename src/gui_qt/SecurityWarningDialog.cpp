#include "SecurityWarningDialog.h"
#include "../security/AntivirusScanner.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QStyle>
#include <QIcon>
#include <QFrame>

SecurityWarningDialog::SecurityWarningDialog(Mode mode,
                                             const QString& target,
                                             const QString& threatOrDetails,
                                             const QString& secondaryInfo,
                                             QWidget* parent)
    : QDialog(parent),
      m_mode(mode),
      m_target(target),
      m_threatOrDetails(threatOrDetails),
      m_secondaryInfo(secondaryInfo) {
    setModal(true);
    setMinimumWidth(560);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);

    if (m_mode == Mode::PreDownload) {
        setWindowTitle(QStringLiteral("IDM Security Warning - Pre-Download Check"));
    } else {
        setWindowTitle(QStringLiteral("IDM Antivirus Alert - Threat Detected!"));
    }

    setStyleSheet(QStringLiteral(
        "QDialog { background: #18181b; color: #f4f4f5; font-family: 'Segoe UI', system-ui, sans-serif; }"
        "QLabel { font-size: 12px; color: #e4e4e7; }"
        "QFrame#detailsBox { background: #111113; border: 1px solid #27272a; border-radius: 6px; padding: 12px; }"
        "QPushButton#safeBtn { background: #2563eb; color: #ffffff; font-weight: 600; border: none; border-radius: 6px; padding: 8px 18px; font-size: 12px; }"
        "QPushButton#safeBtn:hover { background: #1d4ed8; }"
        "QPushButton#overrideBtn { background: #27272a; color: #f87171; border: 1px solid #7f1d1d; border-radius: 6px; padding: 8px 16px; font-size: 12px; font-weight: 500; }"
        "QPushButton#overrideBtn:hover { background: #3f1515; border-color: #ef4444; color: #fca5a5; }"
        "QCheckBox { font-size: 12px; color: #a1a1aa; }"
        "QCheckBox::indicator { width: 16px; height: 16px; }"
    ));

    setupUi();
}

void SecurityWarningDialog::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(14);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // Top Header Banner with Icon
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(14);

    auto* iconLabel = new QLabel(this);
    QIcon warnIcon = style()->standardIcon(m_mode == Mode::PreDownload ? QStyle::SP_MessageBoxWarning : QStyle::SP_MessageBoxCritical);
    iconLabel->setPixmap(warnIcon.pixmap(44, 44));
    headerLayout->addWidget(iconLabel, 0, Qt::AlignTop);

    auto* titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(4);

    auto* headingLabel = new QLabel(this);
    if (m_mode == Mode::PreDownload) {
        headingLabel->setText(QStringLiteral("<span style='font-size: 15px; font-weight: 700; color: #f59e0b;'>Potential Threat Detected Before Download</span>"));
    } else {
        headingLabel->setText(QStringLiteral("<span style='font-size: 15px; font-weight: 700; color: #ef4444;'>Malicious Threat Blocked by Antivirus Engine</span>"));
    }
    titleLayout->addWidget(headingLabel);

    auto* descLabel = new QLabel(this);
    descLabel->setWordWrap(true);
    if (m_mode == Mode::PreDownload) {
        descLabel->setText(QStringLiteral("IDM SmartScreen Pre-Flight Shield analyzed this link and flagged it as suspicious or harmful. Downloading this content may endanger your computer."));
    } else {
        descLabel->setText(QStringLiteral("IDM Antivirus Scanner and Windows Defender detected a malicious payload in this downloaded file. To protect your system, execution has been prevented."));
    }
    titleLayout->addWidget(descLabel);

    headerLayout->addLayout(titleLayout, 1);
    mainLayout->addLayout(headerLayout);

    // Details Frame Box
    auto* detailsBox = new QFrame(this);
    detailsBox->setObjectName(QStringLiteral("detailsBox"));
    auto* detailsGrid = new QGridLayout(detailsBox);
    detailsGrid->setHorizontalSpacing(12);
    detailsGrid->setVerticalSpacing(8);

    if (m_mode == Mode::PreDownload) {
        detailsGrid->addWidget(new QLabel(QStringLiteral("<b>Target URL:</b>"), detailsBox), 0, 0);
        auto* urlVal = new QLabel(m_target, detailsBox);
        urlVal->setWordWrap(true);
        urlVal->setStyleSheet(QStringLiteral("color: #38bdf8; font-family: Consolas, monospace; font-size: 11px;"));
        detailsGrid->addWidget(urlVal, 0, 1);

        detailsGrid->addWidget(new QLabel(QStringLiteral("<b>Security Alert:</b>"), detailsBox), 1, 0);
        auto* alertVal = new QLabel(QStringLiteral("<span style='color: #f87171; font-weight: 600;'>%1</span>").arg(m_threatOrDetails), detailsBox);
        alertVal->setWordWrap(true);
        detailsGrid->addWidget(alertVal, 1, 1);

        if (!m_secondaryInfo.isEmpty()) {
            detailsGrid->addWidget(new QLabel(QStringLiteral("<b>Destination:</b>"), detailsBox), 2, 0);
            auto* destVal = new QLabel(m_secondaryInfo, detailsBox);
            destVal->setWordWrap(true);
            detailsGrid->addWidget(destVal, 2, 1);
        }
    } else {
        detailsGrid->addWidget(new QLabel(QStringLiteral("<b>Threat Name:</b>"), detailsBox), 0, 0);
        auto* threatVal = new QLabel(QStringLiteral("<span style='color: #ef4444; font-weight: 700; font-size: 13px;'>%1</span>").arg(m_threatOrDetails), detailsBox);
        threatVal->setWordWrap(true);
        detailsGrid->addWidget(threatVal, 0, 1);

        detailsGrid->addWidget(new QLabel(QStringLiteral("<b>Target File:</b>"), detailsBox), 1, 0);
        auto* fileVal = new QLabel(m_target, detailsBox);
        fileVal->setWordWrap(true);
        fileVal->setStyleSheet(QStringLiteral("color: #38bdf8; font-family: Consolas, monospace; font-size: 11px;"));
        detailsGrid->addWidget(fileVal, 1, 1);

        if (!m_secondaryInfo.isEmpty()) {
            detailsGrid->addWidget(new QLabel(QStringLiteral("<b>Details:</b>"), detailsBox), 2, 0);
            auto* secVal = new QLabel(m_secondaryInfo, detailsBox);
            secVal->setWordWrap(true);
            detailsGrid->addWidget(secVal, 2, 1);
        }
    }

    mainLayout->addWidget(detailsBox);

    // Option checkbox
    if (m_mode == Mode::PreDownload) {
        m_alwaysTrustCheck = new QCheckBox(QStringLiteral("Always trust and whitelist this domain in the future"), this);
        mainLayout->addWidget(m_alwaysTrustCheck);
    } else {
        m_alwaysTrustCheck = new QCheckBox(QStringLiteral("Always whitelist this file hash (SHA-256) and domain"), this);
        mainLayout->addWidget(m_alwaysTrustCheck);
    }

    // Action Buttons
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(10);

    m_overrideBtn = new QPushButton(this);
    m_overrideBtn->setObjectName(QStringLiteral("overrideBtn"));
    if (m_mode == Mode::PreDownload) {
        m_overrideBtn->setText(QStringLiteral("Override & Download Anyway"));
    } else {
        m_overrideBtn->setText(QStringLiteral("Override & Keep File"));
    }
    connect(m_overrideBtn, &QPushButton::clicked, this, &SecurityWarningDialog::onOverrideClicked);
    btnLayout->addWidget(m_overrideBtn);

    btnLayout->addStretch(1);

    m_safeActionBtn = new QPushButton(this);
    m_safeActionBtn->setObjectName(QStringLiteral("safeBtn"));
    if (m_mode == Mode::PreDownload) {
        m_safeActionBtn->setText(QStringLiteral("Cancel Download (Recommended)"));
    } else {
        m_safeActionBtn->setText(QStringLiteral("Quarantine & Delete (Recommended)"));
    }
    m_safeActionBtn->setDefault(true);
    connect(m_safeActionBtn, &QPushButton::clicked, this, &SecurityWarningDialog::onActionCancelOrDeleteClicked);
    btnLayout->addWidget(m_safeActionBtn);

    mainLayout->addLayout(btnLayout);
}

bool SecurityWarningDialog::isAlwaysTrustDomainChecked() const {
    return m_alwaysTrustCheck && m_alwaysTrustCheck->isChecked();
}

void SecurityWarningDialog::onOverrideClicked() {
    if (m_alwaysTrustCheck && m_alwaysTrustCheck->isChecked()) {
        if (m_mode == Mode::PreDownload) {
            std::string domain = AntivirusScanner::extract_domain_from_url(m_target.toStdString());
            if (!domain.empty()) {
                AntivirusScanner::add_domain_to_whitelist(domain, "User override from dialog");
            }
        } else {
            std::string filePath = m_target.toStdString();
            std::string fhash = AntivirusScanner::calculate_file_sha256(filePath);
            if (!fhash.empty()) {
                AntivirusScanner::add_hash_to_whitelist(fhash, "User override from dialog");
            }
            std::string sec = m_secondaryInfo.toStdString();
            size_t p = sec.find("Target URL: ");
            std::string urlStr = (p != std::string::npos) ? sec.substr(p + 12) : sec;
            std::string domain = AntivirusScanner::extract_domain_from_url(urlStr);
            if (!domain.empty()) {
                AntivirusScanner::add_domain_to_whitelist(domain, "User override from dialog");
            }
        }
    }
    accept();
}

void SecurityWarningDialog::onActionCancelOrDeleteClicked() {
    reject();
}
