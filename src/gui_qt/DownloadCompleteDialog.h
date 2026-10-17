#pragma once
#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <string>

class DownloadCompleteDialog : public QDialog {
    Q_OBJECT
public:
    DownloadCompleteDialog(const std::string& destinationPath, uint64_t totalBytes, QWidget* parent = nullptr);

private slots:
    void onOpenClicked();
    void onOpenFolderClicked();

private:
    std::string m_path;
};
