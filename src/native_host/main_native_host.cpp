#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#endif

#include "NativeHost.h"
#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>

using namespace std;
using namespace idm;

int main(int argc, char* argv[]) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc > 1) {
        string flag = argv[1];
        if (flag == "--install") {
            string manifest_path = (argc > 2) ? argv[2] : "";
            if (manifest_path.empty()) {
#ifdef _WIN32
                char curExe[MAX_PATH];
                if (GetModuleFileNameA(NULL, curExe, MAX_PATH) > 0) {
                    string my_exe = curExe;
                    filesystem::path exeDir = filesystem::path(my_exe).parent_path();
                    manifest_path = (exeDir / "com.idm.native_host.json").string();
                    vector<string> origins = {
                        "chrome-extension://bjgkbgeokfiebnpmenpcojhlehlapjih/",
                        "chrome-extension://nnnaaifgjldomglhfdjfoeajfkigkld/"
                    };
                    string content = NativeHost::generate_native_manifest(my_exe, origins, BrowserType::Chrome);
                    ofstream mf(manifest_path);
                    if (mf.is_open()) {
                        mf << content;
                        mf.close();
                    }
                }
#endif
            }
            string ff_manifest_path = manifest_path;
#ifdef _WIN32
            if (argc <= 2) {
                char curExe[MAX_PATH];
                if (GetModuleFileNameA(NULL, curExe, MAX_PATH) > 0) {
                    string my_exe = curExe;
                    filesystem::path exeDir = filesystem::path(my_exe).parent_path();
                    ff_manifest_path = (exeDir / "com.idm.native_host_firefox.json").string();
                    vector<string> ff_extensions = {
                        "idm_integration@internetdownloadmanager.com"
                    };
                    string ff_content = NativeHost::generate_native_manifest(my_exe, ff_extensions, BrowserType::Firefox);
                    ofstream ff_mf(ff_manifest_path);
                    if (ff_mf.is_open()) {
                        ff_mf << ff_content;
                        ff_mf.close();
                    }
                }
            }
#endif
            bool c = NativeHost::install_native_host_registry(BrowserType::Chrome, manifest_path);
            bool e = NativeHost::install_native_host_registry(BrowserType::Edge, manifest_path);
            bool f = NativeHost::install_native_host_registry(BrowserType::Firefox, ff_manifest_path);
            bool b = NativeHost::install_native_host_registry(BrowserType::Brave, manifest_path);
            cout << "Installed registry keys. Chrome: " << (c ? "OK" : "FAILED")
                 << ", Edge: " << (e ? "OK" : "FAILED")
                 << ", Firefox: " << (f ? "OK" : "FAILED")
                 << ", Brave: " << (b ? "OK" : "FAILED") << endl;
            return (c && e && f && b) ? 0 : 1;
        } else if (flag == "--uninstall") {
            NativeHost::uninstall_native_host_registry(BrowserType::Chrome);
            NativeHost::uninstall_native_host_registry(BrowserType::Edge);
            NativeHost::uninstall_native_host_registry(BrowserType::Firefox);
            NativeHost::uninstall_native_host_registry(BrowserType::Brave);
            cout << "Uninstalled native messaging registry keys." << endl;
            return 0;
        }
    }

    // Normal Stdio loop for Chrome / Edge
    InterceptedDownloadPayload payload;
    while (NativeHost::read_message_from_stdin(payload)) {
        string resp;
        bool ok = NativeHost::forward_payload_to_server(payload, "127.0.0.1", 8080, &resp);
        if (ok) {
            if (resp.find("\"status\":\"IGNORED\"") != string::npos ||
                resp.find("\"status\": \"IGNORED\"") != string::npos) {
                NativeHost::write_response_to_stdout(false, "IGNORED");
            } else {
                NativeHost::write_response_to_stdout(true, "Download task added to IDM");
            }
        } else {
            NativeHost::write_response_to_stdout(false, "IDM engine offline (task queued)");
        }
    }

    return 0;
}
