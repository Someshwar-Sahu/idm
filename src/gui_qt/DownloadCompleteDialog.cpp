#include "DownloadCompleteDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QDir>
#include <sstream>
#include <iomanip>

namespace {
QString formatBytes(uint64_t bytes) {
    if (bytes == 0) return QStringLiteral("0 B");
    static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(unitIndex == 0 ? 0 : 2) << size << " " << units[unitIndex];
    return QString::fromStdString(ss.str());
}
}

DownloadCompleteDialog::DownloadCompleteDialog(const std::string& destinationPath, uint64_t totalBytes, QWidget* parent)
    : QDialog(parent), m_path(destinationPath) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(QStringLiteral("Download Complete"));
    setMinimumWidth(480);
    setStyleSheet(QStringLiteral(
        "QDialog { background: #1c1c20; color: #f4f4f5; }"
        "QLabel { font-size: 12px; color: #e4e4e7; }"
        "QPushButton { background: #27272a; color: #f4f4f5; border: 1px solid #3f3f46; border-radius: 5px; padding: 6px 16px; font-size: 12px; font-weight: 500; }"
        "QPushButton:hover { background: #3f3f46; border-color: #60a5fa; }"
        "QPushButton:pressed { background: #18181b; }"
    ));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    auto* headerLabel = new QLabel(QStringLiteral("✔  Download Complete!"), this);
    headerLabel->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: 700; color: #34d399; padding: 4px 0;"));
    mainLayout->addWidget(headerLabel);

    QFileInfo fi(QString::fromStdString(m_path));
    auto* nameLabel = new QLabel(QStringLiteral("<b>File:</b> ") + fi.fileName(), this);
    mainLayout->addWidget(nameLabel);

    auto* sizeLabel = new QLabel(QStringLiteral("<b>Size:</b> ") + formatBytes(totalBytes), this);
    mainLayout->addWidget(sizeLabel);

    auto* pathLabel = new QLabel(QStringLiteral("<b>Folder:</b> ") + fi.absolutePath(), this);
    pathLabel->setWordWrap(true);
    mainLayout->addWidget(pathLabel);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch(1);

    auto* openBtn = new QPushButton(QStringLiteral("Open"), this);
    openBtn->setStyleSheet(QStringLiteral("background: #2563eb; color: white; font-weight: 600;"));
    connect(openBtn, &QPushButton::clicked, this, &DownloadCompleteDialog::onOpenClicked);
    btnLayout->addWidget(openBtn);

    auto* folderBtn = new QPushButton(QStringLiteral("Open Folder"), this);
    connect(folderBtn, &QPushButton::clicked, this, &DownloadCompleteDialog::onOpenFolderClicked);
    btnLayout->addWidget(folderBtn);

    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(closeBtn);

    mainLayout->addLayout(btnLayout);
}

void DownloadCompleteDialog::onOpenClicked() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdString(m_path)));
    accept();
}

void DownloadCompleteDialog::onOpenFolderClicked() {
#ifdef _WIN32
    QString nativePath = QDir::toNativeSeparators(QString::fromStdString(m_path));
    QStringList args;
    args << QStringLiteral("/select,") << nativePath;
    QProcess::startDetached(QStringLiteral("explorer.exe"), args);
#else
    QFileInfo fi(QString::fromStdString(m_path));
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
#endif
    accept();
}
