//
// https://en.cppreference.com/w/cpp/io/manip/quoted#Example
//
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <span>
#if __has_include(<spanstream>)
#include <spanstream>
#endif
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
namespace fs = std::filesystem;

void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void remove_stream_sandbox(const fs::path &path) {
  std::error_code ec;
  fs::remove_all(path, ec);
}

class stream_sandbox {
public:
  explicit stream_sandbox(fs::path path) : path_(std::move(path)) {
    remove_stream_sandbox(path_);
    std::error_code ec;
    if (!fs::create_directory(path_, ec)) {
      // A previous debugger-interrupted VM run can leave the sandbox behind.
      expect(fs::is_directory(path_), "failed to create stream sandbox");
    }
  }

  ~stream_sandbox() { remove_stream_sandbox(path_); }

  const fs::path &path() const noexcept { return path_; }

private:
  fs::path path_;
};
} // namespace

namespace quoted_test {
void default_delimiter() {
  const std::string in =
      "std::quoted() quotes this string and embedded \"quotes\" too";
  std::stringstream ss;
  ss << std::quoted(in);
  std::string out;
  ss >> std::quoted(out);
  std::cout << "Default delimiter case:\n"
               "read in     ["
            << in << "]\n"
            << "stored as   [" << ss.str() << "]\n"
            << "written out [" << out << "]\n\n";
}

void custom_delimiter() {
  const char delim{'$'};
  const char escape{'%'};
  const std::string in =
      "std::quoted() quotes this string and embedded $quotes$ $too";
  std::stringstream ss;
  ss << std::quoted(in, delim, escape);
  std::string out;
  ss >> std::quoted(out, delim, escape);

  std::cout << "Custom delimiter case:\n"
               "read in     ["
            << in << "]\n"
            << "stored as   [" << ss.str() << "]\n"
            << "written out [" << out << "]\n\n";
}

void run() {
  default_delimiter();
  custom_delimiter();
}
} // namespace quoted_test

//
// https://en.cppreference.com/w/cpp/io/basic_filebuf/underflow#Example
//
namespace filebuf_underflow_test {
struct mybuf : std::filebuf {
  int underflow() override {
    std::cout << "Before underflow(): size of the get area is "
              << egptr() - eback() << " with " << egptr() - gptr()
              << " read positions available\n";
    int rc = std::filebuf::underflow();
    std::cout << "underflow() returns " << rc << ".\n"
              << "After the call, size of the get area is " << egptr() - eback()
              << " with " << egptr() - gptr()
              << " read positions available\n";
    return rc;
  }
};

void run() {
  stream_sandbox sandbox{"crtsys-stream-filebuf-sandbox"};
  const auto filename = sandbox.path() / "test.txt";

  // The cppreference example opens an existing "test.txt". The driver harness
  // creates the file inside a per-test sandbox so repeated runs can clean up.
  {
    std::ofstream out{filename};
    expect(static_cast<bool>(out), "failed to create filebuf input file");
    out << "I/O stream buffer test\n";
  }

  mybuf buf;
  expect(buf.open(filename.c_str(), std::ios_base::in) != nullptr,
         "failed to open filebuf input file");
  std::istream stream(&buf);
  int chars = 0;
  // The cppreference example uses `while (stream.get()) ;`. The no-argument
  // `istream::get()` returns `int_type`; EOF is commonly -1 and therefore
  // truthy, so this driver harness compares against EOF explicitly.
  while (stream.get() != std::char_traits<char>::eof()) {
    ++chars;
  }
  expect(chars > 0, "filebuf example did not read any characters");
}
} // namespace filebuf_underflow_test

//
// https://en.cppreference.com/w/cpp/io/basic_filebuf/open#Example
//
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4456) // cppreference uses `d` and `d2` in one scope.
#endif
namespace filebuf_open_test {
void run() {
  stream_sandbox sandbox{"crtsys-stream-filebuf-open-sandbox"};

  // The cppreference example uses "Test.b" in the process current directory.
  // Keep the filebuf operations and binary payload, but isolate the file.
  std::string filename = (sandbox.path() / "Test.b").string();
  std::filebuf fb;

  double d = 3.14;
  if (!fb.open(filename, std::ios::binary | std::ios::out)) {
    std::cout << "Open file " << filename << " for write failed\n";
    throw std::runtime_error("filebuf open for write failed");
  }
  fb.sputn(reinterpret_cast<char *>(&d), sizeof d);
  expect(fb.close() != nullptr, "filebuf close after write failed");

  double d2 = 0.0;
  if (!fb.open(filename, std::ios::binary | std::ios::in)) {
    std::cout << "Open file " << filename << " for read failed\n";
    throw std::runtime_error("filebuf open for read failed");
  }

  auto got = fb.sgetn(reinterpret_cast<char *>(&d2), sizeof d2);
  if (sizeof(d2) != got) {
    std::cout << "Read of " << filename << " failed\n";
    throw std::runtime_error("filebuf binary read failed");
  } else {
    std::cout << "Read back from file: " << d2 << '\n';
    expect(d2 == 3.14, "filebuf binary payload mismatch");
  }
}
} // namespace filebuf_open_test
#ifdef _MSC_VER
#pragma warning(pop)
#endif

//
// https://en.cppreference.com/w/cpp/io/basic_filebuf/is_open#Example
//
namespace filebuf_is_open_test {
void run() {
  stream_sandbox sandbox{"crtsys-stream-filebuf-is-open-sandbox"};
  const auto filename = sandbox.path() / "test.txt";

  // The cppreference example assumes "test.txt" exists next to the program.
  // The driver harness creates that file in its sandbox before opening it.
  {
    std::ofstream out{filename};
    expect(static_cast<bool>(out), "failed to create filebuf is_open input");
    out << "test";
  }

  std::ifstream fs(filename);
  std::filebuf fb;
  fb.open(filename, std::ios_base::in);
  std::cout << std::boolalpha << "direct call: " << fb.is_open() << '\n'
            << "through streambuf: " << fs.rdbuf()->is_open() << '\n'
            << "through fstream: " << fs.is_open() << '\n';
  expect(fb.is_open(), "filebuf direct is_open failed");
  expect(fs.rdbuf()->is_open(), "ifstream rdbuf is_open failed");
  expect(fs.is_open(), "ifstream is_open failed");
}
} // namespace filebuf_is_open_test

//
// https://en.cppreference.com/w/cpp/io/basic_filebuf/seekoff#Example
//
namespace filebuf_seekoff_test {
template <typename CharT>
int get_encoding(const std::basic_istream<CharT> &stream) {
  using Facet = std::codecvt<CharT, char, std::mbstate_t>;
  return std::use_facet<Facet>(stream.getloc()).encoding();
}

void run() {
  stream_sandbox sandbox{"crtsys-stream-filebuf-seekoff-sandbox"};
  const auto filename = sandbox.path() / "text.txt";

  // The cppreference example uses a UTF-8 payload and a fixed "text.txt".
  // Keep the byte payload, but place the file in the per-test sandbox.
  std::ofstream(filename) << "\x7a\xc3\x9f\xe6\xb0\xb4\xf0\x9d\x84\x8b";

  std::ifstream f1(filename);
  expect(f1.is_open(), "failed to open seekoff narrow input");
  const auto f1_encoding = get_encoding(f1);
  const auto f1_pos3 = f1.rdbuf()->pubseekoff(3, std::ios_base::beg);
  const auto f1_end = f1.rdbuf()->pubseekoff(0, std::ios_base::end);
  std::cout << "f1's locale's encoding() returns " << f1_encoding << '\n'
            << "pubseekoff(3, beg) returns " << f1_pos3 << '\n'
            << "pubseekoff(0, end) returns " << f1_end << '\n';
  expect(f1_pos3 == std::ifstream::pos_type(3),
         "narrow filebuf seekoff to byte offset failed");
  expect(f1_end == std::ifstream::pos_type(10),
         "narrow filebuf seekoff to end failed");

  std::wifstream f2(filename);
  f2.imbue(std::locale("en_US.UTF-8"));
  expect(f2.is_open(), "failed to open seekoff wide input");
  const auto f2_encoding = get_encoding(f2);
  const auto f2_pos3 = f2.rdbuf()->pubseekoff(3, std::ios_base::beg);
  const auto f2_end = f2.rdbuf()->pubseekoff(0, std::ios_base::end);
  std::cout << "f2's locale's encoding() returns " << f2_encoding << '\n'
            << "pubseekoff(3, beg) returns " << f2_pos3 << '\n'
            << "pubseekoff(0, end) returns " << f2_end << '\n';
  expect(f2_end == std::wifstream::pos_type(10),
         "wide filebuf seekoff to end failed");
}
} // namespace filebuf_seekoff_test

//
// https://en.cppreference.com/w/cpp/io/basic_filebuf/seekpos#Example
//
namespace filebuf_seekpos_test {
struct mybuf : std::filebuf {
  pos_type seekpos(pos_type sp, std::ios_base::openmode which) override {
    std::cout << "Before seekpos(" << sp
              << "), size of the get area is " << egptr() - eback()
              << " with " << egptr() - gptr()
              << " read positions available.\n";

    pos_type rc = std::filebuf::seekpos(sp, which);
    std::cout << "seekpos() returns " << rc << ".\n After the call, "
              << "size of the get area is " << egptr() - eback() << " with "
              << egptr() - gptr() << " read positions available.\n";
    return rc;
  }
};

void run() {
  stream_sandbox sandbox{"crtsys-stream-filebuf-seekpos-sandbox"};
  const auto filename = sandbox.path() / "test.txt";

  // The cppreference example opens an existing "test.txt". The driver harness
  // creates it inside the sandbox before exercising seekpos through seekg().
  {
    std::ofstream out{filename};
    expect(static_cast<bool>(out), "failed to create filebuf seekpos input");
    out << "abcdef";
  }

  mybuf buf;
  expect(buf.open(filename.c_str(), std::ios_base::in) != nullptr,
         "failed to open filebuf seekpos input");
  std::istream stream(&buf);
  expect(stream.get() != std::char_traits<char>::eof(),
         "failed to read initial seekpos character");
  stream.seekg(2);
  expect(static_cast<bool>(stream), "istream seekg through seekpos failed");
  expect(stream.get() == 'c', "seekpos did not reposition to expected byte");
}
} // namespace filebuf_seekpos_test

//
// https://en.cppreference.com/w/cpp/io/basic_ifstream#Example
//
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4456) // cppreference reuses `d` in the readback block.
#endif
namespace ifstream_test {
void run() {
  stream_sandbox sandbox{"crtsys-stream-ifstream-sandbox"};

  // The cppreference example uses "Test.b" in the process current directory.
  // Keep the example's file contents, but place the file in a private sandbox.
  std::string filename = (sandbox.path() / "Test.b").string();

  double d = 3.14;
  std::ofstream(filename, std::ios::binary)
      .write(reinterpret_cast<char *>(&d), sizeof d)
      << 123 << "abc";

  std::ifstream istrm(filename, std::ios::binary);
  if (!istrm.is_open()) {
    std::cout << "failed to open " << filename << '\n';
    throw std::runtime_error("failed to open ifstream input file");
  } else {
    double d;
    istrm.read(reinterpret_cast<char *>(&d), sizeof d);
    int n;
    std::string s;
    if (istrm >> n >> s) {
      std::cout << "read back from file: " << d << ' ' << n << ' ' << s
                << '\n';
      expect(d == 3.14, "ifstream double payload mismatch");
      expect(n == 123, "ifstream integer payload mismatch");
      expect(s == "abc", "ifstream string payload mismatch");
    } else {
      throw std::runtime_error("ifstream formatted extraction failed");
    }
  }
}
} // namespace ifstream_test
#ifdef _MSC_VER
#pragma warning(pop)
#endif

//
// https://en.cppreference.com/w/cpp/io/basic_ifstream/is_open#Example
//
namespace ifstream_is_open_test {
bool file_exists(const std::string &str) {
  std::ifstream fs(str);
  return fs.is_open();
}

void run() {
  stream_sandbox sandbox{"crtsys-stream-ifstream-is-open-sandbox"};
  const auto main_cpp = (sandbox.path() / "main.cpp").string();
  const auto strange_file = (sandbox.path() / "strange_file").string();

  // The cppreference example says "this file is called main.cpp". In the
  // driver harness, create that file explicitly inside the sandbox.
  {
    std::ofstream out{main_cpp};
    expect(static_cast<bool>(out), "failed to create ifstream is_open input");
    out << "int main() {}\n";
  }

  std::boolalpha(std::cout);
  const bool existing = file_exists(main_cpp);
  const bool missing = file_exists(strange_file);
  std::cout << existing << '\n' << missing << '\n';
  expect(existing, "ifstream is_open did not find existing file");
  expect(!missing, "ifstream is_open unexpectedly found missing file");
}
} // namespace ifstream_is_open_test

//
// https://en.cppreference.com/w/cpp/io/basic_ofstream/basic_ofstream#Example
//
namespace ofstream_constructor_test {
void run() {
  stream_sandbox sandbox{"crtsys-stream-ofstream-sandbox"};

  // The cppreference example uses fixed filenames. The driver harness maps the
  // same constructor shapes to sandboxed paths and writes through the streams
  // so /WX does not flag the constructor-only locals as unused.
  std::ofstream f0;
  std::ofstream f1((sandbox.path() / "test.bin").string(), std::ios::binary);

  std::string name = (sandbox.path() / "example.txt").string();
  std::ofstream f2(name);

  std::ofstream f3(std::move(f1));

  expect(!f0.is_open(), "default ofstream should not open a file");
  expect(f2.is_open(), "ofstream string constructor did not open a file");
  expect(f3.is_open(), "ofstream move constructor did not preserve open file");

  f2 << "example";
  f3 << "binary";
  expect(static_cast<bool>(f2), "ofstream text write failed");
  expect(static_cast<bool>(f3), "ofstream binary write failed");
}
} // namespace ofstream_constructor_test

//
// https://en.cppreference.com/w/cpp/io/basic_fstream#Example
//
namespace fstream_test {
void run() {
  stream_sandbox sandbox{"crtsys-stream-fstream-sandbox"};

  // The cppreference example uses "test.bin" in the process current directory.
  // Keep the stream mode and payload, but isolate the file for the driver run.
  std::string filename{(sandbox.path() / "test.bin").string()};

  std::fstream s{filename, s.binary | s.trunc | s.in | s.out};
  if (!s.is_open()) {
    std::cout << "failed to open " << filename << '\n';
    throw std::runtime_error("failed to open fstream file");
  } else {
    double d = 3.14;
    s.write(reinterpret_cast<char *>(&d), sizeof d);
    s << 123 << "abc";
    s.seekp(0);

    d = 2.71;
    s.read(reinterpret_cast<char *>(&d), sizeof d);
    int n;
    std::string str;
    if (s >> n >> str) {
      std::cout << "read back from file: " << d << ' ' << n << ' ' << str
                << '\n';
      expect(d == 3.14, "fstream double payload mismatch");
      expect(n == 123, "fstream integer payload mismatch");
      expect(str == "abc", "fstream string payload mismatch");
    } else {
      throw std::runtime_error("fstream formatted extraction failed");
    }
  }
}
} // namespace fstream_test

//
// https://en.cppreference.com/w/cpp/io/basic_fstream/open#Example
//
namespace fstream_open_test {
void run() {
  stream_sandbox sandbox{"crtsys-stream-fstream-open-sandbox"};

  // The cppreference example uses a fixed "example.123" in the current
  // directory. Keep the same open/create/reopen flow inside the sandbox.
  std::string filename = (sandbox.path() / "example.123").string();

  std::fstream fs;
  fs.open(filename);

  if (!fs.is_open()) {
    fs.clear();
    fs.open(filename, std::ios::out); // create file
    expect(fs.is_open(), "fstream open did not create output file");
    fs.close();
    fs.open(filename);
  }
  std::cout << std::boolalpha;
  std::cout << "fs.is_open() = " << fs.is_open() << '\n';
  std::cout << "fs.good() = " << fs.good() << '\n';
  expect(fs.is_open(), "fstream open example did not open file");
  expect(fs.good(), "fstream open example stream is not good");
}
} // namespace fstream_open_test

//
// https://en.cppreference.com/w/cpp/io/basic_fstream/is_open#Example
//
namespace fstream_is_open_test {
void run() {
  stream_sandbox sandbox{"crtsys-stream-fstream-is-open-sandbox"};

  // The cppreference example uses "some_file" in the current directory. Keep
  // the missing-file then create-file behavior in a sandboxed path.
  std::string filename = (sandbox.path() / "some_file").string();

  std::fstream fs(filename, std::ios::in);

  std::cout << std::boolalpha;
  std::cout << "fs.is_open() = " << fs.is_open() << '\n';
  expect(!fs.is_open(), "fstream is_open missing-file branch did not fail");

  if (!fs.is_open()) {
    fs.clear();
    fs.open(filename, std::ios::out);
    std::cout << "fs.is_open() = " << fs.is_open() << '\n';
    expect(fs.is_open(), "fstream is_open create branch did not open");
  }
}
} // namespace fstream_is_open_test

//
// https://en.cppreference.com/w/cpp/io/basic_spanstream/span#Example
//
namespace spanstream_test {
void run() {
#if defined(__cpp_lib_spanstream) && __cpp_lib_spanstream >= 202106L
  char out_buf[16];
  std::ospanstream ost{std::span<char>{out_buf}};
  ost << "C++" << ' ' << 23 << '\0'; // note explicit null-termination
  auto sp = ost.span();
  assert(sp[0] == 'C' && sp[1] == '+' && sp[2] == '+' && sp[3] == ' ' &&
         sp[4] == '2' && sp[5] == '3' && sp[6] == '\0');
  std::cout << "sp.data(): [" << sp.data() << "]\n";
  std::cout << "out_buf: [" << out_buf << "]\n";
  // spanstream uses out_buf as internal storage, no allocations
  assert(static_cast<char *>(out_buf) == sp.data());
  const char in_buf[] = "X Y 42";
  std::ispanstream ist{std::span<const char>{in_buf}};
  assert(static_cast<const char *>(in_buf) == ist.span().data());
  char c;
  ist >> c;
  assert(c == 'X');
  ist >> c;
  assert(c == 'Y');
  int i;
  ist >> i;
  assert(i == 42);
  ist >> i; // buffer is exhausted
  assert(!ist);
  // Release builds compile out assert(), so mark assert-only locals as used.
  (void)c;
  (void)i;
#else
  std::cout << "std::spanstream is not available in this MSVC STL\n";
#endif
}
} // namespace spanstream_test
