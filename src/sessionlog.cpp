#include "sessionlog.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace sessionlog {

File::~File() { close(); }

void File::prune(const std::string& dir, const std::string& prefix, int keep) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    const std::string head = prefix + "-";
    std::vector<fs::path> logs;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        if (name.size() > head.size() + 4 && name.rfind(head, 0) == 0 &&
            name.compare(name.size() - 4, 4, ".log") == 0)
            logs.push_back(e.path());
    }
    // The names carry a zero-padded local timestamp, so name order IS age.
    std::sort(logs.begin(), logs.end());
    if (keep < 0) keep = 0;
    const size_t excess = logs.size() > (size_t)keep ? logs.size() - (size_t)keep : 0;
    for (size_t i = 0; i < excess; ++i) fs::remove(logs[i], ec);
}

bool File::open(const std::string& dir, const std::string& prefix, int maxLines,
                int keepFiles) {
    close();
    if (maxLines <= 0) return false;
    std::error_code ec;
    fs::create_directories(dir, ec);
    prune(dir, prefix, keepFiles > 1 ? keepFiles - 1 : 0);

    // Seconds and milliseconds from ONE clock read, or a session opened across
    // a second boundary could name itself older than the one before it.
    const auto clock = std::chrono::system_clock::now();
    const std::time_t now = std::chrono::system_clock::to_time_t(clock);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    // Milliseconds, so two sessions in one second still sort by age; the name
    // order is what prune() trusts.
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             clock.time_since_epoch())
                             .count() %
                         1000;
    char stamp[48];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    std::snprintf(stamp + std::strlen(stamp), 8, "-%03lld", ms);
    std::string path = (fs::path(dir) / (prefix + "-" + stamp + ".log")).string();
    // A collision within the same millisecond must not share a file (the
    // second would truncate the first one's record). '_' sorts after '.', so
    // "..._2.log" stays newer than "....log" in prune()'s name order.
    for (int n = 2; fs::exists(path, ec) && n < 10; ++n)
        path = (fs::path(dir) / (prefix + "-" + stamp + "_" + std::to_string(n) + ".log"))
                   .string();

    std::lock_guard<std::mutex> lock(mutex_);
    file_ = std::fopen(path.c_str(), "wb");
    if (!file_) return false;
    path_ = path;
    maxLines_ = maxLines;
    linesInFile_ = 0;
    tail_.clear();
    return true;
}

void File::write(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_) return;
    tail_.push_back(line);
    if (tail_.size() > (size_t)maxLines_) tail_.pop_front();
    std::fwrite(line.data(), 1, line.size(), file_);
    std::fputc('\n', file_);
    std::fflush(file_);
    if (++linesInFile_ >= (size_t)maxLines_ * 2) rewriteTail();
}

void File::rewriteTail() {
    std::fclose(file_);
    file_ = std::fopen(path_.c_str(), "wb");
    if (!file_) return;
    for (const std::string& l : tail_) {
        std::fwrite(l.data(), 1, l.size(), file_);
        std::fputc('\n', file_);
    }
    std::fflush(file_);
    linesInFile_ = tail_.size();
}

void File::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_) std::fclose(file_);
    file_ = nullptr;
    path_.clear();
    tail_.clear();
    linesInFile_ = 0;
}

std::string File::path() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return path_;
}

}  // namespace sessionlog
