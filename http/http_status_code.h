// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_HTTP_HTTP_STATUS_CODE_H_
#define NET_HTTP_HTTP_STATUS_CODE_H_

#include "net/base/net_export.h"

namespace net {

// HTTP status codes.
// Taken from RFC 2616 Section 10.
enum HttpStatusCode {
  // Informational 1xx
  HTTP_CONTINUE = 100,
  HTTP_SWITCHING_PROTOCOLS = 101,

  // Successful 2xx
  HTTP_OK = 200,
  HTTP_CREATED = 201,
  HTTP_ACCEPTED = 202,
  HTTP_NON_AUTHORITATIVE_INFORMATION = 203,
  HTTP_NO_CONTENT = 204,
  HTTP_RESET_CONTENT = 205,
  HTTP_PARTIAL_CONTENT = 206,

  // Redirection 3xx
  HTTP_MULTIPLE_CHOICES = 300,
  HTTP_MOVED_PERMANENTLY = 301,
  HTTP_FOUND = 302,
  HTTP_SEE_OTHER = 303,
  HTTP_NOT_MODIFIED = 304,
  HTTP_USE_PROXY = 305,
  // 306 is no longer used.
  HTTP_TEMPORARY_REDIRECT = 307,

  // Client error 4xx
  HTTP_BAD_REQUEST = 400,
  HTTP_UNAUTHORIZED = 401,
  HTTP_PAYMENT_REQUIRED = 402,
  HTTP_FORBIDDEN = 403,
  HTTP_NOT_FOUND = 404,
  HTTP_METHOD_NOT_ALLOWED = 405,
  HTTP_NOT_ACCEPTABLE = 406,
  HTTP_PROXY_AUTHENTICATION_REQUIRED = 407,
  HTTP_REQUEST_TIMEOUT = 408,
  HTTP_CONFLICT = 409,
  HTTP_GONE = 410,
  HTTP_LENGTH_REQUIRED = 411,
  HTTP_PRECONDITION_FAILED = 412,
  HTTP_REQUEST_ENTITY_TOO_LARGE = 413,
  HTTP_REQUEST_URI_TOO_LONG = 414,
  HTTP_UNSUPPORTED_MEDIA_TYPE = 415,
  HTTP_REQUESTED_RANGE_NOT_SATISFIABLE = 416,
  HTTP_EXPECTATION_FAILED = 417,

  // Server error 5xx
  HTTP_INTERNAL_SERVER_ERROR = 500,
  HTTP_NOT_IMPLEMENTED = 501,
  HTTP_BAD_GATEWAY = 502,
  HTTP_SERVICE_UNAVAILABLE = 503,
  HTTP_GATEWAY_TIMEOUT = 504,
  HTTP_VERSION_NOT_SUPPORTED = 505,
};

// Returns the corresponding HTTP status description to use in the Reason-Phrase
// field in an HTTP response for given |code|. It's based on the IANA HTTP
// Status Code Registry.
// http://www.iana.org/assignments/http-status-codes/http-status-codes.xml
//
// This function doesn't cover all codes defined above. It returns an empty
// string (or crash in debug build) for status codes which are not yet covered
// or just invalid. Please extend it when needed.
NET_EXPORT const char* GetHttpReasonPhrase(HttpStatusCode code);

}  // namespace net

#endif  // NET_HTTP_HTTP_STATUS_CODE_H_
