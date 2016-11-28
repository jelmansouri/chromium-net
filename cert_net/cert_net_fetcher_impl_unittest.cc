// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert_net/cert_net_fetcher_impl.h"

#include <string>
#include <utility>

#include "base/compiler_specific.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/synchronization/lock.h"
#include "net/cert/cert_net_fetcher.h"
#include "net/cert/ct_policy_enforcer.h"
#include "net/cert/mock_cert_verifier.h"
#include "net/cert/multi_log_ct_verifier.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_server_properties_impl.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/gtest_util.h"
#include "net/url_request/url_request_job_factory_impl.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "testing/platform_test.h"

using net::test::IsOk;

// TODO(eroman): Test that cookies aren't sent.

namespace net {

namespace {

const base::FilePath::CharType kDocRoot[] =
    FILE_PATH_LITERAL("net/data/cert_net_fetcher_impl_unittest");

// A non-mock URLRequestContext which can access http:// urls.
class RequestContext : public URLRequestContext {
 public:
  RequestContext() : storage_(this) {
    ProxyConfig no_proxy;
    storage_.set_host_resolver(
        std::unique_ptr<HostResolver>(new MockHostResolver));
    storage_.set_cert_verifier(base::WrapUnique(new MockCertVerifier));
    storage_.set_transport_security_state(
        base::WrapUnique(new TransportSecurityState));
    storage_.set_cert_transparency_verifier(
        base::WrapUnique(new MultiLogCTVerifier));
    storage_.set_ct_policy_enforcer(base::WrapUnique(new CTPolicyEnforcer));
    storage_.set_proxy_service(ProxyService::CreateFixed(no_proxy));
    storage_.set_ssl_config_service(new SSLConfigServiceDefaults);
    storage_.set_http_server_properties(
        std::unique_ptr<HttpServerProperties>(new HttpServerPropertiesImpl()));

    HttpNetworkSession::Params params;
    params.host_resolver = host_resolver();
    params.cert_verifier = cert_verifier();
    params.transport_security_state = transport_security_state();
    params.cert_transparency_verifier = cert_transparency_verifier();
    params.ct_policy_enforcer = ct_policy_enforcer();
    params.proxy_service = proxy_service();
    params.ssl_config_service = ssl_config_service();
    params.http_server_properties = http_server_properties();
    storage_.set_http_network_session(
        base::MakeUnique<HttpNetworkSession>(params));
    storage_.set_http_transaction_factory(base::MakeUnique<HttpCache>(
        storage_.http_network_session(), HttpCache::DefaultBackend::InMemory(0),
        false /* set_up_quic_server_info */));
    storage_.set_job_factory(base::MakeUnique<URLRequestJobFactoryImpl>());
  }

  ~RequestContext() override { AssertNoURLRequests(); }

 private:
  URLRequestContextStorage storage_;
};

// Wait for the request to complete, and verify that it completed successfully
// with the indicated bytes.
void VerifySuccess(const std::string& expected_body,
                   CertNetFetcher::Request* request) {
  Error actual_error;
  std::vector<uint8_t> actual_body;
  request->WaitForResult(&actual_error, &actual_body);

  EXPECT_THAT(actual_error, IsOk());
  EXPECT_EQ(expected_body, std::string(actual_body.begin(), actual_body.end()));
}

// Wait for the request to complete, and verify that it completed with the
// indicated failure.
void VerifyFailure(Error expected_error, CertNetFetcher::Request* request) {
  Error actual_error;
  std::vector<uint8_t> actual_body;
  request->WaitForResult(&actual_error, &actual_body);

  EXPECT_EQ(expected_error, actual_error);
  EXPECT_EQ(0u, actual_body.size());
}

struct NetworkThreadState {
  TestNetworkDelegate network_delegate;
  RequestContext context;
};

class CertNetFetcherImplTest : public PlatformTest {
 public:
  CertNetFetcherImplTest() {
    test_server_.AddDefaultHandlers(base::FilePath(kDocRoot));
    StartNetworkThread();
  }

  ~CertNetFetcherImplTest() override {
    network_thread_->task_runner()->PostTask(
        FROM_HERE, base::Bind(&CertNetFetcherImplTest::TeardownOnNetworkThread,
                              base::Unretained(this)));
    network_thread_->Stop();
  }

 protected:
  std::unique_ptr<CertNetFetcher> CreateFetcher() {
    scoped_refptr<TrivialURLRequestContextGetter> context_getter(
        new TrivialURLRequestContextGetter(&state_->context,
                                           network_thread_->task_runner()));
    return CreateCertNetFetcher(context_getter.get());
  }

  int NumCreatedRequests() {
    int count = 0;
    base::WaitableEvent done(base::WaitableEvent::ResetPolicy::MANUAL,
                             base::WaitableEvent::InitialState::NOT_SIGNALED);
    network_thread_->task_runner()->PostTask(
        FROM_HERE, base::Bind(&CertNetFetcherImplTest::CountCreatedRequests,
                              base::Unretained(this), &count, &done));
    done.Wait();
    return count;
  }

  void StartNetworkThread() {
    // Start the network thread.
    network_thread_.reset(new base::Thread("network thread"));
    base::Thread::Options options(base::MessageLoop::TYPE_IO, 0);
    EXPECT_TRUE(network_thread_->StartWithOptions(options));

    // Initialize the URLRequestContext (and wait till it has completed).
    base::WaitableEvent done(base::WaitableEvent::ResetPolicy::MANUAL,
                             base::WaitableEvent::InitialState::NOT_SIGNALED);
    network_thread_->task_runner()->PostTask(
        FROM_HERE, base::Bind(&CertNetFetcherImplTest::InitOnNetworkThread,
                              base::Unretained(this), &done));
    done.Wait();
  }

  void InitOnNetworkThread(base::WaitableEvent* done) {
    state_.reset(new NetworkThreadState);
    state_->context.set_network_delegate(&state_->network_delegate);
    done->Signal();
  }

  void TeardownOnNetworkThread() { state_.reset(); }

  void CountCreatedRequests(int* count, base::WaitableEvent* done) {
    *count = state_->network_delegate.created_requests();
    done->Signal();
  }

  EmbeddedTestServer test_server_;
  std::unique_ptr<base::Thread> network_thread_;

  std::unique_ptr<NetworkThreadState> state_;
};

// Helper to start an AIA fetch using default parameters.
WARN_UNUSED_RESULT std::unique_ptr<CertNetFetcher::Request> StartRequest(
    CertNetFetcher* fetcher,
    const GURL& url) {
  return fetcher->FetchCaIssuers(url, CertNetFetcher::DEFAULT,
                                 CertNetFetcher::DEFAULT);
}

// Flaky on Android. See http://crbug.com/646147.
#if defined(OS_ANDROID)
#define MAYBE_ParallelFetchNoDuplicates DISABLED_ParallelFetchNoDuplicates
#else
#define MAYBE_ParallelFetchNoDuplicates ParallelFetchNoDuplicates
#endif
// Fetch a few unique URLs using GET in parallel. Each URL has a different body
// and Content-Type.
TEST_F(CertNetFetcherImplTest, MAYBE_ParallelFetchNoDuplicates) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  // Request a URL with Content-Type "application/pkix-cert"
  GURL url1 = test_server_.GetURL("/cert.crt");
  std::unique_ptr<CertNetFetcher::Request> request1 =
      StartRequest(fetcher.get(), url1);

  // Request a URL with Content-Type "application/pkix-crl"
  GURL url2 = test_server_.GetURL("/root.crl");
  std::unique_ptr<CertNetFetcher::Request> request2 =
      StartRequest(fetcher.get(), url2);

  // Request a URL with Content-Type "application/pkcs7-mime"
  GURL url3 = test_server_.GetURL("/certs.p7c");
  std::unique_ptr<CertNetFetcher::Request> request3 =
      StartRequest(fetcher.get(), url3);

  // Wait for all of the requests to complete and verify the fetch results.
  VerifySuccess("-cert.crt-\n", request1.get());
  VerifySuccess("-root.crl-\n", request2.get());
  VerifySuccess("-certs.p7c-\n", request3.get());

  EXPECT_EQ(3, NumCreatedRequests());
}

// Fetch a caIssuers URL which has an unexpected extension and Content-Type.
// The extension is .txt and the Content-Type is text/plain. Despite being
// unusual this succeeds as the extension and Content-Type are not required to
// be meaningful.
TEST_F(CertNetFetcherImplTest, ContentTypeDoesntMatter) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  GURL url = test_server_.GetURL("/foo.txt");
  std::unique_ptr<CertNetFetcher::Request> request =
      StartRequest(fetcher.get(), url);
  VerifySuccess("-foo.txt-\n", request.get());
}

// Fetch a URLs whose HTTP response code is not 200. These are considered
// failures.
TEST_F(CertNetFetcherImplTest, HttpStatusCode) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  // Response was HTTP status 404.
  {
    GURL url = test_server_.GetURL("/404.html");
    std::unique_ptr<CertNetFetcher::Request> request =
        StartRequest(fetcher.get(), url);
    VerifyFailure(ERR_FAILED, request.get());
  }

  // Response was HTTP status 500.
  {
    GURL url = test_server_.GetURL("/500.html");
    std::unique_ptr<CertNetFetcher::Request> request =
        StartRequest(fetcher.get(), url);
    VerifyFailure(ERR_FAILED, request.get());
  }
}

// Fetching a URL with a Content-Disposition header should have no effect.
TEST_F(CertNetFetcherImplTest, ContentDisposition) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  GURL url = test_server_.GetURL("/downloadable.js");
  std::unique_ptr<CertNetFetcher::Request> request =
      StartRequest(fetcher.get(), url);
  VerifySuccess("-downloadable.js-\n", request.get());
}

// Verifies that a cachable request will be served from the HTTP cache the
// second time it is requested.
TEST_F(CertNetFetcherImplTest, Cache) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  // Fetch a URL whose HTTP headers make it cacheable for 1 hour.
  GURL url(test_server_.GetURL("/cacheable_1hr.crt"));
  {
    std::unique_ptr<CertNetFetcher::Request> request =
        StartRequest(fetcher.get(), url);
    VerifySuccess("-cacheable_1hr.crt-\n", request.get());
  }

  EXPECT_EQ(1, NumCreatedRequests());

  // Kill the HTTP server.
  ASSERT_TRUE(test_server_.ShutdownAndWaitUntilComplete());

  // Fetch again -- will fail unless served from cache.
  {
    std::unique_ptr<CertNetFetcher::Request> request =
        StartRequest(fetcher.get(), url);
    VerifySuccess("-cacheable_1hr.crt-\n", request.get());
  }

  EXPECT_EQ(2, NumCreatedRequests());
}

// Verify that the maximum response body constraints are enforced by fetching a
// resource that is larger than the limit.
TEST_F(CertNetFetcherImplTest, TooLarge) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  // This file has a response body 12 bytes long. So setting the maximum to 11
  // bytes will cause it to fail.
  GURL url(test_server_.GetURL("/certs.p7c"));
  std::unique_ptr<CertNetFetcher::Request> request =
      fetcher->FetchCaIssuers(url, CertNetFetcher::DEFAULT, 11);

  VerifyFailure(ERR_FILE_TOO_BIG, request.get());
}

// Set the timeout to 10 milliseconds, and try fetching a URL that takes 5
// seconds to complete. It should fail due to a timeout.
TEST_F(CertNetFetcherImplTest, Hang) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  GURL url(test_server_.GetURL("/slow/certs.p7c?5"));
  std::unique_ptr<CertNetFetcher::Request> request =
      fetcher->FetchCaIssuers(url, 10, CertNetFetcher::DEFAULT);
  VerifyFailure(ERR_TIMED_OUT, request.get());
}

// Verify that if a response is gzip-encoded it gets inflated before being
// returned to the caller.
TEST_F(CertNetFetcherImplTest, Gzip) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  GURL url(test_server_.GetURL("/gzipped_crl"));
  std::unique_ptr<CertNetFetcher::Request> request =
      StartRequest(fetcher.get(), url);
  VerifySuccess("-gzipped_crl-\n", request.get());
}

// Try fetching an unsupported URL scheme (https).
TEST_F(CertNetFetcherImplTest, HttpsNotAllowed) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  GURL url("https://foopy/foo.crt");
  std::unique_ptr<CertNetFetcher::Request> request =
      StartRequest(fetcher.get(), url);
  VerifyFailure(ERR_DISALLOWED_URL_SCHEME, request.get());

  // No request was created because the URL scheme was unsupported.
  EXPECT_EQ(0, NumCreatedRequests());
}

// Try fetching a URL which redirects to https.
TEST_F(CertNetFetcherImplTest, RedirectToHttpsNotAllowed) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  GURL url(test_server_.GetURL("/redirect_https"));

  std::unique_ptr<CertNetFetcher::Request> request =
      StartRequest(fetcher.get(), url);
  VerifyFailure(ERR_DISALLOWED_URL_SCHEME, request.get());

  EXPECT_EQ(1, NumCreatedRequests());
}

// Try fetching an unsupported URL scheme (https) and then immediately
// cancelling. This is a bit special because this codepath needs to post a task.
TEST_F(CertNetFetcherImplTest, CancelHttpsNotAllowed) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  GURL url("https://foopy/foo.crt");
  std::unique_ptr<CertNetFetcher::Request> request =
      StartRequest(fetcher.get(), url);

  // Cancel the request (May or may not have started yet, as the request is
  // running on another thread).
  request.reset();
}

// Flaky on Android. See http://crbug.com/646147.
#if defined(OS_ANDROID)
#define MAYBE_CancelBeforeRunningMessageLoop \
  DISABLED_CancelBeforeRunningMessageLoop
#else
#define MAYBE_CancelBeforeRunningMessageLoop CancelBeforeRunningMessageLoop
#endif
// Start a few requests, and cancel one of them before running the message loop
// again.
TEST_F(CertNetFetcherImplTest, MAYBE_CancelBeforeRunningMessageLoop) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  GURL url1 = test_server_.GetURL("/cert.crt");
  std::unique_ptr<CertNetFetcher::Request> request1 =
      StartRequest(fetcher.get(), url1);

  GURL url2 = test_server_.GetURL("/root.crl");
  std::unique_ptr<CertNetFetcher::Request> request2 =
      StartRequest(fetcher.get(), url2);

  GURL url3 = test_server_.GetURL("/certs.p7c");

  std::unique_ptr<CertNetFetcher::Request> request3 =
      StartRequest(fetcher.get(), url3);

  // Cancel the second request.
  request2.reset();

  // Wait for the non-cancelled requests to complete, and verify the fetch
  // results.
  VerifySuccess("-cert.crt-\n", request1.get());
  VerifySuccess("-certs.p7c-\n", request3.get());
}

// Start several requests, and cancel one of them after the first has completed.
// NOTE: The python test server is single threaded and can only service one
// request at a time. After a socket is opened by the server it waits for it to
// be completed, and any subsequent request will hang until the first socket is
// closed.
// Cancelling the first request can therefore be problematic, since if
// cancellation is done after the socket is opened but before reading/writing,
// then the socket is re-cycled and things will be stalled until the cleanup
// timer (10 seconds) closes it.
// To work around this, the last request is cancelled, and hope that the
// requests are given opened sockets in a FIFO order.
// TODO(eroman): Make this more robust.
// TODO(eroman): Rename this test.
TEST_F(CertNetFetcherImplTest, CancelAfterRunningMessageLoop) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  GURL url1 = test_server_.GetURL("/cert.crt");

  std::unique_ptr<CertNetFetcher::Request> request1 =
      StartRequest(fetcher.get(), url1);

  GURL url2 = test_server_.GetURL("/certs.p7c");
  std::unique_ptr<CertNetFetcher::Request> request2 =
      StartRequest(fetcher.get(), url2);

  GURL url3("ftp://www.not.supported.com/foo");
  std::unique_ptr<CertNetFetcher::Request> request3 =
      StartRequest(fetcher.get(), url3);

  // Wait for the ftp request to complete (it should complete right away since
  // it doesn't even try to connect to the server).
  VerifyFailure(ERR_DISALLOWED_URL_SCHEME, request3.get());

  // Cancel the second outstanding request.
  request2.reset();

  // Wait for the first request to complete and verify the fetch result.
  VerifySuccess("-cert.crt-\n", request1.get());
}

// Flaky on Android. See http://crbug.com/646147.
#if defined(OS_ANDROID)
#define MAYBE_ParallelFetchDuplicates DISABLED_ParallelFetchDuplicates
#else
#define MAYBE_ParallelFetchDuplicates ParallelFetchDuplicates
#endif
// Fetch the same URLs in parallel and verify that only 1 request is made per
// URL.
TEST_F(CertNetFetcherImplTest, MAYBE_ParallelFetchDuplicates) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  GURL url1 = test_server_.GetURL("/cert.crt");
  GURL url2 = test_server_.GetURL("/root.crl");

  // Issue 3 requests for url1, and 3 requests for url2
  std::unique_ptr<CertNetFetcher::Request> request1 =
      StartRequest(fetcher.get(), url1);

  std::unique_ptr<CertNetFetcher::Request> request2 =
      StartRequest(fetcher.get(), url2);

  std::unique_ptr<CertNetFetcher::Request> request3 =
      StartRequest(fetcher.get(), url1);

  std::unique_ptr<CertNetFetcher::Request> request4 =
      StartRequest(fetcher.get(), url2);

  std::unique_ptr<CertNetFetcher::Request> request5 =
      StartRequest(fetcher.get(), url2);

  std::unique_ptr<CertNetFetcher::Request> request6 =
      StartRequest(fetcher.get(), url1);

  // Cancel all but one of the requests for url1.
  request1.reset();
  request3.reset();

  // Wait for the remaining requests to finish and verify the fetch results.
  VerifySuccess("-root.crl-\n", request2.get());
  VerifySuccess("-root.crl-\n", request4.get());
  VerifySuccess("-root.crl-\n", request5.get());
  VerifySuccess("-cert.crt-\n", request6.get());

  // Verify that only 2 URLRequests were started even though 6 requests were
  // issued.
  EXPECT_EQ(2, NumCreatedRequests());
}

// Flaky on Android. See http://crbug.com/646147.
#if defined(OS_ANDROID)
#define MAYBE_CancelThenStart DISABLED_CancelThenStart
#else
#define MAYBE_CancelThenStart CancelThenStart
#endif
// Cancel a request and then start another one for the same URL.
TEST_F(CertNetFetcherImplTest, MAYBE_CancelThenStart) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();

  GURL url = test_server_.GetURL("/cert.crt");

  std::unique_ptr<CertNetFetcher::Request> request1 =
      StartRequest(fetcher.get(), url);
  request1.reset();

  std::unique_ptr<CertNetFetcher::Request> request2 =
      StartRequest(fetcher.get(), url);

  std::unique_ptr<CertNetFetcher::Request> request3 =
      StartRequest(fetcher.get(), url);
  request3.reset();

  // All but |request2| were canceled.
  VerifySuccess("-cert.crt-\n", request2.get());
}

// Start duplicate requests and then cancel all of them.
TEST_F(CertNetFetcherImplTest, CancelAll) {
  ASSERT_TRUE(test_server_.Start());

  auto fetcher = CreateFetcher();
  std::unique_ptr<CertNetFetcher::Request> request[3];

  GURL url = test_server_.GetURL("/cert.crt");

  for (size_t i = 0; i < arraysize(request); ++i) {
    request[i] = StartRequest(fetcher.get(), url);
  }

  // Cancel all the requests.
  for (size_t i = 0; i < arraysize(request); ++i)
    request[i].reset();

  EXPECT_EQ(1, NumCreatedRequests());
}

}  // namespace

}  // namespace net
