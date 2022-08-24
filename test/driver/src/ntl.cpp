#include <ntl/expand_stack>

#include <string>

bool ntl_expand_stack_test() {
  long result = 0;
  ntl::expand_stack(
      [&result](int i, long l, double d) { result = (long)(i + l + d); }, 1, 2,
      3.0);
  if (result != 6)
    return false;

  result = ntl::expand_stack(
      [](int i, long l, double d) -> long { return (long)(i + l + d); }, 1, 2,
      3.0);
  if (result != 6)
    return false;

  std::string t1;
  std::string t2;
  std::string t3;

  ntl::expand_stack([&] {
    try {
      throw std::bad_alloc();
    } catch (const std::exception &e) {
      try {
        t1 = e.what();
        throw std::bad_array_new_length();
      } catch (const std::exception &e) {
        try {
          t2 = e.what();
          throw std::runtime_error("test");
        } catch (const std::runtime_error &e) {
          t3 = e.what();
        } catch (const std::exception &) {
        }
      }
    }
  });
  return !(t1.empty() || t2.empty() || t3.empty());
}

#include <ntl/irql>

bool ntl_irql_test() {
  auto old_irql = ntl::current_irql();
  {
    auto raised_irql = ntl::raise_irql(ntl::irql::apc);

    if (old_irql != raised_irql.old())
      return false;

    if (ntl::current_irql() != ntl::irql::apc)
      return false;
  }
  if (ntl::current_irql() != old_irql)
    return false;

  old_irql = ntl::current_irql();
  {
    auto raised_irql = ntl::raise_irql_to_dpc_level();
    if (old_irql != raised_irql.old())
      return false;

    if (ntl::current_irql() != ntl::irql::dispatch)
      return false;
  }
  old_irql = ntl::current_irql();
  {
    auto raised_irql = ntl::raise_irql_to_synch_level();
    if (old_irql != raised_irql.old())
      return false;
  }
  return (ntl::current_irql() == old_irql);
}

#include <ntl/spin_lock>

bool ntl_spin_lock_test() {
  ntl::spin_lock lock;
  ntl::spin_lock lock2;

  if (!lock.test())
    return false;

  if (!lock2.test())
    return false;

  std::unique_lock lk(lock);
  if (!lk.owns_lock())
    return false;

  std::unique_lock lk2(lock, std::try_to_lock);
  if (lk2.owns_lock())
    return false;

  {
    ntl::unique_lock<ntl::spin_lock> nlk(lock2, ntl::at_dpc_level_lock);
    if (!nlk.owns_lock())
      return false;
    if (lock2.test())
      return false;

    ntl::unique_lock<ntl::spin_lock> nlk2(lock2, std::try_to_lock);
    if (nlk2.owns_lock())
      return false;
  }
  if (!lock2.test())
    return false;

  return KeTestSpinLock(lock.native_handle()) == FALSE;
}

#include <ntl/resource>

bool ntl_resource_test() {
  ntl::resource res;
  {
    std::shared_lock lk(res);
    if (!lk.owns_lock())
      return false;
    if (!res.locked())
      return false;
    if (!res.locked_shared())
      return false;

    std::unique_lock lk2(res, std::try_to_lock);
    if (lk2.owns_lock())
      return false;
    if (res.locked_exclusive())
      return false;
  }
  if (res.locked())
    return false;
  if (res.locked_exclusive())
    return false;
  if (res.locked_shared())
    return false;

  ntl::resource res2;
  std::unique_lock lk3(res);
  if (!lk3.owns_lock())
    return false;
  if (!res.locked())
    return false;
  if (!res.locked_exclusive())
    return false;

  std::shared_lock lk4(res, std::try_to_lock);
  if (!lk4.owns_lock())
    return false;
  if (res.locked_shared())
    return false;

  ntl::unique_lock<ntl::resource> lk5(res2, ntl::adopt_critical_region);
  if (!lk5.owns_lock())
    return false;
  if (!res2.locked())
    return false;
  if (!res2.locked_exclusive())
    return false;
  if (res2.locked_shared())
    return false;

  ntl::shared_lock<ntl::resource> lk6(lk5);
  if (lk5.owns_lock())
    return false;
  if (!lk6.owns_lock())
    return false;
  if (!res2.locked())
    return false;
  if (!res2.locked_shared())
    return false;
  if (res2.locked_exclusive())
    return false;

  ntl::resource res3;
  ntl::shared_lock<ntl::resource> lk7(res3, ntl::adopt_critical_region);
  if (!lk7.owns_lock())
    return false;
  if (!res3.locked())
    return false;
  return res3.locked_shared();
}

//
// Google Test.
//
#include <gtest/gtest.h>

TEST(ntl_test, ntl_expand_stack_test) {
  long result = 0;
  ntl::expand_stack(
      [&result](int i, long l, double d) { result = (long)(i + l + d); }, 1, 2,
      3.0);
  EXPECT_EQ(result, 6);

  result = ntl::expand_stack(
      [](int i, long l, double d) -> long { return (long)(i + l + d); }, 1, 2,
      3.0);
  EXPECT_EQ(result, 6);

  std::string t1;
  std::string t2;
  std::string t3;

  ntl::expand_stack([&] {
    try {
      throw std::bad_alloc();
    } catch (const std::exception &e) {
      try {
        t1 = e.what();
        throw std::bad_array_new_length();
      } catch (const std::exception &e) {
        try {
          t2 = e.what();
          throw std::runtime_error("test");
        } catch (const std::runtime_error &e) {
          t3 = e.what();
        } catch (const std::exception &) {
        }
      }
    }
  });
  EXPECT_FALSE(t1.empty() && t2.empty() && t3.empty());
}

TEST(ntl_test, ntl_irql_test) {
  auto old_irql = ntl::current_irql();
  {
    auto raised_irql = ntl::raise_irql(ntl::irql::apc);
    EXPECT_EQ(old_irql, raised_irql.old());
    EXPECT_EQ(ntl::current_irql(), ntl::irql::apc);
  }
  EXPECT_EQ(ntl::current_irql(), old_irql);

  old_irql = ntl::current_irql();
  {
    auto raised_irql = ntl::raise_irql_to_dpc_level();
    EXPECT_EQ(old_irql, raised_irql.old());

    EXPECT_EQ(ntl::current_irql(), ntl::irql::dispatch);
  }

  EXPECT_EQ(ntl::current_irql(), old_irql);
}

TEST(ntl_test, ntl_spin_lock_test) {
  ntl::spin_lock lock;
  ntl::spin_lock lock2;
  {
    EXPECT_TRUE(lock.test());

    std::unique_lock lk(lock);
    EXPECT_FALSE(lock.test());
    EXPECT_TRUE(lk.owns_lock());

    std::unique_lock lk2(lock, std::try_to_lock);
    EXPECT_FALSE(lock.test());
    EXPECT_FALSE(lk2.owns_lock());

    ntl::unique_lock<ntl::spin_lock> lk3(lock2, ntl::at_dpc_level_lock);
    EXPECT_FALSE(lock2.test());
    EXPECT_TRUE(lk3.owns_lock());
  }
  EXPECT_TRUE(lock.test());
  EXPECT_TRUE(KeTestSpinLock(lock.native_handle()));
}

TEST(ntl_test, ntl_resource_test) {
  ntl::resource res;
  {
    std::shared_lock lk(res);
    EXPECT_TRUE(lk.owns_lock());
    EXPECT_TRUE(res.locked());
    EXPECT_TRUE(res.locked_shared());

    std::unique_lock lk2(res, std::try_to_lock);
    EXPECT_FALSE(lk2.owns_lock());
    EXPECT_FALSE(res.locked_exclusive());
  }
  EXPECT_FALSE(res.locked());
  EXPECT_FALSE(res.locked_exclusive());
  EXPECT_FALSE(res.locked_shared());

  ntl::resource res2;
  std::unique_lock lk3(res);
  EXPECT_TRUE(lk3.owns_lock());
  EXPECT_TRUE(res.locked());
  EXPECT_TRUE(res.locked_exclusive());

  std::shared_lock lk4(res, std::try_to_lock);
  EXPECT_TRUE(lk4.owns_lock());
  EXPECT_FALSE(res.locked_shared());

  ntl::unique_lock<ntl::resource> lk5(res2, ntl::adopt_critical_region);
  EXPECT_TRUE(lk5.owns_lock());
  EXPECT_TRUE(res2.locked());
  EXPECT_TRUE(res2.locked_exclusive());
  EXPECT_FALSE(res2.locked_shared());

  ntl::shared_lock<ntl::resource> lk6(lk5);
  EXPECT_FALSE(lk5.owns_lock());
  EXPECT_TRUE(lk6.owns_lock());
  EXPECT_TRUE(res2.locked());
  EXPECT_TRUE(res2.locked_shared());
  EXPECT_FALSE(res2.locked_exclusive());

  ntl::resource res3;
  ntl::shared_lock<ntl::resource> lk7(res3, ntl::adopt_critical_region);
  EXPECT_TRUE(lk7.owns_lock());
  EXPECT_TRUE(res3.locked());
  EXPECT_TRUE(res3.locked_shared());
}