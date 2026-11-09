#include <filesystem>
#include <fstream>

int main() {
    // The process creation smoke test checks CreateProcessW's explicit
    // lpCurrentDirectory by creating this file in the designated location.
    std::ofstream out(std::filesystem::current_path() / "child-working-directory.txt");
    out << "cwd-verified\n";
    return out ? 17 : 23;
}
