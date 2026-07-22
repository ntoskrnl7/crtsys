/**
 * @file operation_callback.cpp
 * @author jungkwang.lee (ntoskrnl7@gmail.com)
 * @brief Compile-time coverage for operation-typed NTL minifilter callbacks.
 *
 * @copyright Copyright (c) 2022 NT Template Library Authors.
 */

#include <ntl/flt/all>

#include <type_traits>
#include <utility>

namespace ntl_flt_compile_test {

using close_data = ntl::flt::callback_data<ntl::flt::operation::close>;
using create_data = ntl::flt::callback_data<ntl::flt::operation::create>;

static_assert(std::is_same_v<close_data, ntl::flt::close_callback_data>);
static_assert(std::is_same_v<create_data, ntl::flt::create_callback_data>);
static_assert(
    std::is_same_v<ntl::flt::callback_data<ntl::flt::operation::cleanup>,
                   ntl::flt::cleanup_callback_data>);

static_assert(
    std::is_same_v<
        decltype(std::declval<create_data>().parameters().create_options()),
        ULONG>);
static_assert(std::is_same_v<
              decltype(std::declval<create_data>().parameters().disposition()),
              UCHAR>);

ntl::flt::pre_result close_callback(close_data, ntl::flt::related_objects,
                                    void *&) noexcept;

ntl::flt::pre_result read_callback(ntl::flt::read_callback_data,
                                   ntl::flt::related_objects, void *&) noexcept;

ntl::flt::pre_result normal_create_callback(ntl::flt::create_callback_data,
                                            ntl::flt::related_objects) noexcept;

ntl::flt::pre_result normal_read_callback(ntl::flt::read_callback_data,
                                          ntl::flt::related_objects) noexcept;

void normal_create_post_callback(ntl::flt::create_callback_data,
                                 ntl::flt::related_objects) noexcept;

ntl::flt::post_continuation
    normal_read_immediate_callback(ntl::flt::read_callback_data,
                                   ntl::flt::related_objects) noexcept;

void normal_read_safe_callback(ntl::flt::read_callback_data,
                               ntl::flt::related_objects) noexcept;

struct completion_state {
  explicit completion_state(unsigned long value) noexcept : value(value) {}
  ~completion_state() noexcept = default;

  unsigned long value;
};

ntl::flt::pre_result completion_read_callback(
    ntl::flt::read_callback_data, ntl::flt::related_objects,
    ntl::flt::completion_slot<completion_state> &) noexcept;

void completion_read_post_callback(
    ntl::flt::read_callback_data, ntl::flt::related_objects,
    ntl::flt::completion_ref<completion_state>) noexcept;

void draining_completion_read_post_callback(
    ntl::flt::read_callback_data, ntl::flt::related_objects,
    ntl::flt::completion_ref<completion_state>,
    ntl::flt::post_operation_flags) noexcept;

ntl::flt::post_continuation typed_read_immediate_callback(
    ntl::flt::read_callback_data, ntl::flt::related_objects,
    ntl::flt::completion_ref<completion_state>) noexcept;

void typed_read_safe_callback(
    ntl::flt::read_callback_data, ntl::flt::related_objects,
    ntl::flt::completion_ref<completion_state>) noexcept;

ntl::flt::post_continuation
read_post_callback(ntl::flt::read_callback_data, ntl::flt::related_objects,
                   void *, ntl::flt::post_operation_flags) noexcept;

void invalid_read_post_callback(ntl::flt::read_callback_data,
                                ntl::flt::related_objects, void *,
                                ntl::flt::post_operation_flags) noexcept;

void read_when_safe_callback(ntl::flt::read_callback_data,
                             ntl::flt::related_objects, void *) noexcept;

// Generic callbacks are valid C++. Keep compile coverage here even though
// current C++ editor completion engines do not list members for `auto data`.
const auto generic_create_pre_callback =
    [](auto data, auto objects, auto &completion_context) noexcept {
      static_assert(std::is_same_v<decltype(data),
                                   ntl::flt::create_callback_data>);
      static_assert(std::is_same_v<decltype(objects),
                                   ntl::flt::related_objects>);
      static_assert(std::is_same_v<decltype(completion_context), void *&>);

      (void)data.parameters().create_options();
      completion_context = nullptr;
      return ntl::flt::pre_result::success_with_callback;
    };

const auto generic_create_post_callback =
    [](auto data, auto objects, auto completion_context, auto flags) noexcept {
      static_assert(std::is_same_v<decltype(data),
                                   ntl::flt::create_callback_data>);
      static_assert(std::is_same_v<decltype(objects),
                                   ntl::flt::related_objects>);
      static_assert(std::is_same_v<decltype(completion_context), void *>);
      static_assert(std::is_same_v<decltype(flags),
                                   ntl::flt::post_operation_flags>);

      (void)data.io_status();
      return ntl::flt::post_result::finished;
    };

const auto generic_cleanup_pre_callback = [](auto data,
                                             auto objects) noexcept {
  static_assert(std::is_same_v<decltype(data),
                               ntl::flt::cleanup_callback_data>);
  static_assert(
      std::is_same_v<decltype(objects), ntl::flt::related_objects>);
  return ntl::flt::pre_result::success_with_callback;
};

const auto generic_cleanup_post_callback = [](auto data,
                                              auto objects) noexcept {
  static_assert(std::is_same_v<decltype(data),
                               ntl::flt::cleanup_callback_data>);
  static_assert(
      std::is_same_v<decltype(objects), ntl::flt::related_objects>);
  (void)data.io_status();
};

static_assert(std::is_convertible_v<
              decltype(generic_create_pre_callback),
              ntl::flt::pre_operation_callback<
                  ntl::flt::operation_id::create>>);
static_assert(std::is_convertible_v<
              decltype(generic_create_post_callback),
              ntl::flt::post_operation_callback<
                  ntl::flt::operation_id::create>>);
static_assert(std::is_convertible_v<
              decltype(generic_cleanup_pre_callback),
              ntl::flt::normal_pre_operation_callback<
                  ntl::flt::operation_id::cleanup>>);
static_assert(std::is_convertible_v<
              decltype(generic_cleanup_post_callback),
              ntl::flt::normal_post_operation_callback<
                  ntl::flt::operation_id::cleanup>>);

using generic_create_registration =
    decltype(std::declval<ntl::flt::registration &>().on(
        ntl::flt::operation::create, generic_create_pre_callback,
        generic_create_post_callback));
static_assert(std::is_same_v<generic_create_registration,
                             ntl::flt::registration &>);

using generic_cleanup_registration =
    decltype(std::declval<ntl::flt::registration &>().on(
        ntl::flt::operation::cleanup, generic_cleanup_pre_callback,
        generic_cleanup_post_callback));
static_assert(std::is_same_v<generic_cleanup_registration,
                             ntl::flt::registration &>);

template <typename Callback, typename = void>
struct can_register_close : std::false_type {};

template <typename Callback>
struct can_register_close<
    Callback, std::void_t<decltype(std::declval<ntl::flt::registration &>().on(
                  ntl::flt::operation::close, std::declval<Callback>()))>>
    : std::true_type {};

static_assert(can_register_close<decltype(&close_callback)>::value,
              "operation::close must accept callback_data<operation::close>");
static_assert(!can_register_close<decltype(&read_callback)>::value,
              "operation::close must reject read_callback_data");

using normal_create_registration =
    decltype(std::declval<ntl::flt::registration &>().on(
        ntl::flt::operation::create, &normal_create_callback,
        &normal_create_post_callback));
static_assert(
    std::is_same_v<normal_create_registration, ntl::flt::registration &>,
    "flags-less post callbacks must register as normal-only work");

using normal_safe_registration =
    decltype(std::declval<ntl::flt::registration &>().on(
        ntl::flt::operation::read, &normal_read_callback,
        &normal_read_immediate_callback, &normal_read_safe_callback));
static_assert(
    std::is_same_v<normal_safe_registration, ntl::flt::registration &>,
    "flags-less post callbacks must be able to request WhenSafe");

using typed_completion_registration =
    decltype(std::declval<ntl::flt::registration &>()
                 .on_with_completion<completion_state>(
                     ntl::flt::operation::read, &completion_read_callback,
                     &completion_read_post_callback));
static_assert(
    std::is_same_v<typed_completion_registration, ntl::flt::registration &>,
    "typed normal completion registration must be supported");

using draining_completion_registration =
    decltype(std::declval<ntl::flt::registration &>()
                 .on_with_completion<completion_state>(
                     ntl::flt::operation::read, &completion_read_callback,
                     &draining_completion_read_post_callback));
static_assert(
    std::is_same_v<draining_completion_registration, ntl::flt::registration &>,
    "typed drain-aware completion registration must be supported");

using typed_safe_registration =
    decltype(std::declval<ntl::flt::registration &>()
                 .on_with_completion<completion_state>(
                     ntl::flt::operation::read, &completion_read_callback,
                     &typed_read_immediate_callback,
                     &typed_read_safe_callback));
static_assert(std::is_same_v<typed_safe_registration, ntl::flt::registration &>,
              "typed completion state must remain valid through WhenSafe");

using safe_read_registration =
    decltype(std::declval<ntl::flt::registration &>().on(
        ntl::flt::operation::read, &read_callback, &read_post_callback,
        &read_when_safe_callback));
static_assert(std::is_same_v<safe_read_registration, ntl::flt::registration &>,
              "on(read, pre, post, when_safe) must be supported");

template <typename Post, typename = void>
struct can_register_safe_read : std::false_type {};

template <typename Post>
struct can_register_safe_read<
    Post, std::void_t<decltype(std::declval<ntl::flt::registration &>().on(
              ntl::flt::operation::read, &read_callback, std::declval<Post>(),
              &read_when_safe_callback))>> : std::true_type {};

static_assert(can_register_safe_read<decltype(&read_post_callback)>::value,
              "mixed post callbacks must return post_continuation");
static_assert(
    !can_register_safe_read<decltype(&invalid_read_post_callback)>::value,
    "void direct post callbacks must not silently force a safe continuation");

static_assert(std::is_same_v<decltype(std::declval<ntl::flt::registration &>()
                                          .allow_manual_detach()
                                          .deny_manual_detach()),
                             ntl::flt::registration &>,
              "manual detach policy must be explicit and chainable");

} // namespace ntl_flt_compile_test
