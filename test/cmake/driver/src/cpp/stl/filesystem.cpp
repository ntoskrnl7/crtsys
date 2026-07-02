//
// https://en.cppreference.com/w/cpp/filesystem/path/lexically_normal#Example
//
#if defined(_MSC_VER)
// Release builds define NDEBUG, so assert-only cppreference verification values
// can look unused under /WX. Keep the examples intact and silence that warning
// only for this test translation unit.
#pragma warning(disable : 4189)
#endif

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <system_error>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {
constexpr fs::perms write_perms =
    fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write;

void remove_sandbox(const fs::path &path) {
  // Filesystem examples share a scratch directory inside one driver session;
  // remove stale contents so reruns after debugger stops stay deterministic.
  std::error_code ec;
  fs::remove_all(path, ec);
}
} // namespace

namespace filesystem_path_test {
void run() {
  assert(fs::path("a/./b/..").lexically_normal() == "a/");
  assert(fs::path("a/.///b/../").lexically_normal() == "a/");
  assert(fs::path("/a/d").lexically_relative("/a/b/c") == "../../d");
  assert(fs::path("/a/b/c").lexically_relative("/a/d") == "../b/c");
  assert(fs::path("a/b/c").lexically_relative("a") == "b/c");
  assert(fs::path("a/b/c").lexically_relative("a/b/c/x/y") == "../..");
  assert(fs::path("a/b/c").lexically_relative("a/b/c") == ".");
  assert(fs::path("a/b").lexically_relative("c/d") == "../../a/b");
  assert(fs::path("a/b").lexically_relative("/a/b") == "");
  assert(fs::path("a/b").lexically_proximate("/a/b") == "a/b");
  std::cout << "filesystem path lexical assertions passed\n";
}
} // namespace filesystem_path_test

//
// https://en.cppreference.com/w/cpp/filesystem/absolute
// https://en.cppreference.com/w/cpp/filesystem/relative
// https://en.cppreference.com/w/cpp/filesystem/current_path
//
namespace filesystem_path_relation_test {
void run() {
  const fs::path previous_current_path = fs::current_path();
  const fs::path sandbox{"sandbox"};
  remove_sandbox(sandbox);

  assert(fs::create_directories(sandbox / "a" / "b"));
  std::ofstream(sandbox / "a" / "file.txt").put('a');

  try {
    // cppreference documents these as current-directory-sensitive APIs. The
    // driver harness restores current_path before returning so following
    // filesystem examples are not affected.
    fs::current_path(sandbox);

    const fs::path absolute_file = fs::absolute("a/file.txt");
    assert(absolute_file.is_absolute());
    assert(fs::exists(absolute_file));

    assert(fs::relative(absolute_file, fs::current_path()) ==
           fs::path("a") / "file.txt");
    assert(fs::proximate(absolute_file, fs::current_path()) ==
           fs::path("a") / "file.txt");
  } catch (...) {
    fs::current_path(previous_current_path);
    remove_sandbox(sandbox);
    throw;
  }

  fs::current_path(previous_current_path);
  remove_sandbox(sandbox);
  std::cout << "filesystem path relation assertions passed\n";
}
} // namespace filesystem_path_relation_test

//
// https://en.cppreference.com/w/cpp/filesystem/directory_iterator#Example
//
namespace filesystem_directory_iterator_test {
void run() {
  const fs::path sandbox{"sandbox"};
  remove_sandbox(sandbox);

  assert(fs::create_directories(sandbox / "dir1" / "dir2"));
  std::ofstream{sandbox / "file1.txt"};
  std::ofstream{sandbox / "file2.txt"};

  std::cout << "directory_iterator:\n";
  std::uintmax_t directory_entries{};

  // directory_iterator can be iterated using a range-for loop.
  for (auto const &dir_entry : fs::directory_iterator{sandbox}) {
    std::cout << dir_entry.path() << '\n';
    ++directory_entries;
  }

  std::cout << "\ndirectory_iterator as a range:\n";
  std::uintmax_t range_entries{};
  // directory_iterator behaves as a range in other ways, too.
  std::ranges::for_each(fs::directory_iterator{sandbox},
                        [&range_entries](const auto &dir_entry) {
                          std::cout << dir_entry << '\n';
                          ++range_entries;
                        });

  std::cout << "\nrecursive_directory_iterator:\n";
  std::uintmax_t recursive_entries{};
  for (auto const &dir_entry : fs::recursive_directory_iterator{sandbox}) {
    std::cout << dir_entry << '\n';
    ++recursive_entries;
  }

  assert(directory_entries == 3);
  assert(range_entries == 3);
  assert(recursive_entries == 4);
  assert(fs::remove_all(sandbox) >= 5);
}
} // namespace filesystem_directory_iterator_test

//
// https://en.cppreference.com/w/cpp/filesystem/copy_file#Example
//
namespace filesystem_copy_file_test {
void run() {
  const fs::path sandbox{"sandbox"};
  remove_sandbox(sandbox);

  assert(fs::create_directory(sandbox));
  std::ofstream(sandbox / "file1.txt").put('a');

  assert(fs::copy_file(sandbox / "file1.txt", sandbox / "file2.txt"));
  // now there are two files in sandbox:
  std::cout << "file1.txt holds: "
            << std::ifstream(sandbox / "file1.txt").rdbuf() << '\n';
  std::cout << "file2.txt holds: "
            << std::ifstream(sandbox / "file2.txt").rdbuf() << '\n';

  // fail to copy directory
  assert(fs::create_directory(sandbox / "abc"));
  bool caught_directory_copy{};
  try {
    fs::copy_file(sandbox / "abc", sandbox / "def");
  } catch (fs::filesystem_error &e) {
    caught_directory_copy = true;
    std::cout << "Could not copy sandbox/abc: " << e.what() << '\n';
  }

  assert(caught_directory_copy);
  {
    char from_file1{};
    char from_file2{};
    std::ifstream in{sandbox / "file1.txt"};
    in.get(from_file1);
    assert(in);
    in.close();
    in.open(sandbox / "file2.txt");
    in.get(from_file2);
    assert(in);
    assert(from_file1 == 'a');
    assert(from_file2 == 'a');
  }

  assert(fs::remove_all(sandbox) >= 4);
}
} // namespace filesystem_copy_file_test

//
// https://en.cppreference.com/w/cpp/filesystem/copy#Example
//
namespace filesystem_copy_test {
void run() {
  const fs::path sandbox{"sandbox"};
  const fs::path sandbox_copy{"sandbox_copy"};
  remove_sandbox(sandbox);
  remove_sandbox(sandbox_copy);

  fs::create_directories(sandbox / "dir" / "subdir");
  std::ofstream(sandbox / "file1.txt").put('a');
  fs::copy(sandbox / "file1.txt", sandbox / "file2.txt");
  fs::copy(sandbox / "dir", sandbox / "dir2");

  const auto copy_options = fs::copy_options::update_existing |
                            fs::copy_options::recursive |
                            fs::copy_options::directories_only;
  fs::copy(sandbox, sandbox_copy, copy_options);

  assert(fs::exists(sandbox / "file2.txt"));
  assert(fs::exists(sandbox / "dir2"));
  assert(fs::exists(sandbox_copy / "dir" / "subdir"));
  assert(fs::exists(sandbox_copy / "dir2"));

  remove_sandbox(sandbox);
  remove_sandbox(sandbox_copy);
  std::cout << "filesystem copy completed\n";
}
} // namespace filesystem_copy_test

//
// https://en.cppreference.com/w/cpp/filesystem/status
// https://en.cppreference.com/w/cpp/filesystem/exists
// https://en.cppreference.com/w/cpp/filesystem/is_directory
//
namespace filesystem_status_test {
// These linked filesystem pages document query APIs rather than one standalone
// combined Example section, so use a small sandbox and verify the public status
// predicates directly.
void run() {
  const fs::path sandbox{"sandbox"};
  remove_sandbox(sandbox);

  assert(fs::create_directory(sandbox));
  std::ofstream(sandbox / "file.txt").put('a');

  std::error_code ec;
  auto file_status = fs::status(sandbox / "file.txt", ec);
  assert(!ec);
  assert(fs::status_known(file_status));
  assert(fs::exists(file_status));
  assert(fs::is_regular_file(file_status));
  assert(!fs::is_directory(file_status));

  auto directory_status = fs::status(sandbox, ec);
  assert(!ec);
  assert(fs::exists(directory_status));
  assert(fs::is_directory(directory_status));

  auto missing_status = fs::status(sandbox / "missing.txt", ec);
  assert(fs::status_known(missing_status));
  assert(!fs::exists(missing_status));

  auto symlink_status = fs::symlink_status(sandbox / "file.txt", ec);
  assert(!ec);
  assert(fs::is_regular_file(symlink_status));
  assert(!fs::is_symlink(symlink_status));

  remove_sandbox(sandbox);
  std::cout << "filesystem status assertions passed\n";
}
} // namespace filesystem_status_test

//
// https://en.cppreference.com/w/cpp/filesystem/file_size#Example
//
namespace filesystem_file_size_test {
void run() {
  const fs::path sandbox{"sandbox"};
  remove_sandbox(sandbox);
  assert(fs::create_directory(sandbox));

  const fs::path example = sandbox / "example.bin";
  std::ofstream(example).put('a'); // create file of size 1
  std::cout << example.filename() << " size = " << fs::file_size(example)
            << '\n';
  assert(fs::file_size(example) == 1);

  std::error_code ec;
  const auto missing_size = fs::file_size(sandbox / "missing.bin", ec);
  // Release builds compile out assert(), so mark assert-only locals as used.
  (void)missing_size;
  assert(ec);
  assert(missing_size == static_cast<std::uintmax_t>(-1));

  remove_sandbox(sandbox);
}
} // namespace filesystem_file_size_test

//
// https://en.cppreference.com/w/cpp/filesystem/is_empty
//
namespace filesystem_is_empty_test {
// The cppreference is_empty page has no standalone Example section; create
// explicit empty/non-empty file and directory cases in the sandbox.
void run() {
  const fs::path sandbox{"sandbox"};
  remove_sandbox(sandbox);
  assert(fs::create_directory(sandbox));

  const auto empty_file = sandbox / "empty.txt";
  std::ofstream{empty_file};
  assert(fs::is_empty(empty_file));
  std::ofstream(empty_file).put('a');
  assert(!fs::is_empty(empty_file));

  const auto empty_dir = sandbox / "empty_dir";
  assert(fs::create_directory(empty_dir));
  assert(fs::is_empty(empty_dir));
  std::ofstream(empty_dir / "file.txt").put('b');
  assert(!fs::is_empty(empty_dir));

  remove_sandbox(sandbox);
  std::cout << "filesystem is_empty assertions passed\n";
}
} // namespace filesystem_is_empty_test

//
// https://en.cppreference.com/w/cpp/filesystem/resize_file#Example
//
namespace filesystem_resize_file_test {
void run() {
  auto p = fs::temp_directory_path() / "example_resize.bin";
  std::ofstream{p}.put('a');
  std::cout << "File size:  " << fs::file_size(p) << '\n'
            << "Free space: " << fs::space(p).free << '\n';
  fs::resize_file(p, 64 * 1024);
  std::cout << "File size:  " << fs::file_size(p) << '\n'
            << "Free space: " << fs::space(p).free << '\n';
  assert(fs::file_size(p) == 64 * 1024);
  fs::resize_file(p, 1);
  assert(fs::file_size(p) == 1);
  fs::remove(p);
}
} // namespace filesystem_resize_file_test

//
// https://en.cppreference.com/w/cpp/filesystem/permissions
//
namespace filesystem_permissions_test {
// The cppreference permissions page documents the API without a standalone
// portable Example section; use a sandbox file and verify write-bit changes.
bool has_write_permissions(fs::perms perms) {
  return (perms & write_perms) != fs::perms::none;
}

void run() {
  const fs::path sandbox{"sandbox"};
  remove_sandbox(sandbox);
  assert(fs::create_directory(sandbox));

  const auto file = sandbox / "file.txt";
  std::ofstream(file).put('a');

  fs::permissions(file, write_perms, fs::perm_options::remove);
  assert(!has_write_permissions(fs::status(file).permissions()));

  fs::permissions(file, write_perms, fs::perm_options::add);
  assert(has_write_permissions(fs::status(file).permissions()));

  remove_sandbox(sandbox);
  std::cout << "filesystem permissions assertions passed\n";
}
} // namespace filesystem_permissions_test

//
// https://en.cppreference.com/w/cpp/filesystem/create_hard_link#Example
// https://en.cppreference.com/w/cpp/filesystem/hard_link_count
// https://en.cppreference.com/w/cpp/filesystem/equivalent#Example
//
namespace filesystem_hard_link_test {
void run() {
  const fs::path sandbox{"sandbox"};
  remove_sandbox(sandbox);

  fs::create_directories(sandbox / "subdir");
  std::ofstream(sandbox / "a").put('a'); // create regular file
  fs::create_hard_link(sandbox / "a", sandbox / "b");

  assert(fs::hard_link_count(sandbox / "a") >= 2);
  assert(fs::equivalent(sandbox / "a", sandbox / "b"));
  assert(fs::equivalent(fs::path{"."}, fs::current_path()));

  fs::remove(sandbox / "a");
  // read from the original file via surviving hard link
  char c = static_cast<char>(std::ifstream(sandbox / "b").get());
  std::cout << c << '\n';
  assert(c == 'a');

  fs::remove_all(sandbox);
}
} // namespace filesystem_hard_link_test

//
// https://en.cppreference.com/w/cpp/filesystem/create_symlink#Example
//
namespace filesystem_create_symlink_test {
void run() {
  const fs::path sandbox{"sandbox"};
  remove_sandbox(sandbox);

  fs::create_directories("sandbox/subdir");
  fs::create_symlink("target", "sandbox/sym1");
  fs::create_directory_symlink("subdir", "sandbox/sym2");
  std::uintmax_t symlink_count{};

  for (auto it = fs::directory_iterator("sandbox"); it != fs::directory_iterator();
       ++it) {
    if (is_symlink(it->symlink_status())) {
      std::cout << *it << "->" << read_symlink(*it) << '\n';
      ++symlink_count;
    }
  }

  assert(symlink_count == 2);
  assert(fs::read_symlink("sandbox/sym1") == "target");
  assert(fs::read_symlink("sandbox/sym2") == "subdir");
  assert(std::filesystem::equivalent("sandbox/sym2", "sandbox/subdir"));

  fs::remove_all("sandbox");
}
} // namespace filesystem_create_symlink_test

//
// https://en.cppreference.com/w/cpp/filesystem/read_symlink#Example
//
namespace filesystem_read_symlink_test {
void run() {
  const fs::path sandbox{"sandbox"};
  remove_sandbox(sandbox);

  fs::create_directories(sandbox);
  std::ofstream(sandbox / "cat").put('c');
  fs::create_symlink("gcc-5", sandbox / "gcc");

  for (fs::path p : {sandbox / "gcc", sandbox / "cat", sandbox / "mouse"}) {
    std::cout << p;
    fs::exists(p) ? fs::is_symlink(p)
                        ? std::cout << " -> " << fs::read_symlink(p) << '\n'
                        : std::cout << " exists but it is not a symlink\n"
                  : std::cout << " does not exist\n";
  }

  assert(fs::is_symlink(sandbox / "gcc"));
  assert(fs::read_symlink(sandbox / "gcc") == "gcc-5");
  assert(!fs::is_symlink(sandbox / "cat"));
  assert(!fs::exists(sandbox / "mouse"));

  remove_sandbox(sandbox);
}
} // namespace filesystem_read_symlink_test

//
// https://en.cppreference.com/w/cpp/filesystem/copy_symlink
//
namespace filesystem_copy_symlink_test {
// The cppreference copy_symlink page has no standalone Example section; create
// a sandbox symlink and verify the copied link still resolves to the target.
void run() {
  const fs::path sandbox{"sandbox"};
  remove_sandbox(sandbox);

  fs::create_directories(sandbox);
  std::ofstream(sandbox / "target.txt").put('t');
  fs::create_symlink("target.txt", sandbox / "source-link");
  fs::copy_symlink(sandbox / "source-link", sandbox / "copied-link");

  assert(fs::is_symlink(sandbox / "source-link"));
  assert(fs::is_symlink(sandbox / "copied-link"));
  assert(fs::read_symlink(sandbox / "copied-link") == "target.txt");
  assert(fs::equivalent(sandbox / "copied-link", sandbox / "target.txt"));

  remove_sandbox(sandbox);
  std::cout << "filesystem copy_symlink assertions passed\n";
}
} // namespace filesystem_copy_symlink_test

//
// https://en.cppreference.com/w/cpp/filesystem/directory_entry
//
namespace filesystem_directory_entry_test {
// The cppreference directory_entry page has no standalone Example section; use
// a sandbox file to verify cached metadata, refresh(), and status forwarding.
void run() {
  const fs::path sandbox{"sandbox"};
  remove_sandbox(sandbox);
  assert(fs::create_directory(sandbox));

  const auto file = sandbox / "file.txt";
  std::ofstream(file) << "ab";

  fs::directory_entry entry{file};
  assert(entry.exists());
  assert(entry.is_regular_file());
  assert(!entry.is_directory());
  assert(entry.file_size() == 2);
  assert(entry.hard_link_count() >= 1);
  assert(fs::is_regular_file(entry.status()));

  std::ofstream(file, std::ios::app).put('c');
  entry.refresh();
  assert(entry.file_size() == 3);

  remove_sandbox(sandbox);
  std::cout << "filesystem directory_entry assertions passed\n";
}
} // namespace filesystem_directory_entry_test

//
// https://en.cppreference.com/w/cpp/filesystem/space#Example
//
namespace filesystem_space_test {
void run() {
  std::error_code ec;
  const fs::space_info si = fs::space(fs::current_path(), ec);
  const auto unknown = static_cast<std::uintmax_t>(-1);
  // Release builds compile out assert(), so mark assert-only locals as used.
  (void)unknown;

  assert(!ec);
  assert(si.capacity != unknown);
  assert(si.free != unknown);
  assert(si.available != unknown);
  assert(si.capacity >= si.free);
  assert(si.free >= si.available);

  std::cout << "filesystem space capacity=" << si.capacity
            << " free=" << si.free << " available=" << si.available << '\n';
}
} // namespace filesystem_space_test

//
// https://en.cppreference.com/w/cpp/filesystem/rename#Example
//
namespace filesystem_rename_test {
void run() {
  // cppreference uses relative paths. Anchor the same shape under current_path()
  // so cleanup targets the driver test sandbox even after earlier examples.
  std::filesystem::path p = std::filesystem::current_path() / "sandbox";
  remove_sandbox(p);
  std::filesystem::create_directories(p / "from");
  std::ofstream{ p / "from/file1.txt" }.put('a');
  std::filesystem::create_directory(p / "to");
  //  fs::rename(p / "from/file1.txt", p / "to/"); // error: "to" is a directory
  fs::rename(p / "from/file1.txt", p / "to/file2.txt"); // OK
  //  fs::rename(p / "from", p / "to"); // error: "to" is not empty
  fs::rename(p / "from", p / "to/subdir"); // OK

  std::filesystem::remove_all(p);
  std::cout << "filesystem rename completed\n";
}
} // namespace filesystem_rename_test

//
// https://en.cppreference.com/w/cpp/filesystem/temp_directory_path#Example
//
namespace filesystem_temp_directory_path_test {
void run() {
  std::cout << "Temp directory is " << fs::temp_directory_path() << '\n';
}
} // namespace filesystem_temp_directory_path_test

//
// https://en.cppreference.com/w/cpp/filesystem/last_write_time#Example
//
namespace filesystem_last_write_time_test {
void run() {
  auto p = std::filesystem::temp_directory_path() / "example.bin";
  std::ofstream{p.c_str()}.put('a'); // create file

  std::filesystem::file_time_type ftime = std::filesystem::last_write_time(p);
  std::cout << std::format("File write time is {}\n", ftime);

  // move file write time 1 hour to the future
  std::filesystem::last_write_time(p, ftime + 1h);

  // read back from the filesystem
  ftime = std::filesystem::last_write_time(p);
  std::cout << std::format("File write time is {}\n", ftime);

  std::filesystem::remove(p);
}
} // namespace filesystem_last_write_time_test

//
// https://en.cppreference.com/w/cpp/filesystem/canonical#Example
//
namespace filesystem_canonical_test {
class scoped_current_path {
public:
  scoped_current_path() : old_(std::filesystem::current_path()) {}
  ~scoped_current_path() { std::filesystem::current_path(old_); }

  scoped_current_path(const scoped_current_path &) = delete;
  scoped_current_path &operator=(const scoped_current_path &) = delete;

private:
  std::filesystem::path old_;
};

void run() {
  scoped_current_path restore_current_path;

  auto tmp = std::filesystem::temp_directory_path();
  remove_sandbox(tmp / "a");
  std::filesystem::current_path(tmp);

  auto d1 = tmp / "a/b/c1/d";
  auto d2 = tmp / "a/b/c2/e";
  std::filesystem::create_directories(d1);
  std::filesystem::create_directories(d2);
  std::filesystem::current_path(d1);

  auto p1 = std::filesystem::path("../../c2/./e");
  auto p2 = std::filesystem::path("../no-such-file");

  std::cout << "Current path is " << std::filesystem::current_path() << '\n'
            << "Canonical path for " << p1 << " is "
            << std::filesystem::canonical(p1) << '\n'
            << "Weakly canonical path for " << p2 << " is "
            << std::filesystem::weakly_canonical(p2) << '\n';

  bool caught_missing_path{};
  try {
    [[maybe_unused]] auto x_x = std::filesystem::canonical(p2);
  } catch (const std::exception &ex) {
    caught_missing_path = true;
    std::cout << "Canonical path for " << p2 << " threw exception:\n"
              << ex.what() << '\n';
  }

  assert(caught_missing_path);
  // cppreference leaves the current path inside "a"; move out before cleanup
  // because Windows can reject deleting the current directory tree.
  std::filesystem::current_path(tmp);
  const auto count = std::filesystem::remove_all(tmp / "a");
  std::cout << "Deleted " << count << " files or directories.\n";
}
} // namespace filesystem_canonical_test

//
// https://en.cppreference.com/w/cpp/filesystem/status
// https://en.cppreference.com/w/cpp/filesystem/equivalent
// https://en.cppreference.com/w/cpp/filesystem/file_size
// https://en.cppreference.com/w/cpp/filesystem/copy_file
// https://en.cppreference.com/w/cpp/filesystem/rename
// https://en.cppreference.com/w/cpp/filesystem/permissions
// https://en.cppreference.com/w/cpp/filesystem/remove
// https://en.cppreference.com/w/cpp/filesystem/resize_file
// https://en.cppreference.com/w/cpp/filesystem/last_write_time
// https://en.cppreference.com/w/cpp/filesystem/canonical
//
namespace filesystem_error_code_overload_test {
void run() {
  const fs::path sandbox{"sandbox_fs_error_code"};
  remove_sandbox(sandbox);

  assert(fs::create_directory(sandbox));
  assert(fs::create_directory(sandbox / "dir"));
  std::ofstream(sandbox / "file.txt").put('x');

  std::error_code ec;
  assert(fs::exists(sandbox / "file.txt", ec));
  assert(!ec);

  const auto st = fs::status(sandbox / "file.txt", ec);
  assert(!ec);
  assert(fs::is_regular_file(st));

  const auto same =
      fs::equivalent(sandbox / "file.txt", sandbox / "missing.txt", ec);
  assert(!same);
  assert(ec);

  ec.clear();
  // Directory size reporting is implementation-defined for std::filesystem;
  // Windows-style providers can report a zero-sized directory without setting
  // ec. Use a missing path for the non-throwing file_size error contract.
  const auto bad_size = fs::file_size(sandbox / "missing-size.txt", ec);
  assert(ec);
  assert(bad_size == static_cast<std::uintmax_t>(-1));

  ec.clear();
  const bool copied =
      fs::copy_file(sandbox / "missing-source.txt", sandbox / "copy.txt", ec);
  assert(!copied);
  assert(ec);

  ec.clear();
  fs::rename(sandbox / "missing-rename.txt", sandbox / "renamed.txt", ec);
  assert(ec);

  ec.clear();
  fs::permissions(sandbox / "missing-permissions.txt",
                  fs::perms::owner_read, ec);
  assert(ec);

  ec.clear();
  const bool removed = fs::remove(sandbox / "missing-remove.txt", ec);
  assert(!removed);
  assert(!ec);

  ec.clear();
  const auto removed_count = fs::remove_all(sandbox / "missing-remove-all", ec);
  assert(removed_count == 0);
  assert(!ec);

  ec.clear();
  fs::resize_file(sandbox / "missing-resize.txt", 42, ec);
  assert(ec);

  ec.clear();
  (void)fs::last_write_time(sandbox / "missing-time.txt", ec);
  assert(ec);

  ec.clear();
  (void)fs::canonical(sandbox / "missing-canonical.txt", ec);
  assert(ec);

  remove_sandbox(sandbox);
}
} // namespace filesystem_error_code_overload_test

//
// https://en.cppreference.com/w/cpp/filesystem/directory_entry/refresh
//
namespace filesystem_directory_entry_refresh_test {
void run() {
  const fs::path sandbox{"sandbox_fs_refresh"};
  remove_sandbox(sandbox);

  assert(fs::create_directory(sandbox));
  const fs::path target = sandbox / "entry";
  std::ofstream(target).put('x');

  std::error_code ec;
  fs::directory_entry entry{target};
  assert(entry.is_regular_file(ec));
  assert(!ec);

  assert(fs::remove(target));
  assert(fs::create_directory(target));

  entry.refresh(ec);
  assert(!ec);
  assert(entry.is_directory(ec));
  assert(!ec);

  remove_sandbox(sandbox);
}
} // namespace filesystem_directory_entry_refresh_test

//
// https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator
// https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator/disable_recursion_pending
// https://en.cppreference.com/w/cpp/filesystem/recursive_directory_iterator/increment
//
namespace filesystem_recursive_directory_options_test {
void run() {
  const fs::path sandbox{"sandbox_fs_recursive_options"};
  remove_sandbox(sandbox);

  assert(fs::create_directories(sandbox / "skip" / "child"));
  assert(fs::create_directories(sandbox / "keep"));
  std::ofstream(sandbox / "skip" / "child" / "hidden.txt").put('h');
  std::ofstream(sandbox / "keep" / "visible.txt").put('v');

  std::error_code ec;
  std::size_t visited_files = 0;
  for (fs::recursive_directory_iterator it{
           sandbox, fs::directory_options::none, ec},
       end;
       it != end;) {
    assert(!ec);

    if (it->path().filename() == "skip") {
      assert(it.recursion_pending());
      it.disable_recursion_pending();
      assert(!it.recursion_pending());
    }

    if (it->is_regular_file(ec)) {
      assert(!ec);
      assert(it->path().filename() != "hidden.txt");
      ++visited_files;
    } else {
      assert(!ec);
    }

    it.increment(ec);
    assert(!ec);
  }

  assert(visited_files == 1);
  remove_sandbox(sandbox);
}
} // namespace filesystem_recursive_directory_options_test
