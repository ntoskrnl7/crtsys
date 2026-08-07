#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <ntl/wfp/all>

#include "bind_redirect_contract.hpp"

namespace {

struct controller_options {
  std::filesystem::path application;
  std::filesystem::path ready_file;
  std::filesystem::path stop_file;
  std::uint32_t duration_ms = 30'000;
};

unsigned long parse_unsigned(std::wstring_view value) {
  if (value.empty())
    throw std::invalid_argument("missing numeric controller argument");
  std::size_t consumed = 0;
  const unsigned long result =
      std::stoul(std::wstring(value), &consumed, 10);
  if (consumed != value.size())
    throw std::invalid_argument("invalid numeric controller argument");
  return result;
}

controller_options parse_options(int argc, wchar_t **argv) {
  controller_options result;
  for (int index = 1; index < argc; ++index) {
    const std::wstring_view name(argv[index]);
    if (index + 1 >= argc)
      throw std::invalid_argument("controller option is missing its value");
    const std::wstring_view value(argv[++index]);
    if (name == L"--application") {
      result.application = value;
    } else if (name == L"--ready-file") {
      result.ready_file = value;
    } else if (name == L"--stop-file") {
      result.stop_file = value;
    } else if (name == L"--duration-ms") {
      result.duration_ms =
          static_cast<std::uint32_t>(parse_unsigned(value));
      if (result.duration_ms < 100 || result.duration_ms > 300'000)
        throw std::invalid_argument(
            "--duration-ms must be between 100 and 300000");
    } else {
      throw std::invalid_argument("unknown bind-redirect controller option");
    }
  }
  if (result.application.empty() || result.ready_file.empty() ||
      result.stop_file.empty())
    throw std::invalid_argument(
        "required: --application, --ready-file, --stop-file");
  result.application = std::filesystem::canonical(result.application);
  if (!std::filesystem::is_regular_file(result.application))
    throw std::invalid_argument("--application is not a regular file");
  return result;
}

void write_control_file(const std::filesystem::path &path,
                        std::string_view text) {
  if (!path.parent_path().empty())
    std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output)
    throw std::runtime_error("cannot create bind-redirect control file");
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  output.close();
  if (!output)
    throw std::runtime_error("cannot flush bind-redirect control file");
}

void wait_for_stop(const controller_options &options) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(options.duration_ms);
  while (!std::filesystem::exists(options.stop_file)) {
    if (std::chrono::steady_clock::now() >= deadline)
      throw std::runtime_error(
          "bind-redirect controller timed out waiting for stop");
    ::Sleep(20);
  }
}

void install_policy(ntl::wfp::policy_session &session,
                    const ntl::wfp::application_id &application) {
  session.install([&](ntl::wfp::policy_transaction &transaction) {
    const auto provider = transaction.add_provider(
        {wfp_bind_redirect::provider_key,
         L"crtsys NTL WFP bind-redirect provider",
         L"Dynamic dual-stack UDP bind policy"});
    const auto sublayer = transaction.add_sublayer(
        provider,
        {wfp_bind_redirect::sublayer_key,
         L"crtsys NTL WFP bind-redirect sublayer",
         L"Application-scoped bind redirection", 0x7810});

    const auto callout_v4 =
        transaction.add_callout<wfp_bind_redirect::layer_v4>(
            provider,
            {wfp_bind_redirect::callout_key_v4,
             L"Redirect selected IPv4 UDP binds",
             L"Typed ALE_BIND_REDIRECT_V4 callout"});
    ntl::wfp::bind_redirect_filter_builder<wfp_bind_redirect::layer_v4>
        filter_v4(wfp_bind_redirect::filter_key_v4,
                  L"Redirect selected application IPv4 UDP binds",
                  wfp_bind_redirect::selector_v4,
                  ntl::wfp::callout_unavailable::block);
    filter_v4.application_equal(application).protocol_equal(IPPROTO_UDP);
    transaction.add_bind_redirect_filter(sublayer, callout_v4, filter_v4);

    const auto callout_v6 =
        transaction.add_callout<wfp_bind_redirect::layer_v6>(
            provider,
            {wfp_bind_redirect::callout_key_v6,
             L"Redirect selected IPv6 UDP binds",
             L"Typed ALE_BIND_REDIRECT_V6 callout"});
    ntl::wfp::bind_redirect_filter_builder<wfp_bind_redirect::layer_v6>
        filter_v6(wfp_bind_redirect::filter_key_v6,
                  L"Redirect selected application IPv6 UDP binds",
                  wfp_bind_redirect::selector_v6,
                  ntl::wfp::callout_unavailable::block);
    filter_v6.application_equal(application).protocol_equal(IPPROTO_UDP);
    transaction.add_bind_redirect_filter(sublayer, callout_v6, filter_v6);
  });
}

} // namespace

int wmain(int argc, wchar_t **argv) {
  try {
    const auto options = parse_options(argc, argv);
    const auto application =
        ntl::wfp::application_id::from_path(options.application.wstring());
    auto policy = ntl::wfp::policy_session::ephemeral(
        L"crtsys ntl::wfp bind-redirect controller");
    install_policy(policy, application);
    write_control_file(options.ready_file, "ready\n");
    wait_for_stop(options);
    std::wcout << L"NTL WFP bind-redirect controller stopped: application="
               << options.application.wstring()
               << L", policy removed on exit.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NTL WFP bind-redirect controller failed: "
              << error.what() << '\n';
    return 1;
  }
}
