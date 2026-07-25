#include <ntl/flt/rpc>

#include <cstdint>

namespace {

NTL_FLT_RPC_BEGIN_CONTRACT(
    ntl_flt_rpc_self_contained_contract,
    L"\\ntl-flt-rpc-self-contained",
    1,
    0)

NTL_FLT_ADD_METHOD_ID(
    ntl_flt_rpc_self_contained_contract,
    0x800,
    std::uint32_t(std::uint32_t),
    echo,
    [](std::uint32_t value) noexcept { return value; })

NTL_FLT_RPC_END(ntl_flt_rpc_self_contained_contract)

} // namespace
