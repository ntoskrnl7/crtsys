/**
 * @file registration_and_ownership.cpp
 * @brief Compile-time contract for registration and ownership facilities.
 *
 * This file is intentionally readable as a usage example. It is compiled and
 * linked into the x86/x64 driver test matrix, but the callbacks require a real
 * Filter Manager instance before they can be exercised at runtime.
 */

#include <ntl/flt/all>

#include <type_traits>
#include <utility>

namespace ntl_flt_registration_ownership_compile_test {

struct transaction_state {
  transaction_state() noexcept = default;
  ~transaction_state() noexcept = default;

  ULONG last_notification = 0;
};

#if FLT_MGR_WIN8
struct section_state {
  section_state() noexcept = default;
  ~section_state() noexcept = default;

  bool conflict_seen = false;
};
#endif

inline constexpr ntl::flt::transaction_context<transaction_state>
    transaction_state_context{};
#if FLT_MGR_WIN8
inline constexpr ntl::flt::section_context<section_state>
    section_state_context{};
#endif

ntl::status
generate_file_name(ntl::flt::name_generation_request request,
                   ntl::flt::name_generation_output output) noexcept {
  if (!output || !request.target_instance() || !request.target_file())
    return STATUS_INVALID_PARAMETER;

  (void)request.options().normalized();
  (void)request.callback_data();
  const ntl::status status =
      output.name().try_assign(LR"(\Device\Volume\mapped.txt)");
  if (status.is_err())
    return status;
  output.set_cache(true);
  return STATUS_SUCCESS;
}

ntl::status
normalize_name_component(ntl::flt::name_normalization_request request,
                         ntl::flt::name_normalization_output output) noexcept {
  output.context().set(nullptr);
  return output.expanded_name().try_assign(request.component());
}

void normalize_context_cleanup(
    ntl::flt::normalization_context context) noexcept {
  context.set(nullptr);
}

#if FLT_MGR_LONGHORN
ntl::status transaction_notification(
    ntl::flt::related_objects, ntl::flt::context_view<transaction_state> state,
    ntl::flt::transaction_notifications notifications) noexcept {
  if (!state)
    return STATUS_OBJECT_TYPE_MISMATCH;
  state->last_notification = static_cast<ULONG>(notifications);
  return STATUS_SUCCESS;
}

ntl::status normalize_name_component_ex(
    ntl::flt::name_normalization_request request,
    ntl::flt::name_normalization_output output) noexcept {
  if (!request.target_file())
    return STATUS_INVALID_PARAMETER;
  output.context().set(nullptr);
  return output.expanded_name().try_assign(request.component());
}
#endif

#if FLT_MGR_WIN8
ntl::status section_conflict(ntl::flt::instance target_instance,
                             ntl::flt::context_view<section_state> state,
                             ntl::flt::callback_data_view data) noexcept {
  if (!target_instance || !state || !data)
    return STATUS_INVALID_PARAMETER;
  state->conflict_seen = true;
  return STATUS_SUCCESS;
}
#endif

void complete_owned_io(ntl::flt::callback_data_owner) noexcept {}

struct io_completion_state {};

void complete_owned_io_with_context(ntl::flt::callback_data_owner,
                                    io_completion_state *) noexcept {}

void incoming_io_cancelled(ntl::flt::callback_data_view) noexcept {}

template <class Owner, class = void>
struct exposes_generated_cancel_completion : std::false_type {};

template <class Owner>
struct exposes_generated_cancel_completion<
    Owner, std::void_t<decltype(std::declval<Owner &>()
                                    .template try_set_cancel_completion<
                                        incoming_io_cancelled>())>>
    : std::true_type {};

[[maybe_unused]] ntl::status
use_transaction_context(ntl::flt::related_objects objects) noexcept {
#if FLT_MGR_LONGHORN
  auto state = objects.try_get_or_create(transaction_state_context);
  if (!state)
    return state.status();

  const ntl::status enlist = objects.try_enlist(
      *state, ntl::flt::transaction_notifications::commit_finalize |
                  ntl::flt::transaction_notifications::rollback);
  if (enlist.is_err())
    return enlist;

  // Removal is optional policy work. The returned reference keeps the C++
  // state alive even after it is detached from the transaction.
  auto removed = objects.try_remove(transaction_state_context);
  return removed ? ntl::status::ok() : removed.status();
#else
  (void)objects;
  return STATUS_NOT_SUPPORTED;
#endif
}

#if FLT_MGR_WIN8
[[maybe_unused]] ntl::status create_and_close_kernel_scan_section(
    ntl::flt::related_objects objects) noexcept {
  const ntl::status registered =
      ntl::flt::try_register_for_data_scan(objects.instance());
  if (registered.is_err())
    return registered;

  auto context = ntl::flt::try_allocate_section_context(
      objects.filter().native_handle(), section_state_context);
  if (!context)
    return context.status();

  OBJECT_ATTRIBUTES attributes;
  InitializeObjectAttributes(&attributes, nullptr, OBJ_KERNEL_HANDLE, nullptr,
                             nullptr);
  ntl::flt::data_scan_section_options options;
  options.object_attributes = &attributes;

  auto section = ntl::flt::try_create_data_scan_section(
      objects.instance(), objects.file(), *context, options);
  if (!section)
    return section.status();

  // The official contract requires the creator to close the handle and
  // dereference the object before closing Filter Manager's association.
  const NTSTATUS handle_status = ZwClose(section->handle);
  ObDereferenceObject(section->object);
  const ntl::status close_status = ntl::flt::close_data_scan_section(*context);
  return NT_SUCCESS(handle_status) ? close_status : ntl::status{handle_status};
}
#endif

[[maybe_unused]] ntl::status
issue_owned_io(ntl::flt::related_objects objects) noexcept {
  auto request =
      ntl::flt::try_allocate_callback_data(objects.instance(), objects.file());
  if (!request)
    return request.status();

  // The sample intentionally does not configure an IOPB. Production code
  // fills request->iopb() for the desired operation before choosing sync or
  // async execution.
  auto operation = request->try_perform_asynchronously<complete_owned_io>();
  return operation ? ntl::status::ok() : operation.status();
}

[[maybe_unused]] ntl::flt::registration make_registration() {
  ntl::flt::registration callbacks;
  callbacks.on_generate_file_name(generate_file_name)
      .on_normalize_name_component(normalize_name_component)
      .on_normalize_context_cleanup(normalize_context_cleanup);
#if FLT_MGR_LONGHORN
  callbacks
      .on_transaction_notification(transaction_state_context,
                                   transaction_notification)
      .on_normalize_name_component_ex(normalize_name_component_ex);
#endif
#if FLT_MGR_WIN8
  callbacks.on_section_notification(section_state_context, section_conflict);
#endif
  return callbacks;
}

static_assert(!std::is_copy_constructible_v<ntl::flt::callback_data_owner>);
static_assert(
    std::is_nothrow_move_constructible_v<ntl::flt::callback_data_owner>);
static_assert(
    std::is_copy_constructible_v<ntl::flt::async_callback_data_operation>);
static_assert(
    !exposes_generated_cancel_completion<ntl::flt::callback_data_owner>::value);
static_assert(std::is_trivially_copyable_v<ntl::flt::callback_data_view>);
static_assert(std::is_trivially_copyable_v<ntl::flt::name_control>);
static_assert(std::is_trivially_copyable_v<ntl::flt::normalization_context>);
static_assert(sizeof(ntl::flt::callback_data_view) == sizeof(void *));
static_assert(sizeof(ntl::flt::context_view<transaction_state>) ==
              sizeof(void *));
static_assert(!std::is_same_v<ntl::flt::generate_file_name_callback,
                              PFLT_GENERATE_FILE_NAME>);
static_assert(std::is_convertible_v<decltype(&generate_file_name),
                                    ntl::flt::generate_file_name_callback>);
static_assert(!std::is_convertible_v<PFLT_GENERATE_FILE_NAME,
                                     ntl::flt::generate_file_name_callback>);
static_assert(
    !std::is_convertible_v<PFLT_NORMALIZE_NAME_COMPONENT,
                           ntl::flt::normalize_name_component_callback>);
#if FLT_MGR_LONGHORN
static_assert(!std::is_convertible_v<
              PFLT_TRANSACTION_NOTIFICATION_CALLBACK,
              ntl::flt::transaction_notification_callback<transaction_state>>);
#endif
#if FLT_MGR_WIN8
static_assert(!std::is_convertible_v<
              PFLT_SECTION_CONFLICT_NOTIFICATION_CALLBACK,
              ntl::flt::section_notification_callback<section_state>>);
#endif
static_assert(std::is_same_v<
              decltype(std::declval<ntl::flt::callback_data_owner &>()
                           .template release_and_perform_asynchronously<
                               complete_owned_io_with_context>(
                               static_cast<io_completion_state *>(nullptr))),
              ntl::status>);
static_assert(
    std::is_same_v<
        decltype(std::declval<ntl::flt::callback_data_owner &>()
                     .template try_perform_asynchronously<complete_owned_io>()),
        ntl::result<ntl::flt::async_callback_data_operation>>);
static_assert(
    std::is_same_v<decltype(make_registration()), ntl::flt::registration>);
static_assert(std::is_same_v<
              decltype(std::declval<ntl::flt::name_generation_request>()
                           .try_query_lower_name(FLT_FILE_NAME_OPENED |
                                                 FLT_FILE_NAME_QUERY_DEFAULT)),
              ntl::result<ntl::flt::name_information>>);
static_assert(
    std::is_same_v<decltype(std::declval<ntl::flt::instance>().try_get(
                       std::declval<const ntl::flt::instance_context<
                           transaction_state> &>())),
                   ntl::result<ntl::flt::context_ref<
                       transaction_state, ntl::flt::context_scope::instance>>>);

} // namespace ntl_flt_registration_ownership_compile_test
