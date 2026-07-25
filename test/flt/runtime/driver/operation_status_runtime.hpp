#pragma once

#include <ntl/flt/communication>
#include <ntl/flt/registration>

namespace crtsys_flt_runtime_test {

void configure_operation_status_runtime_messages(
    ntl::flt::communication_server &messages);

void configure_operation_status_runtime_registration(
    ntl::flt::registration &callbacks);

} // namespace crtsys_flt_runtime_test
