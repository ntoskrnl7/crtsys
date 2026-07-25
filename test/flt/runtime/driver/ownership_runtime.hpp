#pragma once

#include <ntl/flt/callback>
#include <ntl/flt/communication>
#include <ntl/flt/registration>
#include <ntl/flt/types>

namespace crtsys_flt_runtime_test {

void configure_ownership_runtime_messages(
    ntl::flt::communication_server &messages);

void configure_ownership_runtime_registration(
    ntl::flt::registration &callbacks);

ntl::status
prepare_ownership_instance(ntl::flt::related_objects objects) noexcept;

void observe_transaction_create(ntl::flt::create_callback_data data,
                                ntl::flt::related_objects objects) noexcept;

void observe_data_scan_create(ntl::flt::create_callback_data data,
                              ntl::flt::related_objects objects) noexcept;

void observe_self_issued_io_create(ntl::flt::create_callback_data data,
                                   ntl::flt::related_objects objects) noexcept;

void close_ownership_runtime() noexcept;

} // namespace crtsys_flt_runtime_test
