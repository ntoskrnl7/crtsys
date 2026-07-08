//
// https://en.cppreference.com/w/cpp/iterator/distance#Example
//
#include <algorithm>
#include <cmath>
#include <complex>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <valarray>
#include <vector>

namespace iterator_distance_test {
void run() {
  std::vector<int> v{3, 1, 4};
  std::cout << "distance(first, last) = "
            << std::distance(v.begin(), v.end()) << '\n'
            << "distance(last, first) = "
            << std::distance(v.end(), v.begin()) << '\n';

  static constexpr auto il = {3, 1, 4};
  // Since C++17 distance can be used in constexpr context.
  static_assert(std::distance(il.begin(), il.end()) == 3);
  static_assert(std::distance(il.end(), il.begin()) == -3);
}
} // namespace iterator_distance_test

//
// https://en.cppreference.com/w/cpp/iterator/advance#Example
//
namespace iterator_advance_test {
void run() {
  std::vector<int> v{3, 1, 4};

  auto vi = v.begin();
  std::advance(vi, 2);
  std::cout << *vi << ' ';

  vi = v.end();
  std::advance(vi, -2);
  std::cout << *vi << '\n';
}
} // namespace iterator_advance_test

//
// https://en.cppreference.com/w/cpp/iterator/next#Example
//
namespace iterator_next_test {
void run() {
  std::vector<int> v{4, 5, 6};

  auto it = v.begin();
  auto nx = std::next(it, 2);
  std::cout << *it << ' ' << *nx << '\n';

  it = v.end();
  nx = std::next(it, -2);
  std::cout << ' ' << *nx << '\n';
}
} // namespace iterator_next_test

//
// https://en.cppreference.com/w/cpp/iterator/back_inserter#Example
//
namespace back_inserter_test {
void run() {
  std::vector<int> v{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  std::fill_n(std::back_inserter(v), 3, -1);
  for (int n : v) {
    std::cout << n << ' ';
  }
  std::cout << '\n';
}
} // namespace back_inserter_test

//
// https://en.cppreference.com/w/cpp/numeric/complex#Example
//
namespace complex_test {
void run() {
  using namespace std::complex_literals;
  std::cout << std::fixed << std::setprecision(1);

  std::complex<double> z1 = 1i * 1i; // imaginary unit squared
  std::cout << "i * i = " << z1 << '\n';

  std::complex<double> z2 = 1.0 + 2i, z3 = 1.0 - 2i; // conjugates
  std::cout << "(1+2i)*(1-2i) = " << z2 * z3 << '\n';

  const double pi = std::acos(-1.0);
  const std::complex<double> i(0.0, 1.0);
  std::cout << "exp(i * pi) = " << std::exp(i * pi) << '\n';

  std::complex<double> z4(1.0, 2.0);
  std::cout << "(1,2)^2 = " << std::pow(z4, 2) << '\n';

  std::complex<double> z5(-1.0, 0.0);
  std::cout << "-1^0.5 = " << std::pow(z5, 0.5) << '\n';

  std::complex<double> z6(-1.0, -0.0);
  std::cout << "(-1,-0)^0.5 = " << std::pow(z6, 0.5) << '\n';

  std::cout << "i^i = " << std::pow(i, i) << '\n';
}
} // namespace complex_test

//
// https://en.cppreference.com/w/cpp/numeric/valarray/slice#Example
//
namespace valarray_slice_test {
class Matrix {
  std::valarray<int> data;
  int dim;

public:
  Matrix(int r, int c) : data(r * c), dim(c) {}
  int &operator()(int r, int c) { return data[r * dim + c]; }
  int trace() const { return data[std::slice(0, dim, dim + 1)].sum(); }
};

void run() {
  Matrix m(3, 3);
  int n = 0;
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      m(r, c) = ++n;
    }
  }
  std::cout << "Trace of the matrix (1,2,3) (4,5,6) (7,8,9) is "
            << m.trace() << '\n';
}
} // namespace valarray_slice_test

//
// https://en.cppreference.com/w/cpp/numeric/random/random_device#Example
//
namespace random_device_test {
void run() {
  std::random_device rd;
  std::map<int, int> hist;
  std::uniform_int_distribution<int> dist(0, 9);
  for (int n = 0; n != 20000; ++n) {
    ++hist[dist(rd)]; // note: demo only: the performance of many
                      // implementations of random_device degrades sharply
                      // once the entropy pool is exhausted. For practical use
                      // random_device is generally only used to seed
                      // a PRNG such as mt19937
  }
  for (auto [x, y] : hist) {
    std::cout << x << " : " << std::string(y / 100, '*') << '\n';
  }
}
} // namespace random_device_test

namespace random_device_semantic_test {
namespace {
void expect(bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
} // namespace

void run() {
  std::random_device rd;
  const auto min = rd.min();
  const auto max = rd.max();
  expect(min < max, "random_device reported an invalid range");

  std::random_device::result_type samples[8]{};
  bool saw_difference = false;
  for (auto &sample : samples) {
    sample = rd();
    expect(sample <= max,
           "random_device sample was outside its advertised range");
    saw_difference = saw_difference || sample != samples[0];
  }

  expect(saw_difference, "random_device returned repeated identical samples");
  std::cout << "random_device semantic assertions passed\n";
}
} // namespace random_device_semantic_test

//
// https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution#Example
//
namespace uniform_int_distribution_test {
void run() {
  std::random_device rd;  // a seed source for the random number engine
  std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()
  std::uniform_int_distribution<> distrib(1, 6);

  // Use distrib to transform the random unsigned int
  // generated by gen into an int in [1, 6]
  for (int n = 0; n != 10; ++n) {
    std::cout << distrib(gen) << ' ';
  }
  std::cout << '\n';
}
} // namespace uniform_int_distribution_test
