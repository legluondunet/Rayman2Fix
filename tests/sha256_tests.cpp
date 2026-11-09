#include "rayman2fix/sha256.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

static void check(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}

int main() {
    const fs::path file = fs::temp_directory_path() /
        ("rayman2fix-sha256-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(r2fix::sha256_text("") ==
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
              "empty-string SHA256");
        check(r2fix::sha256_text("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
              "abc SHA256");
        std::string million(1000000, 'a');
        check(r2fix::sha256_text(million) ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
              "million-a NIST SHA256 vector");
        r2fix::Sha256 chunked;
        for (std::size_t i = 0; i < million.size(); i += 113)
            chunked.update(million.data() + i,
                           std::min<std::size_t>(113, million.size() - i));
        check(chunked.finish() ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
              "streaming chunk SHA256");

        {
            std::ofstream out(file, std::ios::binary);
            out.write(million.data(), static_cast<std::streamsize>(million.size()));
            check(static_cast<bool>(out), "write binary SHA256 test source");
        }
        check(r2fix::sha256_file(file) ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
              "file SHA256");
        fs::remove(file);
        std::cout << "All SHA256 known-vector tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::error_code ignored;
        fs::remove(file, ignored);
        std::cerr << "SHA256 test failed: " << e.what() << '\n';
        return 1;
    }
}
