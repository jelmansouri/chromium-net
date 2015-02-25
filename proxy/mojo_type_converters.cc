// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/proxy/mojo_type_converters.h"

#include "base/logging.h"
#include "net/base/host_port_pair.h"
#include "net/proxy/proxy_server.h"

namespace net {
namespace {

interfaces::ProxyScheme ProxySchemeToMojo(ProxyServer::Scheme scheme) {
  switch (scheme) {
    case ProxyServer::SCHEME_INVALID:
      return interfaces::PROXY_SCHEME_INVALID;
    case ProxyServer::SCHEME_DIRECT:
      return interfaces::PROXY_SCHEME_DIRECT;
    case ProxyServer::SCHEME_HTTP:
      return interfaces::PROXY_SCHEME_HTTP;
    case ProxyServer::SCHEME_SOCKS4:
      return interfaces::PROXY_SCHEME_SOCKS4;
    case ProxyServer::SCHEME_SOCKS5:
      return interfaces::PROXY_SCHEME_SOCKS5;
    case ProxyServer::SCHEME_HTTPS:
      return interfaces::PROXY_SCHEME_HTTPS;
    case ProxyServer::SCHEME_QUIC:
      return interfaces::PROXY_SCHEME_QUIC;
  }
  NOTREACHED();
  return interfaces::PROXY_SCHEME_INVALID;
}

ProxyServer::Scheme ProxySchemeFromMojo(interfaces::ProxyScheme scheme) {
  switch (scheme) {
    case interfaces::PROXY_SCHEME_INVALID:
      return ProxyServer::SCHEME_INVALID;
    case interfaces::PROXY_SCHEME_DIRECT:
      return ProxyServer::SCHEME_DIRECT;
    case interfaces::PROXY_SCHEME_HTTP:
      return ProxyServer::SCHEME_HTTP;
    case interfaces::PROXY_SCHEME_SOCKS4:
      return ProxyServer::SCHEME_SOCKS4;
    case interfaces::PROXY_SCHEME_SOCKS5:
      return ProxyServer::SCHEME_SOCKS5;
    case interfaces::PROXY_SCHEME_HTTPS:
      return ProxyServer::SCHEME_HTTPS;
    case interfaces::PROXY_SCHEME_QUIC:
      return ProxyServer::SCHEME_QUIC;
  }
  NOTREACHED();
  return ProxyServer::SCHEME_INVALID;
}

}  // namespace
}  // namespace net

namespace mojo {

// static
net::interfaces::ProxyServerPtr
TypeConverter<net::interfaces::ProxyServerPtr, net::ProxyServer>::Convert(
    const net::ProxyServer& obj) {
  net::interfaces::ProxyServerPtr server(net::interfaces::ProxyServer::New());
  server->scheme = net::ProxySchemeToMojo(obj.scheme());
  if (server->scheme != net::interfaces::PROXY_SCHEME_DIRECT &&
      server->scheme != net::interfaces::PROXY_SCHEME_INVALID) {
    server->host = obj.host_port_pair().host();
    server->port = obj.host_port_pair().port();
  }
  return server.Pass();
}

// static
net::ProxyServer
TypeConverter<net::ProxyServer, net::interfaces::ProxyServerPtr>::Convert(
    const net::interfaces::ProxyServerPtr& obj) {
  return net::ProxyServer(net::ProxySchemeFromMojo(obj->scheme),
                          net::HostPortPair(obj->host, obj->port));
}

}  // namespace mojo
