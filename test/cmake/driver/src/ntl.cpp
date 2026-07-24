#include <ntl/event>
#include <ntl/expand_stack>
#include <ntl/except>
#include <ntl/device>
#include <ntl/device_endpoint>
#include <ntl/device_interface>
#include <ntl/handle>
#include <ntl/ioctl>
#include <ntl/irp>
#include <ntl/ipc/all>
#include <ntl/irql>
#include <ntl/lookaside_list>
#include <ntl/mdl>
#include <ntl/passive_executor>
#include <ntl/pool_allocator>
#include <ntl/registry>
#include <ntl/remove_lock>
#include <ntl/result>
#include <ntl/symbolic_link>
#include <ntl/system_thread>
#include <ntl/timer>
#include <ntl/unicode_string>
#include <ntl/wait>
#include <ntl/work_item>

#include <memory_resource>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <memory>
#include <numeric>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

template <class T, class = void>
struct supports_boolean_negation : std::false_type {};

template <class T>
struct supports_boolean_negation<
    T, std::void_t<decltype(!std::declval<T>())>> : std::true_type {};

static_assert(!supports_boolean_negation<ntl::status>::value,
              "ntl::status must not be tested as a Boolean; use is_ok/is_err");

void record_driver_entry_failure(const char *stage, long code);

extern "C" NTSYSAPI NTSTATUS NTAPI
ZwCreateEvent(_Out_ PHANDLE EventHandle, _In_ ACCESS_MASK DesiredAccess,
              _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
              _In_ EVENT_TYPE EventType, _In_ BOOLEAN InitialState);

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

struct ioctl_test_input {
  std::uint32_t value;
};

struct ioctl_test_output {
  std::uint32_t value;
};

struct ioctl_pipeline_input {
  std::uint32_t value;
};

struct ioctl_pipeline_output {
  std::uint32_t value;
  std::uint32_t checksum;
};

// IO_REMOVE_LOCK storage must remain at a unique, nonpaged address for the
// lifetime of the driver. In particular, Driver Verifier rejects
// reinitializing a released lock when a later stack frame reuses its address.
// Each single-shot test therefore receives its own image-lifetime lock.
struct remove_lock_test_storage {
  ntl::remove_lock pipeline{ntl::pool_tag("NTPp")};
  ntl::remove_lock standalone{ntl::pool_tag("NTRm")};
};

remove_lock_test_storage g_remove_lock_test_storage;

struct ipc_ring_test_record {
  std::uint32_t sequence;
  std::uint32_t value;
};

bool ntl_ipc_test_impl() {
  constexpr ntl::ipc::region_handle region{7, 11};
  alignas(64) unsigned char region_storage[512]{};
  ntl::ipc::region_view view{region, region_storage, sizeof(region_storage),
                             ntl::ipc::region_access::driver_read_write};

  ntl::ipc::buffer_token token;
  if (view.token(64, 128, token) != ntl::ipc::validation_status::success ||
      token.region != region || token.offset != 64 || token.length != 128)
    return false;

  ntl::ipc::mutable_buffer_view resolved;
  if (view.resolve(token, ntl::ipc::region_access::driver_write, resolved) !=
          ntl::ipc::validation_status::success ||
      resolved.data() != region_storage + 64 || resolved.size() != 128)
    return false;

  auto stale = token;
  ++stale.region.generation;
  if (view.resolve(stale, ntl::ipc::region_access::driver_read, resolved) !=
      ntl::ipc::validation_status::stale_region)
    return false;

  auto overflow = token;
  overflow.offset = sizeof(region_storage) - 1;
  overflow.length = 2;
  if (view.resolve(overflow, ntl::ipc::region_access::driver_read, resolved) !=
      ntl::ipc::validation_status::range_overflow)
    return false;

  using ring_type = ntl::ipc::shared_ring<ipc_ring_test_record, 4>;
  alignas(64) unsigned char ring_storage[ring_type::required_bytes()]{};
  ring_type writer;
  if (ring_type::initialize(ring_storage, sizeof(ring_storage), writer, 19) !=
          ntl::ipc::validation_status::success ||
      writer.epoch() != 19)
    return false;

  ring_type reader;
  if (ring_type::attach(ring_storage, sizeof(ring_storage), reader) !=
      ntl::ipc::validation_status::success)
    return false;

  for (std::uint32_t index = 0; index != 4; ++index) {
    if (!writer.try_write({index, index * 10}))
      return false;
  }
  if (writer.try_write({4, 40}) || writer.writable() != 0 ||
      reader.readable() != 4)
    return false;

  ipc_ring_test_record record{};
  for (std::uint32_t index = 0; index != 2; ++index) {
    if (!reader.try_read(record) || record.sequence != index ||
        record.value != index * 10)
      return false;
  }

  if (!writer.try_write({4, 40}) || !writer.try_write({5, 50}))
    return false;
  for (std::uint32_t index = 2; index != 6; ++index) {
    if (!reader.try_read(record) || record.sequence != index ||
        record.value != index * 10)
      return false;
  }
  return !reader.try_read(record) && reader.readable() == 0 &&
         writer.writable() == 4;
}

bool ntl_mapped_buffer_lifetime_test_impl() {
  const auto fail = [](const char *stage, long code) {
    record_driver_entry_failure(stage, code);
    return false;
  };

  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return fail("mapped_lifetime.passive_level", KeGetCurrentIrql());
  MDL synthetic_mdl{};
  synthetic_mdl.ByteCount = PAGE_SIZE;
  if (!ntl::ipc::detail::mdl_mapping_is_page_isolated(
          &synthetic_mdl, 0, PAGE_SIZE) ||
      ntl::ipc::detail::mdl_mapping_is_page_isolated(
          &synthetic_mdl, 0, PAGE_SIZE / 2))
    return fail("mapped_lifetime.page_isolation", 1);
  synthetic_mdl.ByteOffset = 1;
  if (ntl::ipc::detail::mdl_mapping_is_page_isolated(
          &synthetic_mdl, 0, PAGE_SIZE))
    return fail("mapped_lifetime.unaligned_isolation", 1);
  std::size_t charge = 0;
  if (!ntl::ipc::detail::mdl_mapping_charge(&synthetic_mdl, charge) ||
      charge != PAGE_SIZE * 2 ||
      !ntl::ipc::detail::isolated_page_charge(1, charge) ||
      charge != PAGE_SIZE)
    return fail("mapped_lifetime.mapping_charge",
                static_cast<long>(charge));
  auto connection =
      ntl::ipc::process_connection::try_capture_current_process();
  if (!connection)
    return fail("mapped_lifetime.capture_connection",
                static_cast<NTSTATUS>(connection.status()));
  auto pages = ntl::ipc::page_buffer::try_create(PAGE_SIZE);
  if (!pages)
    return fail("mapped_lifetime.create_pages",
                static_cast<NTSTATUS>(pages.status()));

  auto mapped = ntl::ipc::try_map_page_buffer(
      *pages, 16, 128, *connection, ntl::ipc::map_access::read_write);
  if (!mapped)
    return fail("mapped_lifetime.map_first",
                static_cast<NTSTATUS>(mapped.status()));
  if (connection->mapping_count() != 1)
    return fail("mapped_lifetime.first_count",
                static_cast<long>(connection->mapping_count()));
  auto copied = *mapped;
  const auto descriptor = mapped->descriptor();
  if (!descriptor.valid() || descriptor.generation != connection->generation() ||
      descriptor.length != 128 ||
      descriptor.access !=
          static_cast<std::uint32_t>(ntl::ipc::map_access::read_write))
    return fail("mapped_lifetime.first_descriptor",
                static_cast<long>(descriptor.access));
  const auto first_close = mapped->close();
  if (!first_close.is_ok())
    return fail("mapped_lifetime.close_first",
                static_cast<NTSTATUS>(first_close));
  if (mapped->is_open() || copied.is_open() ||
      connection->mapping_count() != 0)
    return fail("mapped_lifetime.first_close_state",
                static_cast<long>(connection->mapping_count()));

  auto forced = ntl::ipc::try_map_page_buffer(
      *pages, 0, 64, *connection, ntl::ipc::map_access::read);
  if (!forced)
    return fail("mapped_lifetime.map_forced",
                static_cast<NTSTATUS>(forced.status()));
  if (connection->mapping_count() != 1)
    return fail("mapped_lifetime.forced_count",
                static_cast<long>(connection->mapping_count()));
  const auto forced_copy = *forced;
  const auto connection_close = connection->close();
  if (!connection_close.is_ok())
    return fail("mapped_lifetime.close_connection",
                static_cast<NTSTATUS>(connection_close));
  if (forced_copy.is_open() || connection->mapping_count() != 0)
    return fail("mapped_lifetime.connection_close_state",
                static_cast<long>(connection->mapping_count()));

  auto rejected = ntl::ipc::try_map_page_buffer(
      *pages, 0, 64, *connection, ntl::ipc::map_access::read);
  if (rejected ||
      static_cast<NTSTATUS>(rejected.status()) != STATUS_DELETE_PENDING)
    return fail("mapped_lifetime.reject_closed_connection",
                rejected ? STATUS_SUCCESS
                         : static_cast<NTSTATUS>(rejected.status()));

  auto limited = ntl::ipc::process_connection::try_capture_current_process(
      {.max_mappings = 1, .max_mapped_bytes = PAGE_SIZE});
  if (!limited)
    return fail("mapped_lifetime.capture_limited_connection",
                static_cast<NTSTATUS>(limited.status()));
  auto first = ntl::ipc::try_map_page_buffer(
      *pages, 0, 64, *limited, ntl::ipc::map_access::read);
  if (!first)
    return fail("mapped_lifetime.map_limited_first",
                static_cast<NTSTATUS>(first.status()));
  auto over_quota = ntl::ipc::try_map_page_buffer(
      *pages, 64, 64, *limited, ntl::ipc::map_access::read);
  if (over_quota ||
      static_cast<NTSTATUS>(over_quota.status()) != STATUS_QUOTA_EXCEEDED)
    return fail("mapped_lifetime.enforce_quota",
                over_quota ? STATUS_SUCCESS
                           : static_cast<NTSTATUS>(over_quota.status()));
  auto invalid_access = ntl::ipc::try_map_page_buffer(
      *pages, 0, 64, *limited,
      static_cast<ntl::ipc::map_access>(0xffffffffu));
  if (invalid_access)
    return fail("mapped_lifetime.reject_invalid_access", STATUS_SUCCESS);
  const auto limited_first_close = first->close();
  if (!limited_first_close.is_ok())
    return fail("mapped_lifetime.close_limited_first",
                static_cast<NTSTATUS>(limited_first_close));
  const auto limited_close = limited->close();
  if (!limited_close.is_ok())
    return fail("mapped_lifetime.close_limited_connection",
                static_cast<NTSTATUS>(limited_close));
  return true;
}

bool ntl_irp_io_buffer_semantics_test_impl() {
  const auto fail = [](const char *stage, long code) {
    record_driver_entry_failure(stage, code);
    return false;
  };

  if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    return fail("irp_io_buffer.passive_level", KeGetCurrentIrql());

  auto connection =
      ntl::ipc::process_connection::try_capture_current_process(
          {.max_mappings = 4, .max_mapped_bytes = PAGE_SIZE * 4});
  if (!connection)
    return fail("irp_io_buffer.capture_connection",
                static_cast<NTSTATUS>(connection.status()));
  auto storage = ntl::ipc::page_buffer::try_create(PAGE_SIZE);
  if (!storage)
    return fail("irp_io_buffer.create_storage",
                static_cast<NTSTATUS>(storage.status()));

  struct irp_deleter {
    void operator()(PIRP value) const noexcept {
      if (value)
        IoFreeIrp(value);
    }
  };
  std::unique_ptr<IRP, irp_deleter> request{IoAllocateIrp(1, FALSE)};
  if (!request)
    return fail("irp_io_buffer.allocate_irp", STATUS_INSUFFICIENT_RESOURCES);
  IoSetNextIrpStackLocation(request.get());
  auto *const stack = IoGetCurrentIrpStackLocation(request.get());
  if (!stack)
    return fail("irp_io_buffer.current_stack", STATUS_INVALID_PARAMETER);

  DEVICE_OBJECT device{};
  device.Flags = DO_BUFFERED_IO;
  request->RequestorMode = KernelMode;
  request->AssociatedIrp.SystemBuffer = (*storage)->data();

  const auto reset = [&](UCHAR major) noexcept {
    RtlZeroMemory(stack, sizeof(*stack));
    stack->MajorFunction = major;
    stack->DeviceObject = &device;
    request->Flags = 0;
    request->IoStatus.Status = STATUS_SUCCESS;
    request->IoStatus.Information = 0;
    request->AssociatedIrp.SystemBuffer = (*storage)->data();
  };

  // A WRITE is logical input only.
  reset(IRP_MJ_WRITE);
  stack->Parameters.Write.Length = 16;
  const unsigned char write_pattern[16] = {
      0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
      0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};
  RtlCopyMemory((*storage)->data(), write_pattern, sizeof(write_pattern));
  {
    auto mapped = ntl::ipc::try_map_io_buffers(
        &device, request.get(), *connection);
    if (!mapped)
      return fail("irp_io_buffer.map_write",
                  static_cast<NTSTATUS>(mapped.status()));
    if (!mapped->input() || mapped->output() || mapped->control_input() ||
        mapped->input()->size() != sizeof(write_pattern) ||
        mapped->input()->access() != ntl::ipc::map_access::read)
      return fail("irp_io_buffer.write_shape", STATUS_DATA_ERROR);
    unsigned char observed[sizeof(write_pattern)]{};
    const auto copied = ntl::ipc::detail::guarded_copy(
        observed, mapped->input()->target_address(), sizeof(observed));
    if (!copied.is_ok() ||
        RtlCompareMemory(observed, write_pattern, sizeof(observed)) !=
            sizeof(observed))
      return fail("irp_io_buffer.write_contents",
                  static_cast<NTSTATUS>(copied));
    const auto closed = mapped->close();
    if (!closed.is_ok() || mapped->has_open_mappings())
      return fail("irp_io_buffer.close_write",
                  static_cast<NTSTATUS>(closed));
  }

  // A READ is logical output only, and writable staging copies back only when
  // its mapping is closed.
  reset(IRP_MJ_READ);
  stack->Parameters.Read.Length = 16;
  RtlFillMemory((*storage)->data(), 16, 0xcc);
  const unsigned char read_result[16] = {
      0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
      0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf};
  {
    auto mapped = ntl::ipc::try_map_io_buffers(
        &device, request.get(), *connection);
    if (!mapped)
      return fail("irp_io_buffer.map_read",
                  static_cast<NTSTATUS>(mapped.status()));
    if (mapped->input() || !mapped->output() || mapped->control_input() ||
        mapped->output()->size() != sizeof(read_result) ||
        mapped->output()->access() != ntl::ipc::map_access::read_write)
      return fail("irp_io_buffer.read_shape", STATUS_DATA_ERROR);
    const auto copied = ntl::ipc::detail::guarded_copy(
        mapped->output()->target_address(), read_result, sizeof(read_result));
    if (!copied.is_ok())
      return fail("irp_io_buffer.write_read_result",
                  static_cast<NTSTATUS>(copied));
    const auto closed = mapped->close();
    if (!closed.is_ok() ||
        RtlCompareMemory((*storage)->data(), read_result,
                         sizeof(read_result)) != sizeof(read_result))
      return fail("irp_io_buffer.read_copy_back",
                  static_cast<NTSTATUS>(closed));
  }

  // Completed output is clamped to IoStatus.Information. Warning statuses
  // preserve partial data, and bytes beyond the completed range are untouched.
  reset(IRP_MJ_READ);
  stack->Parameters.Read.Length = 16;
  request->IoStatus.Status = STATUS_BUFFER_OVERFLOW;
  request->IoStatus.Information = 5;
  RtlFillMemory((*storage)->data(), 16, 0x5a);
  {
    auto mapped = ntl::ipc::try_map_completed_io_buffers(
        &device, request.get(), *connection);
    if (!mapped)
      return fail("irp_io_buffer.map_completed_read",
                  static_cast<NTSTATUS>(mapped.status()));
    if (!mapped->output() || mapped->output()->size() != 5)
      return fail("irp_io_buffer.completed_read_length", STATUS_DATA_ERROR);
    const unsigned char partial[5] = {1, 2, 3, 4, 5};
    const auto copied = ntl::ipc::detail::guarded_copy(
        mapped->output()->target_address(), partial, sizeof(partial));
    if (!copied.is_ok())
      return fail("irp_io_buffer.write_partial_read",
                  static_cast<NTSTATUS>(copied));
    const auto closed = mapped->close();
    const auto *const bytes =
        static_cast<const unsigned char *>((*storage)->data());
    if (!closed.is_ok() ||
        RtlCompareMemory(bytes, partial, sizeof(partial)) != sizeof(partial) ||
        bytes[sizeof(partial)] != 0x5a)
      return fail("irp_io_buffer.partial_copy_back",
                  static_cast<NTSTATUS>(closed));
  }

  reset(IRP_MJ_READ);
  stack->Parameters.Read.Length = 16;
  request->IoStatus.Status = STATUS_ACCESS_DENIED;
  request->IoStatus.Information = 7;
  {
    auto mapped = ntl::ipc::try_map_completed_io_buffers(
        &device, request.get(), *connection);
    if (mapped ||
        static_cast<NTSTATUS>(mapped.status()) != STATUS_ACCESS_DENIED)
      return fail("irp_io_buffer.reject_failed_read",
                  mapped ? STATUS_SUCCESS
                         : static_cast<NTSTATUS>(mapped.status()));
  }

  // Generic FSCTL IRPs use the common buffer layout only for the documented
  // user/kernel request minors.
  reset(IRP_MJ_FILE_SYSTEM_CONTROL);
  stack->MinorFunction = 0x00; // IRP_MN_USER_FS_REQUEST
  stack->Parameters.FileSystemControl.FsControlCode =
      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS);
  stack->Parameters.FileSystemControl.InputBufferLength = 8;
  stack->Parameters.FileSystemControl.OutputBufferLength = 16;
  {
    auto mapped = ntl::ipc::try_map_io_buffers(
        &device, request.get(), *connection);
    if (!mapped || !mapped->input() || !mapped->output() ||
        mapped->control_input() || mapped->input()->size() != 8 ||
        mapped->output()->size() != 16 ||
        mapped->input()->descriptor().mapping_id !=
            mapped->output()->descriptor().mapping_id)
      return fail("irp_io_buffer.fsctl_buffered_shape",
                  mapped ? STATUS_DATA_ERROR
                         : static_cast<NTSTATUS>(mapped.status()));
    const auto closed = mapped->close();
    if (!closed.is_ok())
      return fail("irp_io_buffer.close_fsctl",
                  static_cast<NTSTATUS>(closed));
  }
  stack->MinorFunction = 0x01; // IRP_MN_MOUNT_VOLUME, no common buffer layout
  {
    auto mapped = ntl::ipc::try_map_io_buffers(
        &device, request.get(), *connection);
    if (mapped || static_cast<NTSTATUS>(mapped.status()) != STATUS_NOT_SUPPORTED)
      return fail("irp_io_buffer.reject_nonbuffer_fsctl",
                  mapped ? STATUS_SUCCESS
                         : static_cast<NTSTATUS>(mapped.status()));
  }

  if (connection->mapping_count() != 0)
    return fail("irp_io_buffer.mapping_count",
                static_cast<long>(connection->mapping_count()));
  const auto closed = connection->close();
  if (!closed.is_ok())
    return fail("irp_io_buffer.close_connection",
                static_cast<NTSTATUS>(closed));
  return true;
}

void delete_symbolic_link_if_present(const std::wstring &link_name) {
  ntl::unicode_string native_link_name(link_name);
  (void)IoDeleteSymbolicLink(&*native_link_name);
}

void delete_registry_key_if_present(const std::wstring &key_path) {
  auto key = ntl::registry_key::open(key_path, DELETE);
  if (key)
    (void)key->delete_key();
}

struct work_item_test_context {
  std::atomic<long> value{0};
  ntl::event release{ntl::event_type::notification, false};
};

struct passive_executor_test_context {
  ntl::event completed{ntl::event_type::notification, false};
  std::atomic<long> value{0};
  std::atomic<ntl::irql> observed_irql{ntl::irql::dispatch};
};

struct dpc_passive_executor_test_context {
  ntl::event dpc_completed{ntl::event_type::notification, false};
  ntl::event passive_completed{ntl::event_type::notification, false};
  ntl::passive_executor *executor = nullptr;
  std::atomic<long> value{0};
  std::atomic<NTSTATUS> post_status{STATUS_UNSUCCESSFUL};
  std::atomic<ntl::irql> dpc_irql{ntl::irql::passive};
  std::atomic<ntl::irql> passive_irql{ntl::irql::dispatch};
};

struct timer_dpc_test_context {
  ntl::event fired{ntl::event_type::notification, false};
  std::atomic<long> count{0};
  std::atomic<void *> system_argument1{nullptr};
  std::atomic<void *> system_argument2{nullptr};
  std::atomic<ntl::irql> observed_irql{ntl::irql::passive};
};

struct system_thread_test_context {
  std::atomic<long> value{0};
  std::atomic<ntl::irql> observed_irql{ntl::irql::dispatch};
};

void timer_dpc_test_routine(void *context, void *system_argument1,
                            void *system_argument2) noexcept {
  auto *const state = static_cast<timer_dpc_test_context *>(context);
  state->system_argument1.store(system_argument1);
  state->system_argument2.store(system_argument2);
  state->observed_irql.store(ntl::current_irql());
  state->count.fetch_add(1);
  state->fired.set();
}

void dpc_to_passive_executor_test_routine(void *context, void *,
                                          void *) noexcept {
  auto *const state =
      static_cast<dpc_passive_executor_test_context *>(context);
  state->dpc_irql.store(ntl::current_irql());

  if (!state->executor) {
    state->post_status.store(STATUS_INVALID_DEVICE_STATE);
    state->dpc_completed.set();
    return;
  }

  const auto status = state->executor->post([state] {
    state->passive_irql.store(ntl::current_irql());
    state->value.store(15);
    state->passive_completed.set();
  });
  state->post_status.store(static_cast<NTSTATUS>(status));
  state->dpc_completed.set();
}

void system_thread_test_routine(void *context) {
  auto *const state = static_cast<system_thread_test_context *>(context);
  state->observed_irql.store(ntl::current_irql());
  state->value.store(42);
  PsTerminateSystemThread(STATUS_SUCCESS);
}

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
  if (!ntl::is_passive_level())
    return false;
  if (!ntl::is_irql_at_most(ntl::irql::passive))
    return false;
  if (!ntl::require_passive_level().is_ok())
    return false;
  if (!ntl::require_irql_at_most(ntl::irql::passive).is_ok())
    return false;

  {
    auto raised_irql = ntl::raise_irql(ntl::irql::apc);

    if (old_irql != raised_irql.old())
      return false;

    if (ntl::current_irql() != ntl::irql::apc)
      return false;
    if (ntl::is_passive_level())
      return false;
    if (!ntl::is_irql_at_most(ntl::irql::apc))
      return false;
    if (ntl::is_irql_at_most(ntl::irql::passive))
      return false;
    if (static_cast<NTSTATUS>(ntl::require_passive_level()) !=
        STATUS_INVALID_DEVICE_STATE)
      return false;
    if (static_cast<NTSTATUS>(ntl::require_irql_at_most(ntl::irql::passive)) !=
        STATUS_INVALID_DEVICE_STATE)
      return false;
    if (!ntl::require_irql_at_most(ntl::irql::apc).is_ok())
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
    if (ntl::is_irql_at_most(ntl::irql::apc))
      return false;
    if (!ntl::is_irql_at_most(ntl::irql::dispatch))
      return false;
    if (static_cast<NTSTATUS>(ntl::require_irql_at_most(ntl::irql::apc)) !=
        STATUS_INVALID_DEVICE_STATE)
      return false;
    if (!ntl::require_irql_at_most(ntl::irql::dispatch).is_ok())
      return false;
  }
  old_irql = ntl::current_irql();
  {
    auto raised_irql = ntl::raise_irql_to_synch_level();
    if (old_irql != raised_irql.old())
      return false;
  }
  return ntl::current_irql() == old_irql &&
         ntl::require_passive_level().is_ok();
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

  auto raw_result = ntl::try_allocate_pool(
      80, ntl::pool_kind::nonpaged, ntl::pool_option::none, "NTRr");
  if (!raw_result || !raw_result.value())
    return false;
  ntl::free_pool(raw_result.value(), "NTRr");

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
  static_assert(sizeof(lookaside_test_object) <= sizeof(SLIST_ENTRY),
                "This test object must not exceed the native SLIST minimum");
  static_assert(nonpaged_list::entry_size() == sizeof(SLIST_ENTRY),
                "Lookaside entries must satisfy the native SLIST minimum");
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

  auto try_object = list.try_make(35);
  if (!try_object || !try_object->get() || (*try_object)->value != 35 ||
      g_lookaside_test_object_count != 1)
    return false;
  try_object->reset();
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

  auto raw_result = ntl::try_allocate_pool(
      24, ntl::pool_kind::nonpaged, ntl::pool_option::none, "NTRr");
  if (!raw_result || !raw_result.value())
    return false;
  ntl::free_pool(raw_result.value(), "NTRr");

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

struct device_control_helper_payload {
  ULONG value;
};

bool ntl_irp_device_control_helpers_test() {
  IRP raw_irp{};
  ntl::irp request(&raw_irp);

  request.succeed(12);
  if (request.status() != STATUS_SUCCESS || request.information() != 12)
    return false;

  request.fail(STATUS_INVALID_PARAMETER);
  if (request.status() != STATUS_INVALID_PARAMETER ||
      request.information() != 0)
    return false;

  request.set_result(STATUS_BUFFER_TOO_SMALL, 4);
  if (request.status() != STATUS_BUFFER_TOO_SMALL ||
      request.information() != 4)
    return false;

  const device_control_helper_payload input{0x12345678};
  ntl::device_control::in_buffer in(&input, sizeof(input));
  if (!in.can_read(sizeof(input)))
    return false;
  const auto *typed_input = in.as<device_control_helper_payload>();
  if (!typed_input || typed_input->value != input.value)
    return false;

  ntl::device_control::in_buffer short_in(&input, sizeof(input) - 1);
  if (short_in.can_read(sizeof(input)) || short_in.as<device_control_helper_payload>())
    return false;

  alignas(device_control_helper_payload) unsigned char output[sizeof(input)]{};
  ntl::device_control::out_buffer out(output, sizeof(output));
  const device_control_helper_payload reply{0x87654321};
  if (!out.can_write(sizeof(reply)) || !out.write(reply) ||
      out.size != sizeof(reply))
    return false;
  const auto *written = reinterpret_cast<const device_control_helper_payload *>(
      output);
  if (written->value != reply.value)
    return false;
  auto *typed_output = out.as<device_control_helper_payload>();
  if (!typed_output || typed_output->value != reply.value)
    return false;

  unsigned char small_output[sizeof(reply) - 1]{};
  ntl::device_control::out_buffer small(small_output, sizeof(small_output));
  if (small.write(reply) || small.size != 0)
    return false;

  char text[8]{};
  ntl::device_control::out_buffer text_out(text, sizeof(text));
  constexpr char expected[] = "ok";
  if (!text_out.write_bytes(expected, sizeof(expected)) ||
      text_out.size != sizeof(expected) ||
      std::memcmp(text, expected, sizeof(expected)) != 0)
    return false;

  text_out.clear();
  return text_out.size == 0;
}

struct ntl_test_ioctl_contract {
  using input_type = ioctl_test_input;
  using output_type = ioctl_test_output;

  static constexpr ULONG device_type = FILE_DEVICE_UNKNOWN;
  static constexpr ULONG function = 0x810;
  static constexpr ULONG method = METHOD_BUFFERED;
  static constexpr ULONG access = FILE_READ_DATA | FILE_WRITE_DATA;
};

using ntl_test_ioctl = ntl::ioctl_from_contract<ntl_test_ioctl_contract>;

using ntl_pipeline_ioctl =
    ntl::ioctl<FILE_DEVICE_UNKNOWN, 0x812, METHOD_BUFFERED,
               FILE_READ_DATA | FILE_WRITE_DATA, ioctl_pipeline_input,
               ioctl_pipeline_output>;

bool ntl_ioctl_test() {
  constexpr ULONG expected_code =
      CTL_CODE(FILE_DEVICE_UNKNOWN, 0x810, METHOD_BUFFERED,
               FILE_READ_DATA | FILE_WRITE_DATA);

  static_assert(ntl_test_ioctl::code == expected_code,
                "typed IOCTL descriptor must match CTL_CODE");
  static_assert(
      std::is_same<ntl_test_ioctl::input_type, ioctl_test_input>::value,
      "typed IOCTL input payload mismatch");
  static_assert(
      std::is_same<ntl_test_ioctl::output_type, ioctl_test_output>::value,
      "typed IOCTL output payload mismatch");

  const auto code = ntl_test_ioctl::control_code();
  if (!ntl::is_ioctl<ntl_test_ioctl>(code) ||
      ntl::is_ioctl<ntl_test_ioctl>(
          ntl::device_control::code{CTL_CODE(FILE_DEVICE_UNKNOWN, 0x811,
                                             METHOD_BUFFERED,
                                             FILE_READ_DATA)}))
    return false;

  const ioctl_test_input input{0x1234};
  ntl::device_control::in_buffer in(&input, sizeof(input));
  const auto *typed_input = ntl::ioctl_input_as<ntl_test_ioctl>(in);
  if (!typed_input || typed_input->value != input.value)
    return false;

  ioctl_test_output storage{};
  ntl::device_control::out_buffer out(&storage, sizeof(storage));
  const ioctl_test_output output{typed_input->value + 1};
  if (!ntl::ioctl_write_output<ntl_test_ioctl>(out, output) ||
      out.size != sizeof(output) || storage.value != 0x1235)
    return false;

  unsigned char short_storage[sizeof(ioctl_test_output) - 1]{};
  ntl::device_control::out_buffer short_out(short_storage,
                                            sizeof(short_storage));
  return !ntl::ioctl_write_output<ntl_test_ioctl>(short_out, output) &&
         short_out.size == 0;
}

struct ioctl_pipeline_dispatch {
  explicit ioctl_pipeline_dispatch(ntl::remove_lock &lock) noexcept
      : remove_lock(lock) {}

  ntl::remove_lock &remove_lock;
  std::uint32_t handled_count = 0;

  NTSTATUS handle(const ntl::device_control::code &code,
                  const ntl::device_control::in_buffer &in,
                  ntl::device_control::out_buffer &out,
                  void *irp_tag) noexcept {
    auto guard = remove_lock.acquire(irp_tag);
    if (!guard) {
      out.clear();
      return static_cast<NTSTATUS>(guard.status());
    }

    if (!ntl::is_ioctl<ntl_pipeline_ioctl>(code)) {
      out.clear();
      return STATUS_INVALID_DEVICE_REQUEST;
    }

    const auto *request = ntl::ioctl_input_as<ntl_pipeline_ioctl>(in);
    if (!request) {
      out.clear();
      return STATUS_INVALID_PARAMETER;
    }

    auto scratch_storage = ntl::try_make_pool_buffer(
        sizeof(ioctl_pipeline_output), ntl::pool_kind::nonpaged,
        ntl::pool_option::none, "NTPm");
    if (!scratch_storage) {
      out.clear();
      return static_cast<NTSTATUS>(scratch_storage.status());
    }

    auto scratch =
        ntl::mdl::allocate(scratch_storage->get(), sizeof(ioctl_pipeline_output));
    if (!scratch) {
      out.clear();
      return static_cast<NTSTATUS>(scratch.status());
    }

    scratch->build_for_nonpaged_pool();
    auto scratch_address = scratch->system_address();
    if (!scratch_address) {
      out.clear();
      return static_cast<NTSTATUS>(scratch_address.status());
    }

    auto *reply = reinterpret_cast<ioctl_pipeline_output *>(*scratch_address);
    reply->value = request->value + 1;
    reply->checksum = request->value ^ 0xa5a5a5a5u;

    if (!ntl::ioctl_write_output<ntl_pipeline_ioctl>(out, *reply)) {
      out.clear();
      return STATUS_BUFFER_TOO_SMALL;
    }

    ++handled_count;
    return STATUS_SUCCESS;
  }

  void begin_teardown() noexcept { remove_lock.release_and_wait(this); }
};

bool ntl_device_control_pipeline_test() {
  ioctl_pipeline_dispatch dispatch(g_remove_lock_test_storage.pipeline);

  const ioctl_pipeline_input input{0x12345678};
  ioctl_pipeline_output output{};
  ntl::device_control::in_buffer in(&input, sizeof(input));
  ntl::device_control::out_buffer out(&output, sizeof(output));

  auto status =
      dispatch.handle(ntl_pipeline_ioctl::control_code(), in, out,
                      reinterpret_cast<void *>(0x1000));
  if (status != STATUS_SUCCESS || out.size != sizeof(output) ||
      output.value != input.value + 1 ||
      output.checksum != (input.value ^ 0xa5a5a5a5u) ||
      dispatch.handled_count != 1)
    return false;

  output = {};
  ntl::device_control::out_buffer wrong_out(&output, sizeof(output));
  status = dispatch.handle(
      ntl::device_control::code{CTL_CODE(FILE_DEVICE_UNKNOWN, 0x813,
                                         METHOD_BUFFERED, FILE_READ_DATA)},
      in, wrong_out, reinterpret_cast<void *>(0x1001));
  if (status != STATUS_INVALID_DEVICE_REQUEST || wrong_out.size != 0 ||
      dispatch.handled_count != 1)
    return false;

  ntl::device_control::in_buffer short_in(&input, sizeof(input) - 1);
  ntl::device_control::out_buffer short_input_out(&output, sizeof(output));
  status = dispatch.handle(ntl_pipeline_ioctl::control_code(), short_in,
                           short_input_out,
                           reinterpret_cast<void *>(0x1002));
  if (status != STATUS_INVALID_PARAMETER || short_input_out.size != 0 ||
      dispatch.handled_count != 1)
    return false;

  unsigned char small_output[sizeof(ioctl_pipeline_output) - 1]{};
  ntl::device_control::out_buffer small_out(small_output,
                                            sizeof(small_output));
  status = dispatch.handle(ntl_pipeline_ioctl::control_code(), in,
                           small_out, reinterpret_cast<void *>(0x1003));
  if (status != STATUS_BUFFER_TOO_SMALL || small_out.size != 0 ||
      dispatch.handled_count != 1)
    return false;

  dispatch.begin_teardown();

  ntl::device_control::out_buffer teardown_out(&output, sizeof(output));
  status = dispatch.handle(ntl_pipeline_ioctl::control_code(), in,
                           teardown_out,
                           reinterpret_cast<void *>(0x1004));
  return status == STATUS_DELETE_PENDING && teardown_out.size == 0 &&
         dispatch.handled_count == 1;
}

bool ntl_mdl_test() {
  constexpr ULONG buffer_size = 64;
  auto buffer = ntl::try_make_pool_buffer(
      buffer_size, ntl::pool_kind::nonpaged, ntl::pool_option::none, "NTMd");
  if (!buffer)
    return false;

  auto *const bytes = static_cast<unsigned char *>(buffer->get());
  bytes[0] = 0x5a;

  auto native_mdl = ntl::mdl::allocate(bytes, buffer_size);
  if (!native_mdl || !*native_mdl || native_mdl->get() == nullptr)
    return false;
  if (native_mdl->byte_count() != buffer_size ||
      native_mdl->virtual_address() != bytes)
    return false;

  native_mdl->build_for_nonpaged_pool();
  auto system_address = native_mdl->system_address();
  if (!system_address || *system_address != bytes)
    return false;

  ntl::mdl moved(std::move(*native_mdl));
  if (*native_mdl || !moved || moved.byte_count() != buffer_size)
    return false;

  PMDL const released = moved.release();
  if (moved || released == nullptr)
    return false;
  IoFreeMdl(released);

  ntl::mdl empty;
  auto invalid_address = empty.system_address();
  return !invalid_address &&
         static_cast<NTSTATUS>(invalid_address.status()) ==
             STATUS_INVALID_PARAMETER;
}

bool ntl_remove_lock_test() {
  auto &lock = g_remove_lock_test_storage.standalone;

  auto first = lock.acquire(reinterpret_cast<void *>(1));
  if (!first || !*first)
    return false;

  ntl::remove_lock_guard moved(std::move(*first));
  if (*first || !moved)
    return false;

  auto second = lock.acquire(reinterpret_cast<void *>(2));
  if (!second || !*second)
    return false;
  second->reset();
  if (*second)
    return false;

  moved.reset();

  auto third = lock.acquire(reinterpret_cast<void *>(3));
  if (!third || !*third)
    return false;
  third->reset();

  lock.release_and_wait(nullptr);

  auto after_remove = lock.acquire(reinterpret_cast<void *>(4));
  return !after_remove &&
         static_cast<NTSTATUS>(after_remove.status()) ==
             STATUS_DELETE_PENDING;
}

bool ntl_device_interface_test() {
  ntl::device_interface_link link;
  if (link || !link.name().empty())
    return false;

  if (static_cast<NTSTATUS>(link.enable()) != STATUS_INVALID_PARAMETER)
    return false;
  if (static_cast<NTSTATUS>(link.disable()) != STATUS_INVALID_PARAMETER)
    return false;

  link.reset();
  return !link;
}

bool ntl_handle_object_test() {
  ntl::unique_handle handle;
  const auto create_status =
      ZwCreateEvent(handle.put(), EVENT_MODIFY_STATE | SYNCHRONIZE, nullptr,
                    NotificationEvent, FALSE);
  if (!NT_SUCCESS(create_status) || !handle)
    return false;

  auto event_ref = ntl::try_reference_object_by_handle<PKEVENT>(
      handle.get(), EVENT_MODIFY_STATE, *ExEventObjectType);
  if (!event_ref || !event_ref->get())
    return false;

  ntl::unique_object<PKEVENT> event_object(std::move(*event_ref));
  if (!event_object || KeReadStateEvent(event_object.get()) != 0)
    return false;

  KeSetEvent(event_object.get(), IO_NO_INCREMENT, FALSE);
  if (KeReadStateEvent(event_object.get()) == 0)
    return false;

  ntl::unique_object<PKEVENT> moved_object(std::move(event_object));
  if (event_object || !moved_object)
    return false;

  PKEVENT const released_object = moved_object.release();
  if (moved_object || !released_object)
    return false;
  ObDereferenceObject(released_object);

  ntl::unique_handle moved_handle(std::move(handle));
  if (handle || !moved_handle)
    return false;

  HANDLE const released_handle = moved_handle.release();
  if (moved_handle || !released_handle)
    return false;

  ntl::unique_handle adopted_handle(released_handle);
  if (!adopted_handle.close().is_ok() || adopted_handle)
    return false;

  return true;
}

bool ntl_registry_test() {
  const std::wstring key_path =
      L"\\Registry\\Machine\\Software\\CrtSysNtlRegistryTest";
  const std::wstring parameters_path = key_path + L"\\Parameters";
  delete_registry_key_if_present(parameters_path);
  delete_registry_key_if_present(key_path);

  ntl::registry_disposition disposition{};
  auto created =
      ntl::registry_key::create(key_path, KEY_READ | KEY_WRITE | DELETE,
                                REG_OPTION_VOLATILE, &disposition);
  if (!created ||
      disposition != ntl::registry_disposition::created_new_key)
    return false;

  ntl::registry_key key(std::move(*created));
  if (!key)
    return false;

  if (!key.set_dword(L"Flags", 0x12345678).is_ok())
    return false;
  auto flags = key.query_dword(L"Flags");
  if (!flags || *flags != 0x12345678)
    return false;

  if (!key.set_qword(L"Large", 0x1122334455667788ull).is_ok())
    return false;
  auto large = key.query_qword(L"Large");
  if (!large || *large != 0x1122334455667788ull)
    return false;

  if (!key.set_string(L"Name", L"crtsys").is_ok())
    return false;
  auto name = key.query_string(L"Name");
  if (!name || *name != L"crtsys")
    return false;

  if (!key.set_expand_string(L"Path", L"%SystemRoot%\\System32").is_ok())
    return false;
  auto path = key.query_string(L"Path");
  if (!path || *path != L"%SystemRoot%\\System32")
    return false;

  const std::vector<std::uint8_t> bytes{1, 2, 3, 4, 5};
  if (!key.set_binary(L"Bytes", bytes).is_ok())
    return false;
  auto queried_bytes = key.query_binary(L"Bytes");
  if (!queried_bytes || *queried_bytes != bytes)
    return false;

  auto raw_name = key.query_value(L"Name");
  if (!raw_name || raw_name->type != REG_SZ || raw_name->data.empty())
    return false;

  auto parameters = ntl::registry_key::create(parameters_path,
                                             KEY_READ | KEY_WRITE | DELETE,
                                             REG_OPTION_VOLATILE);
  if (!parameters)
    return false;
  if (!parameters->set_dword(L"Enabled", 1).is_ok())
    return false;
  if (!parameters->close().is_ok())
    return false;

  auto opened_parameters = ntl::try_open_driver_parameters(key_path);
  if (!opened_parameters)
    return false;
  auto enabled = opened_parameters->query_dword(L"Enabled");
  if (!enabled || *enabled != 1)
    return false;
  {
    auto config = ntl::driver_config::open(key_path, KEY_READ | KEY_WRITE);
    if (!config)
      return false;
    if (config->dword_or(L"Enabled", 0) != 1)
      return false;
    if (config->dword_or(L"MissingDword", 7) != 7)
      return false;
    if (!config->key().set_string(L"Mode", L"fast").is_ok())
      return false;
    if (config->string_or(L"Mode", L"slow") != L"fast")
      return false;
    if (config->string_or(L"MissingString", L"default") != L"default")
      return false;
  }
  if (!opened_parameters->delete_key().is_ok())
    return false;
  if (!opened_parameters->close().is_ok())
    return false;

  if (!key.delete_value(L"Flags").is_ok())
    return false;
  auto missing_value = key.query_dword(L"Flags");
  if (missing_value ||
      static_cast<NTSTATUS>(missing_value.status()) !=
          STATUS_OBJECT_NAME_NOT_FOUND)
    return false;

  ntl::registry_key moved(std::move(key));
  if (key || !moved)
    return false;

  HANDLE const released = moved.release();
  if (moved || !released)
    return false;

  ntl::registry_key adopted(released);
  if (!adopted.close().is_ok() || adopted)
    return false;

  auto opened = ntl::registry_key::open(key_path, KEY_READ | DELETE);
  if (!opened)
    return false;
  if (!opened->delete_key().is_ok())
    return false;
  if (!opened->close().is_ok())
    return false;

  auto missing_key = ntl::registry_key::open(key_path, KEY_READ);
  return !missing_key &&
         static_cast<NTSTATUS>(missing_key.status()) ==
             STATUS_OBJECT_NAME_NOT_FOUND;
}

bool ntl_symbolic_link_test() {
  const std::wstring link_name = L"\\DosDevices\\CrtSysNtlSymbolicLinkTest";
  const std::wstring target_name =
      L"\\Device\\CrtSysNtlSymbolicLinkTarget";

  if (ntl::dos_device_name(L"CrtSysNtlSymbolicLinkTest") != link_name ||
      ntl::device_target_name(L"CrtSysNtlSymbolicLinkTarget") != target_name)
    return false;

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

bool ntl_timer_dpc_test() {
  timer_dpc_test_context direct_context;
  ntl::kdpc direct_dpc(timer_dpc_test_routine, &direct_context);
  void *const direct_arg1 =
      reinterpret_cast<void *>(static_cast<ULONG_PTR>(0x1234));

  if (!direct_dpc.queue(direct_arg1, &direct_context))
    return false;

  auto timeout = ntl::relative_due_time_ms(1000);
  if (!direct_context.fired.wait(&timeout).is_ok())
    return false;
  if (direct_context.count.load() != 1 ||
      direct_context.system_argument1.load() != direct_arg1 ||
      direct_context.system_argument2.load() != &direct_context)
    return false;
  if (direct_context.observed_irql.load() != ntl::irql::dispatch)
    return false;

  ntl::timer synchronization_timer(ntl::timer_type::synchronization);
  if (synchronization_timer.set_once(ntl::relative_due_time_ms(10)))
    return false;

  timeout = ntl::relative_due_time_ms(1000);
  if (!synchronization_timer.wait(&timeout).is_ok())
    return false;
  if (synchronization_timer.signaled())
    return false;

  timer_dpc_test_context timer_context;
  ntl::kdpc timer_dpc(timer_dpc_test_routine, &timer_context);
  ntl::timer notification_timer;

  if (notification_timer.set_once(ntl::relative_due_time_ms(10), &timer_dpc))
    return false;

  timeout = ntl::relative_due_time_ms(1000);
  if (!timer_context.fired.wait(&timeout).is_ok())
    return false;
  if (timer_context.count.load() != 1 ||
      timer_context.observed_irql.load() != ntl::irql::dispatch)
    return false;

  ntl::timer cancel_timer;
  if (cancel_timer.set_periodic(ntl::relative_due_time_ms(1000), 50))
    return false;
  if (!cancel_timer.cancel())
    return false;

  LARGE_INTEGER zero_timeout{};
  if (static_cast<NTSTATUS>(cancel_timer.wait(&zero_timeout)) !=
      STATUS_TIMEOUT)
    return false;

  return !cancel_timer.signaled();
}

bool ntl_system_thread_test() {
  system_thread_test_context context;
  auto created = ntl::system_thread::create(system_thread_test_routine,
                                            &context);
  if (!created || !created->get())
    return false;

  ntl::system_thread thread(std::move(*created));
  auto timeout = ntl::relative_due_time_ms(5000);
  if (static_cast<NTSTATUS>(thread.join(&timeout)) != STATUS_SUCCESS)
    return false;
  if (thread || context.value.load() != 42 ||
      context.observed_irql.load() != ntl::irql::passive)
    return false;

  system_thread_test_context moved_context;
  auto moved_created = ntl::system_thread::create(system_thread_test_routine,
                                                  &moved_context);
  if (!moved_created || !moved_created->get())
    return false;

  ntl::system_thread moved_thread(std::move(*moved_created));
  if (!moved_thread)
    return false;

  HANDLE const released_handle = moved_thread.release();
  if (moved_thread || !released_handle)
    return false;

  ntl::system_thread adopted_thread(released_handle);
  timeout = ntl::relative_due_time_ms(5000);
  if (static_cast<NTSTATUS>(adopted_thread.join(&timeout)) != STATUS_SUCCESS)
    return false;

  return !adopted_thread && moved_context.value.load() == 42 &&
         moved_context.observed_irql.load() == ntl::irql::passive;
}

bool ntl_wait_helpers_test() {
  ntl::event notification;
  const auto first_probe = ntl::try_wait(notification);
  if (!ntl::wait_timed_out(first_probe))
    return false;

  notification.set();
  if (!ntl::wait_signaled(ntl::try_wait(notification)))
    return false;

  ntl::timer synchronization_timer(ntl::timer_type::synchronization);
  if (synchronization_timer.set_once(ntl::relative_timeout_ms(10)))
    return false;
  if (!ntl::wait_signaled(ntl::wait_for(synchronization_timer, 1000)))
    return false;
  if (ntl::wait_signaled(ntl::try_wait(synchronization_timer)))
    return false;

  system_thread_test_context thread_context;
  auto created = ntl::system_thread::create(system_thread_test_routine,
                                            &thread_context);
  if (!created || !created->get())
    return false;

  ntl::system_thread thread(std::move(*created));
  if (!ntl::wait_signaled(ntl::wait_for(thread, 5000)))
    return false;
  if (!thread.close().is_ok())
    return false;

  return thread_context.value.load() == 42 &&
         thread_context.observed_irql.load() == ntl::irql::passive;
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

bool ntl_passive_executor_test() {
  ntl::passive_executor executor{DelayedWorkQueue, "NTLe"};

  passive_executor_test_context inline_context;
  if (!executor.execute([&] {
        inline_context.observed_irql.store(ntl::current_irql());
        inline_context.value.store(11);
      }).is_ok())
    return false;
  if (inline_context.value.load() != 11 ||
      inline_context.observed_irql.load() != ntl::irql::passive)
    return false;

  passive_executor_test_context posted_context;
  if (!executor.post([state = &posted_context] {
        state->observed_irql.store(ntl::current_irql());
        state->value.store(12);
        state->completed.set();
      }).is_ok())
    return false;
  auto timeout = ntl::relative_due_time_ms(5000);
  if (!posted_context.completed.wait(&timeout).is_ok())
    return false;
  if (posted_context.value.load() != 12 ||
      posted_context.observed_irql.load() != ntl::irql::passive)
    return false;

  passive_executor_test_context deferred_context;
  {
    auto raised_irql = ntl::raise_irql_to_dpc_level();
    const auto status = executor.execute([state = &deferred_context] {
      state->observed_irql.store(ntl::current_irql());
      state->value.store(13);
      state->completed.set();
    });
    if (!status.is_ok())
      return false;
  }
  timeout = ntl::relative_due_time_ms(5000);
  if (!deferred_context.completed.wait(&timeout).is_ok())
    return false;
  if (deferred_context.value.load() != 13 ||
      deferred_context.observed_irql.load() != ntl::irql::passive)
    return false;

  dpc_passive_executor_test_context dpc_context;
  dpc_context.executor = &executor;
  ntl::kdpc dpc(dpc_to_passive_executor_test_routine, &dpc_context);
  if (!dpc.queue())
    return false;
  timeout = ntl::relative_due_time_ms(5000);
  if (!dpc_context.dpc_completed.wait(&timeout).is_ok())
    return false;
  if (static_cast<NTSTATUS>(dpc_context.post_status.load()) !=
      STATUS_SUCCESS)
    return false;
  timeout = ntl::relative_due_time_ms(5000);
  if (!dpc_context.passive_completed.wait(&timeout).is_ok())
    return false;
  if (dpc_context.dpc_irql.load() != ntl::irql::dispatch ||
      dpc_context.passive_irql.load() != ntl::irql::passive ||
      dpc_context.value.load() != 15)
    return false;

  std::atomic<long> owned_value{0};
  auto owned_item = executor.make_work_item([&] {
    if (ntl::current_irql() == ntl::irql::passive)
      owned_value.store(14);
  });
  if (!executor.queue_and_wait(owned_item).is_ok())
    return false;

  return owned_value.load() == 14;
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
  EXPECT_TRUE(ntl::is_passive_level());
  EXPECT_TRUE(ntl::is_irql_at_most(ntl::irql::passive));
  EXPECT_TRUE(ntl::require_passive_level().is_ok());
  EXPECT_TRUE(ntl::require_irql_at_most(ntl::irql::passive).is_ok());

  {
    auto raised_irql = ntl::raise_irql(ntl::irql::apc);
    EXPECT_EQ(old_irql, raised_irql.old());
    EXPECT_EQ(ntl::current_irql(), ntl::irql::apc);
    EXPECT_FALSE(ntl::is_passive_level());
    EXPECT_TRUE(ntl::is_irql_at_most(ntl::irql::apc));
    EXPECT_FALSE(ntl::is_irql_at_most(ntl::irql::passive));
    EXPECT_EQ(static_cast<NTSTATUS>(ntl::require_passive_level()),
              STATUS_INVALID_DEVICE_STATE);
    EXPECT_EQ(static_cast<NTSTATUS>(
                  ntl::require_irql_at_most(ntl::irql::passive)),
              STATUS_INVALID_DEVICE_STATE);
    EXPECT_TRUE(ntl::require_irql_at_most(ntl::irql::apc).is_ok());
  }
  EXPECT_EQ(ntl::current_irql(), old_irql);

  old_irql = ntl::current_irql();
  {
    auto raised_irql = ntl::raise_irql_to_dpc_level();
    EXPECT_EQ(old_irql, raised_irql.old());

    EXPECT_EQ(ntl::current_irql(), ntl::irql::dispatch);
    EXPECT_FALSE(ntl::is_irql_at_most(ntl::irql::apc));
    EXPECT_TRUE(ntl::is_irql_at_most(ntl::irql::dispatch));
    EXPECT_EQ(static_cast<NTSTATUS>(ntl::require_irql_at_most(ntl::irql::apc)),
              STATUS_INVALID_DEVICE_STATE);
    EXPECT_TRUE(ntl::require_irql_at_most(ntl::irql::dispatch).is_ok());
  }

  EXPECT_EQ(ntl::current_irql(), old_irql);
  EXPECT_TRUE(ntl::require_passive_level().is_ok());
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

TEST(ntl_test, ntl_irp_device_control_helpers_test) {
  EXPECT_TRUE(ntl_irp_device_control_helpers_test());
}

TEST(ntl_test, ntl_ioctl_test) {
  EXPECT_TRUE(ntl_ioctl_test());
}

TEST(ntl_test, ntl_mdl_test) {
  EXPECT_TRUE(ntl_mdl_test());
}

TEST(ntl_test, ntl_ipc_test) { EXPECT_TRUE(ntl_ipc_test_impl()); }

TEST(ntl_test, ntl_mapped_buffer_lifetime_test) {
  EXPECT_TRUE(ntl_mapped_buffer_lifetime_test_impl());
}

TEST(ntl_test, ntl_irp_io_buffer_semantics_test) {
  EXPECT_TRUE(ntl_irp_io_buffer_semantics_test_impl());
}

// The DriverEntry boolean-test harness is the single owner of
// ntl_device_control_pipeline_test and ntl_remove_lock_test. Both deliberately
// call remove_lock::release_and_wait(), which is a terminal operation. Registering
// either test here would execute the same image-lifetime remove lock a second
// time after teardown and turn a valid one-shot test into a false GTest failure.

TEST(ntl_test, ntl_device_interface_test) {
  EXPECT_TRUE(ntl_device_interface_test());
}

TEST(ntl_test, ntl_handle_object_test) {
  EXPECT_TRUE(ntl_handle_object_test());
}

TEST(ntl_test, ntl_registry_test) {
  EXPECT_TRUE(ntl_registry_test());
}

TEST(ntl_test, ntl_symbolic_link_test) {
  EXPECT_TRUE(ntl_symbolic_link_test());
}

TEST(ntl_test, ntl_event_test) {
  EXPECT_TRUE(ntl_event_test());
}

TEST(ntl_test, ntl_timer_dpc_test) {
  EXPECT_TRUE(ntl_timer_dpc_test());
}

TEST(ntl_test, ntl_system_thread_test) {
  EXPECT_TRUE(ntl_system_thread_test());
}

TEST(ntl_test, ntl_wait_helpers_test) {
  EXPECT_TRUE(ntl_wait_helpers_test());
}

TEST(ntl_test, ntl_work_item_test) {
  EXPECT_TRUE(ntl_work_item_test());
}

TEST(ntl_test, ntl_passive_executor_test) {
  EXPECT_TRUE(ntl_passive_executor_test());
}
