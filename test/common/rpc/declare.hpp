#pragma once

#include <map>
#include <set>
#include <vector>

#include <ntl/deps/zpp/serializer.h>

#define NTL_RPC_NAME L"test_rpc"

class point {
public:
  point() = default;
  point(int x, int y) noexcept : m_x(x), m_y(y) {}
  friend zpp::serializer::access;
  template <typename Archive, typename Self>
  static void serialize(Archive &archive, Self &self) {
    archive(self.m_x, self.m_y);
  }
  int get_x() const noexcept { return m_x; }
  int get_y() const noexcept { return m_y; }

private:
  int m_x = 0;
  int m_y = 0;
};