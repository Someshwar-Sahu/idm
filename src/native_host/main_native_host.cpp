#include "NativeHost.h"
#include <iostream>
#include <string>

using namespace std;
using namespace idm;

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

int main(int argc, char* argv[]) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc > 1) {
        string flag = argv[1];
        if (flag == "--install" && argc > 2) {
            string manifest_path = argv[2];
            bool c = NativeHost::install_native_host_registry(BrowserType::Chrome, manifest_path);
            bool e = NativeHost::install_native_host_registry(BrowserType::Edge, manifest_path);
            cout << "Installed registry keys. Chrome: " << (c ? "OK" : "FAILED")
                 << ", Edge: " << (e ? "OK" : "FAILED") << endl;
            return (c && e) ? 0 : 1;
        } else if (flag == "--uninstall") {
            NativeHost::uninstall_native_host_registry(BrowserType::Chrome);
            NativeHost::uninstall_native_host_registry(BrowserType::Edge);
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
            NativeHost::write_response_to_stdout(true, "Download task added to IDM");
        } else {
            NativeHost::write_response_to_stdout(false, "IDM engine offline (task queued)");
        }
    }

    return 0;
}
