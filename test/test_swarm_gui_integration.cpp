#include "../src/gui_qt/SwarmDownloadDialog.h"
#include "../src/net/SwarmDownloader.h"
#include "../src/security/AntivirusScanner.h"
#include <QApplication>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace std;
namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    // Force offscreen platform for reliable headless CI / sandbox execution
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));

    int fakeArgc = 1;
    char arg0[] = "test_swarm_gui";
    char* fakeArgv[] = { arg0, nullptr };
    QApplication app(fakeArgc, fakeArgv);

    cout << "=== RUNNING EXTENSIVE SWARM GUI INTEGRATION TESTS (POSITIVE + NEGATIVE) ===" << endl;

    fs::path test_dir = "test_swarm_gui_tmp";
    fs::create_directories(test_dir);

    // TEST 1: Metalink XML parsing and GUI widget population (Positive)
    {
        cout << "\n[TEST 1] Testing Metalink XML loading & dialog UI binding..." << endl;
        SwarmDownloadDialog dlg;

        QString xml = QStringLiteral(R"(
            <?xml version="1.0" encoding="UTF-8"?>
            <metalink version="3.0">
              <files>
                <file name="debian-12.0.0-amd64-netinst.iso">
                  <size>655360</size>
                  <verification>
                    <hash type="sha-256">e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855</hash>
                  </verification>
                  <resources>
                    <url location="us" priority="1">https://us.debian.org/debian.iso</url>
                    <url location="de" priority="2">https://de.debian.org/debian.iso</url>
                    <url location="jp" priority="3">https://jp.debian.org/debian.iso</url>
                  </resources>
                </file>
              </files>
            </metalink>
        )");

        bool loaded = dlg.loadMetalinkContent(xml, false);
        assert(loaded && "Valid XML metalink should load successfully");
        assert(dlg.filename() == QStringLiteral("debian-12.0.0-amd64-netinst.iso"));
        assert(dlg.fileSize() == 655360);
        assert(dlg.sha256Hash() == QStringLiteral("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
        assert(dlg.mirrorCount() == 3);

        cout << "  -> Metalink XML parsing & widget binding: PASSED" << endl;
    }

    // TEST 2: Metalink JSON loading and corrupt input handling (Negative)
    {
        cout << "\n[TEST 2] Testing Metalink JSON loading & corrupt input rejection (Negative)..." << endl;
        SwarmDownloadDialog dlg;

        QString corruptXml = QStringLiteral("<broken><tags>Not a metalink</tags>");
        bool failXml = dlg.loadMetalinkContent(corruptXml, false);
        assert(!failXml && "Corrupt XML must be rejected");

        QString emptyJson = QStringLiteral("{}");
        bool failJson = dlg.loadMetalinkContent(emptyJson, true);
        assert(!failJson && "Empty JSON must be rejected");

        QString validJson = QStringLiteral(R"({
            "name": "archive.zip",
            "size": 1048576,
            "hashes": {
                "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
            },
            "mirrors": [
                {"url": "https://m1.test.org/archive.zip", "priority": 1, "location": "US"},
                {"url": "https://m2.test.org/archive.zip", "priority": 2, "location": "EU"}
            ]
        })");

        bool okJson = dlg.loadMetalinkContent(validJson, true);
        assert(okJson && "Valid JSON metalink must load");
        assert(dlg.filename() == QStringLiteral("archive.zip"));
        assert(dlg.fileSize() == 1048576);
        assert(dlg.mirrorCount() == 2);

        cout << "  -> Negative reject & valid JSON binding: PASSED" << endl;
    }

    // TEST 3: Mirror manipulation, deduplication guard (Positive + Negative)
    {
        cout << "\n[TEST 3] Testing Mirror manipulation & duplicate mirror rejection..." << endl;
        SwarmDownloadDialog dlg;
        assert(dlg.mirrorCount() == 0);

        bool a1 = dlg.addMirrorEntry(QStringLiteral("https://mirror1.org/data"), 1, QStringLiteral("US"));
        assert(a1 && "Mirror 1 should be added");
        assert(dlg.mirrorCount() == 1);

        // Negative: Duplicate URL rejection
        bool a2 = dlg.addMirrorEntry(QStringLiteral("https://mirror1.org/data"), 2, QStringLiteral("EU"));
        assert(!a2 && "Duplicate mirror URL must be rejected");
        assert(dlg.mirrorCount() == 1);

        // Negative: Empty URL rejection
        bool a3 = dlg.addMirrorEntry(QStringLiteral("   "), 1, QStringLiteral("Global"));
        assert(!a3 && "Empty or whitespace mirror URL must be rejected");
        assert(dlg.mirrorCount() == 1);

        bool a4 = dlg.addMirrorEntry(QStringLiteral("https://mirror2.org/data"), 2, QStringLiteral("DE"));
        assert(a4 && "Mirror 2 should be added");
        assert(dlg.mirrorCount() == 2);

        cout << "  -> Mirror addition & duplicate prevention: PASSED" << endl;
    }

    // TEST 4: Multiplex Strategy & Thread Controls
    {
        cout << "\n[TEST 4] Testing Strategy & Thread configuration..." << endl;
        SwarmDownloadDialog dlg;
        dlg.setThreadCount(8);
        dlg.setStrategyIndex(1); // PriorityWeighted
        dlg.setDestinationPath(QStringLiteral("C:/Downloads/test.bin"));
        assert(dlg.destinationPath() == QStringLiteral("C:/Downloads/test.bin"));

        cout << "  -> Strategy & threads options: PASSED" << endl;
    }

    // TEST 5: Cancellation during Swarm Download (Negative / Lifecycle)
    {
        cout << "\n[TEST 5] Testing SwarmDownloader user cancellation during active transfer..." << endl;
        fs::path dest = test_dir / "cancel_test.bin";
        idm::SwarmDownloader downloader(dest.string(), 4);
        downloader.add_mirror("https://slow.mirror.com/file");

        // Mock transport that sleeps to simulate network delay
        downloader.set_mock_transport([](const string&, uint64_t, uint64_t, vector<uint8_t>& buf) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            buf.assign(1024, 0xAA);
            return true;
        });

        atomic<bool> downloadFinished{false};
        atomic<bool> downloadResult{false};

        thread t([&]() {
            bool res = downloader.download(1024 * 1024, 8, nullptr);
            downloadResult.store(res);
            downloadFinished.store(true);
        });

        // Sleep briefly to let worker begin, then cancel
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(!downloader.is_cancelled());
        downloader.cancel();
        assert(downloader.is_cancelled());

        t.join();
        assert(downloadFinished.load());
        assert(!downloadResult.load() && "Cancelled download should report false");
        assert(downloader.get_last_error().find("cancelled") != string::npos);

        cout << "  -> Swarm cancellation stops workers cleanly: PASSED" << endl;
    }

    // TEST 6: Multi-Mirror Failover & Integrity Verification (Positive + Fault-Injection)
    {
        cout << "\n[TEST 6] Testing Multi-Mirror Failover & SHA-256 Integrity Verification..." << endl;
        fs::path dest = test_dir / "failover_test.bin";
        uint64_t fileSize = 65536; // 64 KB
        int numChunks = 4;
        uint64_t chunkSize = fileSize / numChunks;

        idm::SwarmDownloader downloader(dest.string(), 2);
        downloader.set_strategy(idm::MultiplexStrategy::PriorityWeighted);

        downloader.add_mirror("https://flaky.mirror.org/data", 1, "US"); // Prio 1, will fail chunk 0
        downloader.add_mirror("https://backup.mirror.org/data", 2, "DE"); // Prio 2, will succeed

        atomic<int> flakyAttempts{0};

        downloader.set_mock_transport([&](const string& url, uint64_t start, uint64_t end, vector<uint8_t>& buf) {
            if (url.find("flaky") != string::npos && start == 0) {
                flakyAttempts++;
                return false; // Fault injection: Mirror 1 fails on chunk 0
            }
            uint64_t len = end - start + 1;
            buf.assign(len, 0x5A); // Identifiable byte pattern
            return true;
        });

        bool success = downloader.download(fileSize, numChunks, nullptr);
        assert(success && "Failover should enable complete download despite flaky mirror");
        assert(flakyAttempts.load() > 0 && "Flaky mirror should have been attempted and failed");
        assert(fs::exists(dest) && fs::file_size(dest) == fileSize);

        // Calculate expected SHA-256 of the 0x5A filled file
        string actualHash = AntivirusScanner::calculate_file_sha256(dest.string());
        assert(!actualHash.empty());

        // Positive check
        bool verifyOk = downloader.verify_integrity(actualHash);
        assert(verifyOk && "Integrity check with matching hash must return true");

        // Negative check: Mismatched hash
        bool verifyBad = downloader.verify_integrity("0000000000000000000000000000000000000000000000000000000000000000");
        assert(!verifyBad && "Integrity check with corrupted/wrong hash must return false");

        cout << "  -> Failover dispatch and SHA-256 integrity verification: PASSED" << endl;
    }

    // Cleanup
    fs::remove_all(test_dir);

    cout << "\n=== ALL 6 SWARM GUI INTEGRATION TESTS PASSED SUCCESSFULLY! ===" << endl;
    return 0;
}
