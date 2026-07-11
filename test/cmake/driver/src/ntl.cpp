#include <ntl/event>
#include <ntl/expand_stack>
#include <ntl/except>
#include <ntl/irql>
#include <ntl/lookaside_list>
#include <ntl/pool_allocator>
#include <ntl/result>
#include <ntl/symbolic_link>
#include <ntl/unicode_string>
#include <ntl/work_item>

#include <memory_resource>
#include <atomic>
#include <memory>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace {
volatile NTSTATUS g_seh_test_status = STATUS_ACCESS_VIOLATION;
using raise_status_fn = void(NTAPI *)(NTSTATUS);
raise_status_fn volatile g_raise_status = ExRaiseStatus;

__declspec(noinline) void raise_seh_test_status() {
  const auto raise_status = g_raise_status;
  raise_status(g_seh_test_status);
}

long g_pool_test_object_count = 0;
long g_lookaside_test_object_count = 0;

struct pool_test_object {
  explicit pool_test_object(int value) : value(value) {
    ++g_pool_test_object_count;
  }

  ~pool_test_object() { --g_pool_test_object_count; }

  int value;
};

struct lookaside_test_object {
  explicit lookaside_test_object(int value) : value(value) {
    ++g_lookaside_test_object_count;
  }

  ~lookaside_test_object() { --g_lookaside_test_object_count; }

  int value;
};

void delete_symbolic_link_if_present(const std::wstring &link_name) {
  ntl::unicode_string native_link_name(link_name);
  (void)IoDeleteSymbolicLink(&*native_link_name);
}

struct work_item_test_context {
  std::atomic<long> value{0};
  ntl::event release{ntl::event_type::notification, false};
};

void work_item_test_routine(void *context) noexcept {
  auto *const state = static_cast<work_item_test_context *>(context);
  if (ntl::current_irql() == ntl::irql::passive)
    state->value.store(41);
}

void blocking_work_item_test_routine(void *context) noexcept {
  auto *const state = static_cast<work_item_test_context *>(context);
  (void)state->release.wait();
  state->value.store(43);
}
} // namespace

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

bool ntl_seh_try_except_test() {
  const auto ret = ntl::seh::try_except([]() { raise_seh_test_status(); });
  return !std::get<0>(ret) &&
         std::get<1>(ret) == static_cast<unsigned long>(STATUS_ACCESS_VIOLATION);
}

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
  ntl::spin_lock lock3;
  auto old_irql = ntl::current_irql();

  if (!lock.test())
    return false;

  if (!lock2.test())
    return false;

  {
    ntl::unique_lock<ntl::spin_lock> lk(lock);
    if (!lk.owns_lock())
      return false;
    if (lock.test())
      return false;
    if (ntl::current_irql() != ntl::irql::dispatch)
      return false;
  }
  if (!lock.test())
    return false;
  if (ntl::current_irql() != old_irql)
    return false;

  {
    std::unique_lock<ntl::spin_lock> lk(lock);
    if (!lk.owns_lock())
      return false;

    std::unique_lock<ntl::spin_lock> lk2(lock, std::try_to_lock);
    if (lk2.owns_lock())
      return false;
  }
  if (!lock.test())
    return false;
  if (ntl::current_irql() != old_irql)
    return false;

  {
    std::unique_lock<ntl::spin_lock> lk(lock, std::try_to_lock);
    if (!lk.owns_lock())
      return false;
    if (lock.test())
      return false;
    if (ntl::current_irql() != ntl::irql::dispatch)
      return false;
  }
  if (!lock.test())
    return false;
  if (ntl::current_irql() != old_irql)
    return false;

  {
    auto raised_irql = ntl::raise_irql_to_dpc_level();
    if (old_irql != raised_irql.old())
      return false;

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
  if (ntl::current_irql() != old_irql)
    return false;

  {
    auto raised_irql = ntl::raise_irql_to_dpc_level();
    if (old_irql != raised_irql.old())
      return false;

    ntl::unique_lock<ntl::spin_lock> nlk(lock2, ntl::at_dpc_level_lock);
    ntl::unique_lock<ntl::spin_lock> nlk2(lock3, ntl::at_dpc_level_lock);
    nlk = std::move(nlk2);

    if (!lock2.test())
      return false;
    if (lock3.test())
      return false;
    if (!nlk.owns_lock())
      return false;
    if (nlk2.owns_lock())
      return false;
  }
  if (!lock3.test())
    return false;

  return ntl::current_irql() == old_irql;
}

#include <ntl/resource>

bool ntl_resource_test() {
  ntl::resource res;
  {
    std::shared_lock<ntl::resource> lk(res);
    if (!lk.owns_lock())
      return false;
    if (!res.locked())
      return false;
    if (!res.locked_shared())
      return false;

    std::unique_lock<ntl::resource> lk2(res, std::try_to_lock);
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
  std::unique_lock<ntl::resource> lk3(res);
  if (!lk3.owns_lock())
    return false;
  if (!res.locked())
    return false;
  if (!res.locked_exclusive())
    return false;

  std::shared_lock<ntl::resource> lk4(res, std::try_to_lock);
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

bool ntl_pool_allocator_test() {
  g_pool_test_object_count = 0;

  void *const raw = ntl::allocate_pool(128, ntl::pool_kind::nonpaged,
                                       ntl::pool_option::none, "NTLr");
  if (!raw)
    return false;
  ntl::free_pool(raw, "NTLr");

  void *const raw_multichar =
      ntl::allocate_pool(64, ntl::pool_kind::nonpaged, ntl::pool_option::none,
                         'mLTN');
  if (!raw_multichar)
    return false;
  ntl::free_pool(raw_multichar, 'mLTN');

  auto buffer = ntl::make_pool_buffer(96, ntl::pool_kind::nonpaged,
                                      ntl::pool_option::none, "NTLb");
  if (!buffer)
    return false;
  void *const released_buffer = buffer.release();
  if (!released_buffer)
    return false;
  ntl::free_pool(released_buffer, "NTLb");

  auto scoped_buffer = ntl::make_pool_buffer(32, ntl::pool_kind::nonpaged,
                                             ntl::pool_option::none, 'bLTN');
  if (!scoped_buffer)
    return false;
  scoped_buffer.reset();

  {
    auto object = ntl::make_pool<pool_test_object>(
        ntl::pool_kind::nonpaged, ntl::pool_option::none, "NTLo", 21);
    if (!object || object->value != 21 || g_pool_test_object_count != 1)
      return false;

    auto moved_object = std::move(object);
    if (object || !moved_object || moved_object->value != 21)
      return false;

    pool_test_object *const released_object = moved_object.release();
    if (!released_object || g_pool_test_object_count != 1)
      return false;
    ntl::pool_deleter<pool_test_object>{"NTLo"}(released_object);
    if (g_pool_test_object_count != 0)
      return false;

    auto reset_object = ntl::make_pool<pool_test_object>("NTLq", 22);
    if (!reset_object || g_pool_test_object_count != 1)
      return false;
    reset_object.reset();
    if (g_pool_test_object_count != 0)
      return false;
  }

  constexpr auto vector_tag = ntl::pool_tag("NTLv");
  std::vector<int, ntl::nonpaged_pool_allocator<int, vector_tag>> values;
  values.reserve(4);
  values.push_back(1);
  values.push_back(2);
  values.push_back(3);
  values.push_back(4);
  if (std::accumulate(values.begin(), values.end(), 0) != 10)
    return false;

  constexpr auto paged_tag = ntl::pool_tag("NTLg");
  std::vector<int, ntl::paged_pool_allocator<int, paged_tag>> paged_values;
  paged_values.assign({5, 6, 7});
  if (paged_values.size() != 3 || paged_values[2] != 7)
    return false;

  using cache_allocator =
      ntl::pool_allocator<int, ntl::pool_kind::nonpaged,
                          ntl::pool_option::cache_aligned, 'cLTN'>;
  std::vector<int, cache_allocator> cache_values;
  cache_values.emplace_back(42);
  if (cache_values.front() != 42)
    return false;

  using string_tag_allocator =
      ntl::pool_allocator<int, ntl::pool_kind::nonpaged,
                          ntl::pool_option::none, ntl::pool_tag("NTLs")>;
  std::vector<int, string_tag_allocator> string_tag_values;
  string_tag_values.assign({13, 14});
  if (string_tag_values[0] != 13 || string_tag_values[1] != 14)
    return false;

  const auto options =
      ntl::pool_option::cache_aligned | ntl::pool_option::raise_on_failure;
  if (!ntl::has_pool_option(options, ntl::pool_option::cache_aligned))
    return false;
  if (ntl::has_pool_option(options, ntl::pool_option::special_pool))
    return false;

  std::pmr::vector<int> pmr_values{ntl::pmr::nonpaged_pool_resource()};
  pmr_values.resize(3);
  pmr_values[0] = 8;
  pmr_values[1] = 9;
  pmr_values[2] = 10;
  if (std::accumulate(pmr_values.begin(), pmr_values.end(), 0) != 27)
    return false;

  ntl::pmr::pool_resource paged_resource{
      ntl::pool_kind::paged, ntl::pool_option::none, "NTLp"};
  std::pmr::vector<int> paged_pmr_values{&paged_resource};
  paged_pmr_values.push_back(11);
  paged_pmr_values.push_back(12);
  return paged_pmr_values[0] == 11 && paged_pmr_values[1] == 12;
}

bool ntl_lookaside_list_test() {
  g_lookaside_test_object_count = 0;

  using nonpaged_list =
      ntl::lookaside_list<lookaside_test_object, ntl::pool_kind::nonpaged,
                          ntl::pool_option::none, ntl::pool_tag("NTLl")>;
  nonpaged_list list;

  lookaside_test_object *const raw = list.allocate();
  if (!raw)
    return false;
  new (raw) lookaside_test_object{31};
  if (raw->value != 31 || g_lookaside_test_object_count != 1)
    return false;
  list.destroy(raw);
  if (g_lookaside_test_object_count != 0)
    return false;

  auto object = list.make(32);
  if (!object || object->value != 32 || g_lookaside_test_object_count != 1)
    return false;

  auto moved_object = std::move(object);
  if (object || !moved_object || moved_object->value != 32)
    return false;
  moved_object.reset();
  if (g_lookaside_test_object_count != 0)
    return false;

  using paged_list =
      ntl::lookaside_list<lookaside_test_object, ntl::pool_kind::paged,
                          ntl::pool_option::none, ntl::pool_tag("NTLg")>;
  paged_list paged;
  auto paged_object = paged.make(33);
  if (!paged_object || paged_object->value != 33 ||
      g_lookaside_test_object_count != 1)
    return false;
  paged_object.reset();
  if (g_lookaside_test_object_count != 0)
    return false;

  using cache_aligned_list =
      ntl::lookaside_list<lookaside_test_object, ntl::pool_kind::nonpaged,
                          ntl::pool_option::cache_aligned, 'lLTN'>;
  cache_aligned_list cache_aligned;
  auto cached_object = cache_aligned.make(34);
  if (!cached_object || cached_object->value != 34 ||
      g_lookaside_test_object_count != 1)
    return false;
  cached_object.reset();

  list.flush();
  paged.flush();
  cache_aligned.flush();
  return g_lookaside_test_object_count == 0;
}

bool ntl_result_test() {
  auto value = ntl::result<int>::success(21);
  if (!value || !value.has_value() || value.value() != 21 || *value != 21)
    return false;
  if (static_cast<NTSTATUS>(value.status()) != STATUS_SUCCESS)
    return false;

  *value = 22;
  if (value.value_or(1) != 22)
    return false;

  const auto direct = ntl::result<std::wstring>::success(L"result");
  if (!direct || direct->size() != 6 || direct.value() != L"result")
    return false;

  auto moved = ntl::ok(std::make_unique<int>(42));
  if (!moved || !moved.value() || *moved.value() != 42)
    return false;

  auto buffer_result = ntl::try_make_pool_buffer(
      48, ntl::pool_kind::nonpaged, ntl::pool_option::none, "NTRb");
  if (!buffer_result || !buffer_result->get())
    return false;
  buffer_result->reset();

  const auto object_count_before = g_pool_test_object_count;
  auto object_result = ntl::try_make_pool<pool_test_object>(
      ntl::pool_kind::nonpaged, ntl::pool_option::none, "NTRo", 23);
  if (!object_result || !object_result->get() ||
      (*object_result)->value != 23)
    return false;
  object_result->reset();
  if (g_pool_test_object_count != object_count_before)
    return false;

  auto failure =
      ntl::result<int>::failure(STATUS_OBJECT_NAME_NOT_FOUND);
  if (failure || failure.has_value() || failure.value_or(7) != 7)
    return false;
  if (static_cast<NTSTATUS>(failure.status()) !=
      STATUS_OBJECT_NAME_NOT_FOUND)
    return false;

  bool caught_value_exception = false;
  try {
    (void)failure.value();
  } catch (const ntl::exception &e) {
    caught_value_exception =
        static_cast<NTSTATUS>(e.get_status()) ==
        STATUS_OBJECT_NAME_NOT_FOUND;
  }
  if (!caught_value_exception)
    return false;

  ntl::result<int> unexpected_failure =
      ntl::unexpected(STATUS_INVALID_PARAMETER);
  if (unexpected_failure ||
      static_cast<NTSTATUS>(unexpected_failure.status()) !=
          STATUS_INVALID_PARAMETER)
    return false;

  auto void_success = ntl::ok();
  if (!void_success || !void_success.has_value())
    return false;
  void_success.value();

  ntl::result<void> void_failure =
      ntl::unexpected(STATUS_ACCESS_DENIED);
  if (void_failure || void_failure.has_value())
    return false;
  if (static_cast<NTSTATUS>(void_failure.status()) != STATUS_ACCESS_DENIED)
    return false;

  bool caught_void_exception = false;
  try {
    void_failure.value();
  } catch (const ntl::exception &e) {
    caught_void_exception =
        static_cast<NTSTATUS>(e.get_status()) == STATUS_ACCESS_DENIED;
  }

  return caught_void_exception;
}

bool ntl_symbolic_link_test() {
  const std::wstring link_name = L"\\DosDevices\\CrtSysNtlSymbolicLinkTest";
  const std::wstring target_name =
      L"\\Device\\CrtSysNtlSymbolicLinkTarget";

  delete_symbolic_link_if_present(link_name);

  {
    ntl::symbolic_link link(link_name, target_name);
    if (!link || link.name() != link_name ||
        link.target_name() != target_name)
      return false;

    ntl::symbolic_link moved(std::move(link));
    if (link || !moved || moved.name() != link_name)
      return false;

    if (!moved.close().is_ok() || moved)
      return false;
  }

  {
    ntl::symbolic_link scoped(link_name, target_name);
    if (!scoped)
      return false;
  }

  {
    auto result_link = ntl::try_create_symbolic_link(link_name, target_name);
    if (!result_link || !result_link->valid() ||
        result_link->name() != link_name)
      return false;
    if (!result_link->close().is_ok())
      return false;
  }

  {
    ntl::symbolic_link released(link_name, target_name);
    const auto released_name = released.release();
    if (released || released_name != link_name)
      return false;
    delete_symbolic_link_if_present(released_name);
  }

  return true;
}

bool ntl_event_test() {
  ntl::event notification;
  if (notification.signaled())
    return false;

  notification.set();
  if (!notification.signaled())
    return false;

  notification.clear();
  if (notification.signaled())
    return false;

  notification.set();
  if (!notification.wait().is_ok())
    return false;
  if (!notification.signaled())
    return false;

  notification.reset();
  if (notification.signaled())
    return false;

  ntl::event synchronization(ntl::event_type::synchronization, true);
  if (!synchronization.wait().is_ok())
    return false;
  if (synchronization.signaled())
    return false;

  LARGE_INTEGER zero_timeout{};
  if (static_cast<NTSTATUS>(synchronization.wait(&zero_timeout)) !=
      STATUS_TIMEOUT)
    return false;

  return true;
}

bool ntl_work_item_test() {
  work_item_test_context raw_context;

  ntl::work_item raw_item(work_item_test_routine, &raw_context);
  if (!raw_item.queue().is_ok())
    return false;
  if (!raw_item.wait().is_ok())
    return false;
  if (raw_context.value.load() != 41)
    return false;

  std::atomic<long> typed_value{0};
  ntl::passive_work_item typed_item([&] {
    if (ntl::current_irql() == ntl::irql::passive)
      typed_value.store(42);
  });

  {
    auto raised_irql = ntl::raise_irql_to_dpc_level();
    if (!typed_item.queue().is_ok())
      return false;
  }
  if (!typed_item.wait().is_ok() || typed_value.load() != 42)
    return false;

  work_item_test_context blocking_context;

  ntl::work_item blocking_item(blocking_work_item_test_routine,
                               &blocking_context);
  if (!blocking_item.queue().is_ok())
    return false;
  if (blocking_item.queue().is_ok())
    return false;

  blocking_context.release.set();
  if (!blocking_item.wait().is_ok())
    return false;

  return blocking_context.value.load() == 43 && !blocking_item.queued();
}

//
// Google Test.
//
#include <gtest/gtest.h>

TEST(ntl_test, ntl_exception_what_test) {
  ntl::exception e(STATUS_INVALID_PARAMETER, "test message");

  EXPECT_EQ(static_cast<NTSTATUS>(e.get_status()), STATUS_INVALID_PARAMETER);
  EXPECT_STREQ(e.what(), "test message");
}

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

TEST(ntl_test, ntl_seh_try_except_test) {
  const auto ret = ntl::seh::try_except([]() { raise_seh_test_status(); });
  EXPECT_FALSE(std::get<0>(ret));
  EXPECT_EQ(std::get<1>(ret),
            static_cast<unsigned long>(STATUS_ACCESS_VIOLATION));
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
  ntl::spin_lock lock3;
  auto old_irql = ntl::current_irql();

  {
    EXPECT_TRUE(lock.test());

    ntl::unique_lock<ntl::spin_lock> lk(lock);
    EXPECT_FALSE(lock.test());
    EXPECT_TRUE(lk.owns_lock());
    EXPECT_EQ(ntl::current_irql(), ntl::irql::dispatch);
  }
  EXPECT_TRUE(lock.test());
  EXPECT_EQ(ntl::current_irql(), old_irql);

  {
    std::unique_lock lk(lock);
    std::unique_lock lk2(lock, std::try_to_lock);
    EXPECT_FALSE(lock.test());
    EXPECT_FALSE(lk2.owns_lock());
  }
  EXPECT_TRUE(lock.test());
  EXPECT_EQ(ntl::current_irql(), old_irql);

  {
    std::unique_lock lk(lock, std::try_to_lock);
    EXPECT_FALSE(lock.test());
    EXPECT_TRUE(lk.owns_lock());
    EXPECT_EQ(ntl::current_irql(), ntl::irql::dispatch);
  }
  EXPECT_TRUE(lock.test());
  EXPECT_EQ(ntl::current_irql(), old_irql);

  {
    auto raised_irql = ntl::raise_irql_to_dpc_level();
    EXPECT_EQ(old_irql, raised_irql.old());

    ntl::unique_lock<ntl::spin_lock> lk3(lock2, ntl::at_dpc_level_lock);
    EXPECT_FALSE(lock2.test());
    EXPECT_TRUE(lk3.owns_lock());
  }
  EXPECT_TRUE(lock2.test());
  EXPECT_EQ(ntl::current_irql(), old_irql);

  {
    auto raised_irql = ntl::raise_irql_to_dpc_level();
    EXPECT_EQ(old_irql, raised_irql.old());

    ntl::unique_lock<ntl::spin_lock> lk3(lock2, ntl::at_dpc_level_lock);
    ntl::unique_lock<ntl::spin_lock> lk4(lock3, ntl::at_dpc_level_lock);
    lk3 = std::move(lk4);
    EXPECT_TRUE(lock2.test());
    EXPECT_FALSE(lock3.test());
    EXPECT_TRUE(lk3.owns_lock());
    EXPECT_FALSE(lk4.owns_lock());
  }
  EXPECT_TRUE(lock3.test());
  EXPECT_TRUE(KeTestSpinLock(lock.native_handle()));
  EXPECT_EQ(ntl::current_irql(), old_irql);
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

TEST(ntl_test, ntl_pool_allocator_test) {
  EXPECT_TRUE(ntl_pool_allocator_test());
}

TEST(ntl_test, ntl_lookaside_list_test) {
  EXPECT_TRUE(ntl_lookaside_list_test());
}

TEST(ntl_test, ntl_result_test) {
  EXPECT_TRUE(ntl_result_test());
}

TEST(ntl_test, ntl_symbolic_link_test) {
  EXPECT_TRUE(ntl_symbolic_link_test());
}

TEST(ntl_test, ntl_event_test) {
  EXPECT_TRUE(ntl_event_test());
}

TEST(ntl_test, ntl_work_item_test) {
  EXPECT_TRUE(ntl_work_item_test());
}
