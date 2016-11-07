// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/nqe/network_quality_estimator_test_util.h"

#include "base/files/file_path.h"
#include "base/run_loop.h"
#include "net/base/load_flags.h"
#include "net/test/embedded_test_server/http_response.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_test_util.h"

namespace net {

TestNetworkQualityEstimator::TestNetworkQualityEstimator(
    const std::map<std::string, std::string>& variation_params,
    std::unique_ptr<net::ExternalEstimateProvider> external_estimate_provider)
    : TestNetworkQualityEstimator(std::move(external_estimate_provider),
                                  variation_params,
                                  true,
                                  true) {}

TestNetworkQualityEstimator::TestNetworkQualityEstimator(
    std::unique_ptr<net::ExternalEstimateProvider> external_estimate_provider,
    const std::map<std::string, std::string>& variation_params,
    bool allow_local_host_requests_for_tests,
    bool allow_smaller_responses_for_tests)
    : NetworkQualityEstimator(std::move(external_estimate_provider),
                              variation_params,
                              allow_local_host_requests_for_tests,
                              allow_smaller_responses_for_tests),
      current_network_type_(NetworkChangeNotifier::CONNECTION_UNKNOWN),
      accuracy_recording_intervals_set_(false),
      rand_double_(0.0) {
  // Set up the embedded test server.
  embedded_test_server_.ServeFilesFromDirectory(
      base::FilePath(FILE_PATH_LITERAL("net/data/url_request_unittest")));
  embedded_test_server_.RegisterRequestHandler(base::Bind(
      &TestNetworkQualityEstimator::HandleRequest, base::Unretained(this)));
  EXPECT_TRUE(embedded_test_server_.Start());
}

TestNetworkQualityEstimator::TestNetworkQualityEstimator(
    const std::map<std::string, std::string>& variation_params)
    : TestNetworkQualityEstimator(variation_params,
                                  std::unique_ptr<ExternalEstimateProvider>()) {
}

TestNetworkQualityEstimator::~TestNetworkQualityEstimator() {}

void TestNetworkQualityEstimator::RunOneRequest() {
  TestDelegate test_delegate;
  TestURLRequestContext context(true);
  context.set_network_quality_estimator(this);
  context.Init();
  std::unique_ptr<URLRequest> request(
      context.CreateRequest(GetEchoURL(), DEFAULT_PRIORITY, &test_delegate));
  request->SetLoadFlags(request->load_flags() | LOAD_MAIN_FRAME_DEPRECATED);
  request->Start();
  base::RunLoop().Run();
}

void TestNetworkQualityEstimator::SimulateNetworkChange(
    NetworkChangeNotifier::ConnectionType new_connection_type,
    const std::string& network_id) {
  current_network_type_ = new_connection_type;
  current_network_id_ = network_id;
  OnConnectionTypeChanged(new_connection_type);
}

std::unique_ptr<test_server::HttpResponse>
TestNetworkQualityEstimator::HandleRequest(
    const test_server::HttpRequest& request) {
  std::unique_ptr<test_server::BasicHttpResponse> http_response(
      new test_server::BasicHttpResponse());
  http_response->set_code(HTTP_OK);
  http_response->set_content("hello");
  http_response->set_content_type("text/plain");
  return std::move(http_response);
}

const GURL TestNetworkQualityEstimator::GetEchoURL() const {
  return embedded_test_server_.GetURL("/echo.html");
}

EffectiveConnectionType
TestNetworkQualityEstimator::GetEffectiveConnectionType() const {
  if (effective_connection_type_)
    return effective_connection_type_.value();
  return NetworkQualityEstimator::GetEffectiveConnectionType();
}

EffectiveConnectionType
TestNetworkQualityEstimator::GetRecentEffectiveConnectionType(
    const base::TimeTicks& start_time) const {
  if (recent_effective_connection_type_)
    return recent_effective_connection_type_.value();
  return NetworkQualityEstimator::GetRecentEffectiveConnectionType(start_time);
}

EffectiveConnectionType
TestNetworkQualityEstimator::GetRecentEffectiveConnectionTypeAndNetworkQuality(
    const base::TimeTicks& start_time,
    base::TimeDelta* http_rtt,
    base::TimeDelta* transport_rtt,
    int32_t* downstream_throughput_kbps) const {
  if (recent_effective_connection_type_) {
    GetRecentHttpRTT(start_time, http_rtt);
    GetRecentTransportRTT(start_time, transport_rtt);
    GetRecentDownlinkThroughputKbps(start_time, downstream_throughput_kbps);
    return recent_effective_connection_type_.value();
  }
  return NetworkQualityEstimator::
      GetRecentEffectiveConnectionTypeAndNetworkQuality(
          start_time, http_rtt, transport_rtt, downstream_throughput_kbps);
}

bool TestNetworkQualityEstimator::GetRecentHttpRTT(
    const base::TimeTicks& start_time,
    base::TimeDelta* rtt) const {
  if (start_time.is_null()) {
    if (start_time_null_http_rtt_) {
      *rtt = start_time_null_http_rtt_.value();
      return true;
    }
    return NetworkQualityEstimator::GetRecentHttpRTT(start_time, rtt);
  }
  if (recent_http_rtt_) {
    *rtt = recent_http_rtt_.value();
    return true;
  }
  return NetworkQualityEstimator::GetRecentHttpRTT(start_time, rtt);
}

bool TestNetworkQualityEstimator::GetRecentTransportRTT(
    const base::TimeTicks& start_time,
    base::TimeDelta* rtt) const {
  if (start_time.is_null()) {
    if (start_time_null_transport_rtt_) {
      *rtt = start_time_null_transport_rtt_.value();
      return true;
    }
    return NetworkQualityEstimator::GetRecentTransportRTT(start_time, rtt);
  }

  if (recent_transport_rtt_) {
    *rtt = recent_transport_rtt_.value();
    return true;
  }
  return NetworkQualityEstimator::GetRecentTransportRTT(start_time, rtt);
}

bool TestNetworkQualityEstimator::GetRecentDownlinkThroughputKbps(
    const base::TimeTicks& start_time,
    int32_t* kbps) const {
  if (start_time.is_null()) {
    if (start_time_null_downlink_throughput_kbps_) {
      *kbps = start_time_null_downlink_throughput_kbps_.value();
      return true;
    }
    return NetworkQualityEstimator::GetRecentDownlinkThroughputKbps(start_time,
                                                                    kbps);
  }

  if (recent_downlink_throughput_kbps_) {
    *kbps = recent_downlink_throughput_kbps_.value();
    return true;
  }
  return NetworkQualityEstimator::GetRecentDownlinkThroughputKbps(start_time,
                                                                  kbps);
}

void TestNetworkQualityEstimator::SetAccuracyRecordingIntervals(
    const std::vector<base::TimeDelta>& accuracy_recording_intervals) {
  accuracy_recording_intervals_set_ = true;
  accuracy_recording_intervals_ = accuracy_recording_intervals;
}

const std::vector<base::TimeDelta>&
TestNetworkQualityEstimator::GetAccuracyRecordingIntervals() const {
  if (accuracy_recording_intervals_set_)
    return accuracy_recording_intervals_;

  return NetworkQualityEstimator::GetAccuracyRecordingIntervals();
}

double TestNetworkQualityEstimator::RandDouble() const {
  return rand_double_;
}

nqe::internal::NetworkID TestNetworkQualityEstimator::GetCurrentNetworkID()
    const {
  return nqe::internal::NetworkID(current_network_type_, current_network_id_);
}

}  // namespace net
