/**
 * @file operation_callback.cpp
 * @author jungkwang.lee (ntoskrnl7@gmail.com)
 * @brief Compile-time coverage for operation-typed NTL minifilter callbacks.
 *
 * @copyright Copyright (c) 2022 NT Template Library Authors.
 */

#include <ntl/flt/all>
#include <ntl/ipc/mapped_buffer>

#include <type_traits>
#include <utility>

namespace ntl_flt_compile_test {

template <typename T, typename = void>
struct has_input_member : std::false_type {};
template <typename T>
struct has_input_member<T,
                        std::void_t<decltype(std::declval<T &>().input)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_output_member : std::false_type {};
template <typename T>
struct has_output_member<T,
                         std::void_t<decltype(std::declval<T &>().output)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_completed_member : std::false_type {};
template <typename T>
struct has_completed_member<
    T, std::void_t<decltype(std::declval<T &>().completed)>>
    : std::true_type {};

static_assert(sizeof(ntl::ipc::mapped_buffer_descriptor) == 40);
static_assert(std::is_copy_constructible_v<ntl::ipc::process_connection>);
static_assert(std::is_same_v<
              decltype(std::declval<
                           const ntl::flt::communication_connection &>()
                           .mapping_process()),
              ntl::ipc::process_connection>);
static_assert(std::is_same_v<
              decltype(std::declval<ntl::flt::communication_port_options &>()
                           .process_mapping_limits(
                               std::declval<
                                   ntl::ipc::process_connection_limits>())),
              ntl::flt::communication_port_options &>);
static_assert(!std::is_copy_constructible_v<ntl::ipc::mapped_io_buffers>);
static_assert(std::is_move_constructible_v<ntl::ipc::mapped_io_buffers>);
static_assert(!std::is_copy_constructible_v<ntl::flt::swapped_io_buffers>);
static_assert(std::is_move_constructible_v<ntl::flt::swapped_io_buffers>);
static_assert(!std::is_copy_constructible_v<ntl::flt::swapped_buffer_view>);
static_assert(std::is_move_constructible_v<ntl::flt::swapped_buffer_view>);
static_assert(std::is_copy_constructible_v<ntl::ipc::borrowed_mdl_view>);
static_assert(ntl::ipc::map_io_options{}.user_mdls ==
              ntl::ipc::user_mdl_policy::isolate_partial_pages);
static_assert(!has_completed_member<ntl::ipc::map_io_options>::value);
static_assert(!has_input_member<ntl::flt::swap_io_options>::value);
static_assert(!has_output_member<ntl::flt::swap_io_options>::value);
static_assert(!std::is_copy_constructible_v<
              ntl::flt::pending_pre_operation_queue>);
static_assert(!std::is_copy_constructible_v<
              ntl::flt::pending_post_operation_registry>);
static_assert(sizeof(ntl::ipc::pending_request_id) == 16);

using mapped_read_result = decltype(ntl::flt::try_map_io_buffers(
    ntl::flt::as_post(std::declval<ntl::flt::read_callback_data>()),
    std::declval<ntl::ipc::process_connection &>()));
using mapped_completed_irp_result =
    decltype(ntl::ipc::try_map_completed_io_buffers(
        std::declval<PDEVICE_OBJECT>(), std::declval<PIRP>(),
        std::declval<ntl::ipc::process_connection &>()));
using swapped_write_result = decltype(ntl::flt::try_swap_io_buffers(
    ntl::flt::as_pre(std::declval<ntl::flt::write_callback_data>())));
static_assert(std::is_same_v<
              mapped_read_result,
              ntl::result<ntl::flt::mapped_operation_buffers>>);
static_assert(std::is_same_v<
              swapped_write_result,
              ntl::result<ntl::flt::swapped_io_buffers>>);
static_assert(std::is_same_v<
              mapped_completed_irp_result,
              ntl::result<ntl::ipc::mapped_io_buffers>>);

template <typename... Data>
inline constexpr bool mapped_pre_surface_v =
    (std::is_same_v<
         decltype(ntl::flt::try_map_io_buffers(
             ntl::flt::as_pre(std::declval<Data>()),
             std::declval<ntl::ipc::process_connection &>())),
         ntl::result<ntl::flt::mapped_operation_buffers>> &&
     ...);

template <typename... Data>
inline constexpr bool mapped_post_surface_v =
    (std::is_same_v<
         decltype(ntl::flt::try_map_io_buffers(
             ntl::flt::as_post(std::declval<Data>()),
             std::declval<ntl::ipc::process_connection &>())),
         ntl::result<ntl::flt::mapped_operation_buffers>> &&
     ...);

template <typename... Data>
inline constexpr bool unambiguous_swapped_pre_surface_v =
    (std::is_same_v<
         decltype(ntl::flt::try_swap_io_buffers(
             ntl::flt::as_pre(std::declval<Data>()))),
         ntl::result<ntl::flt::swapped_io_buffers>> &&
     ...);

template <typename... Data>
inline constexpr bool selectable_swapped_pre_surface_v =
    (std::is_same_v<
         decltype(ntl::flt::try_swap_io_buffers(
             ntl::flt::as_pre(std::declval<Data>()),
             ntl::flt::swap_buffer::all)),
         ntl::result<ntl::flt::swapped_io_buffers>> &&
     ...);

using create_data_for_buffers = ntl::flt::create_callback_data;
using read_data_for_buffers = ntl::flt::read_callback_data;
using write_data_for_buffers = ntl::flt::write_callback_data;
using query_information_data_for_buffers =
    ntl::flt::query_information_callback_data;
using set_information_data_for_buffers =
    ntl::flt::set_information_callback_data;
using query_ea_data_for_buffers = ntl::flt::query_ea_callback_data;
using set_ea_data_for_buffers = ntl::flt::set_ea_callback_data;
using query_volume_data_for_buffers =
    ntl::flt::query_volume_information_callback_data;
using set_volume_data_for_buffers =
    ntl::flt::set_volume_information_callback_data;
using directory_data_for_buffers = ntl::flt::directory_control_callback_data;
using fsctl_data_for_buffers =
    ntl::flt::file_system_control_callback_data;
using ioctl_data_for_buffers = ntl::flt::device_control_callback_data;
using internal_ioctl_data_for_buffers =
    ntl::flt::internal_device_control_callback_data;
using query_security_data_for_buffers =
    ntl::flt::query_security_callback_data;
using set_security_data_for_buffers = ntl::flt::set_security_callback_data;
using query_quota_data_for_buffers = ntl::flt::query_quota_callback_data;
using set_quota_data_for_buffers = ntl::flt::set_quota_callback_data;

static_assert(mapped_pre_surface_v<
              create_data_for_buffers, write_data_for_buffers,
              set_information_data_for_buffers, query_ea_data_for_buffers,
              set_ea_data_for_buffers, set_volume_data_for_buffers,
              set_security_data_for_buffers, query_quota_data_for_buffers,
              set_quota_data_for_buffers, fsctl_data_for_buffers,
              ioctl_data_for_buffers, internal_ioctl_data_for_buffers>);
static_assert(mapped_post_surface_v<
              read_data_for_buffers, query_information_data_for_buffers,
              query_ea_data_for_buffers, query_volume_data_for_buffers,
              directory_data_for_buffers, query_security_data_for_buffers,
              query_quota_data_for_buffers, fsctl_data_for_buffers,
              ioctl_data_for_buffers, internal_ioctl_data_for_buffers>);
static_assert(unambiguous_swapped_pre_surface_v<
              create_data_for_buffers, read_data_for_buffers,
              write_data_for_buffers, query_information_data_for_buffers,
              set_information_data_for_buffers,
              set_ea_data_for_buffers, query_volume_data_for_buffers,
              set_volume_data_for_buffers, directory_data_for_buffers,
              query_security_data_for_buffers, set_security_data_for_buffers,
              set_quota_data_for_buffers>);
static_assert(selectable_swapped_pre_surface_v<
              query_ea_data_for_buffers, query_quota_data_for_buffers,
              fsctl_data_for_buffers, ioctl_data_for_buffers,
              internal_ioctl_data_for_buffers>);

template <typename Data, typename = void>
struct can_map_without_typed_phase : std::false_type {};
template <typename Data>
struct can_map_without_typed_phase<
    Data, std::void_t<decltype(ntl::flt::try_map_io_buffers(
              std::declval<Data>(),
              std::declval<ntl::ipc::process_connection &>()))>>
    : std::true_type {};

template <typename Data, typename = void>
struct can_swap_without_typed_pre : std::false_type {};
template <typename Data>
struct can_swap_without_typed_pre<
    Data, std::void_t<decltype(
              ntl::flt::try_swap_io_buffers(std::declval<Data>()))>>
    : std::true_type {};

template <typename Data, typename = void>
struct can_swap_pre_without_selection : std::false_type {};
template <typename Data>
struct can_swap_pre_without_selection<
    Data, std::void_t<decltype(ntl::flt::try_swap_io_buffers(
              ntl::flt::as_pre(std::declval<Data>())))>>
    : std::true_type {};

template <typename Data, typename = void>
struct can_swap_pre_with_selection : std::false_type {};
template <typename Data>
struct can_swap_pre_with_selection<
    Data, std::void_t<decltype(ntl::flt::try_swap_io_buffers(
              ntl::flt::as_pre(std::declval<Data>()),
              ntl::flt::swap_buffer::all))>>
    : std::true_type {};

template <typename Data, typename = void>
struct can_swap_pre_with_policy_as_selection : std::false_type {};
template <typename Data>
struct can_swap_pre_with_policy_as_selection<
    Data, std::void_t<decltype(ntl::flt::try_swap_io_buffers(
              ntl::flt::as_pre(std::declval<Data>()),
              ntl::flt::swap_io_options{}))>> : std::true_type {};

template <typename Data, typename = void>
struct can_swap_pre_with_selection_and_policy : std::false_type {};
template <typename Data>
struct can_swap_pre_with_selection_and_policy<
    Data, std::void_t<decltype(ntl::flt::try_swap_io_buffers(
              ntl::flt::as_pre(std::declval<Data>()),
              ntl::flt::swap_buffer::all,
              ntl::flt::swap_io_options{}))>> : std::true_type {};

static_assert(
    !can_map_without_typed_phase<ntl::flt::read_callback_data>::value);
static_assert(
    !can_swap_without_typed_pre<ntl::flt::write_callback_data>::value);
static_assert(!can_swap_without_typed_pre<
              ntl::flt::post_operation<ntl::flt::operation_id::read>>::value);
static_assert(can_swap_pre_without_selection<
              ntl::flt::read_callback_data>::value);
static_assert(!can_swap_pre_with_selection<
              ntl::flt::read_callback_data>::value);
static_assert(!can_swap_pre_without_selection<
              ntl::flt::query_ea_callback_data>::value);
static_assert(can_swap_pre_with_selection<
              ntl::flt::query_ea_callback_data>::value);
static_assert(!can_swap_pre_with_policy_as_selection<
              ntl::flt::query_ea_callback_data>::value);
static_assert(can_swap_pre_with_selection_and_policy<
              ntl::flt::query_ea_callback_data>::value);
static_assert(!can_swap_pre_without_selection<
              ntl::flt::cleanup_callback_data>::value);
static_assert(!can_swap_pre_with_selection<
              ntl::flt::cleanup_callback_data>::value);
static_assert(std::is_same_v<
              decltype(std::declval<ntl::ipc::mapped_io_buffers &>()
                           .control_input()),
              const ntl::ipc::mapped_buffer_view *>);
static_assert(std::is_same_v<
              decltype(std::declval<ntl::flt::swapped_io_buffers &>()
                           .control_input()),
              const ntl::flt::swapped_buffer_view *>);
static_assert(std::is_same_v<
              decltype(std::declval<ntl::flt::swapped_io_buffers &>().apply()),
              ntl::status>);
static_assert(std::is_same_v<
              decltype(std::declval<
                           const ntl::ipc::mapped_io_buffers &>()
                           .has_open_mappings()),
              bool>);
static_assert(std::is_same_v<
              decltype(std::declval<
                           const ntl::flt::swapped_io_buffers &>()
                           .has_open_user_mappings()),
              bool>);
static_assert(std::is_same_v<
              decltype(std::declval<ntl::flt::swapped_io_buffers &>()
                           .complete()),
              ntl::status>);
static_assert(std::is_same_v<
              decltype(std::declval<ntl::flt::swapped_io_buffers &>().copy_back(
                  ntl::flt::as_post(
                      std::declval<ntl::flt::read_callback_data>()))),
              ntl::status>);

using swapped_fsctl_result = decltype(ntl::flt::try_swap_io_buffers(
    ntl::flt::as_pre(
        std::declval<ntl::flt::file_system_control_callback_data>()),
    ntl::flt::swap_buffer::all));
using swapped_ioctl_result = decltype(ntl::flt::try_swap_io_buffers(
    ntl::flt::as_pre(
        std::declval<ntl::flt::device_control_callback_data>()),
    ntl::flt::swap_buffer::all));
static_assert(std::is_same_v<swapped_fsctl_result, swapped_write_result>);
static_assert(std::is_same_v<swapped_ioctl_result, swapped_write_result>);
using swapped_create_result = decltype(ntl::flt::try_swap_io_buffers(
    ntl::flt::as_pre(std::declval<ntl::flt::create_callback_data>())));
using swapped_query_ea_result = decltype(ntl::flt::try_swap_io_buffers(
    ntl::flt::as_pre(std::declval<ntl::flt::query_ea_callback_data>()),
    ntl::flt::swap_buffer::output));
using swapped_query_quota_result = decltype(ntl::flt::try_swap_io_buffers(
    ntl::flt::as_pre(std::declval<ntl::flt::query_quota_callback_data>()),
    ntl::flt::swap_buffer::input));
using swapped_set_quota_result = decltype(ntl::flt::try_swap_io_buffers(
    ntl::flt::as_pre(std::declval<ntl::flt::set_quota_callback_data>())));
static_assert(std::is_same_v<swapped_create_result, swapped_write_result>);
static_assert(std::is_same_v<swapped_query_ea_result, swapped_write_result>);
static_assert(std::is_same_v<swapped_query_quota_result, swapped_write_result>);
static_assert(std::is_same_v<swapped_set_quota_result, swapped_write_result>);
static_assert(std::is_same_v<
              decltype(std::declval<ntl::flt::pending_pre_operation_queue &>()
                           .resume(std::declval<
                               ntl::ipc::pending_request_id>())),
              ntl::status>);

ntl::status deferred_pre_test_routine(
    PFLT_CALLBACK_DATA, void *, ntl::flt::deferred_pre_completion &) noexcept;
ntl::status deferred_post_test_routine(PFLT_CALLBACK_DATA, void *) noexcept;
static_assert(std::is_same_v<
              decltype(ntl::flt::queue_pre_operation_at_passive(
                  static_cast<PFLT_CALLBACK_DATA>(nullptr),
                  &deferred_pre_test_routine)),
              ntl::status>);
static_assert(std::is_same_v<
              decltype(ntl::flt::queue_post_operation_at_passive(
                  static_cast<PFLT_CALLBACK_DATA>(nullptr),
                  &deferred_post_test_routine)),
              ntl::status>);
static_assert(std::is_same_v<
              decltype(std::declval<
                           ntl::flt::pending_post_operation_registry &>()
                           .reply(std::declval<
                               ntl::ipc::pending_request_id>())),
              ntl::status>);

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
      data.set_io_status(STATUS_SUCCESS);
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
