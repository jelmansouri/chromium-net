#!/usr/bin/python
# Copyright (c) 2015 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Certificate chain with 1 intermediate, but the root is not in trust store.
Verification is expected to fail because the final intermediate (Intermediate)
does not chain to a known root."""

import common

# Self-signed root certificate, which is NOT saved as the trust anchor.
root = common.create_self_signed_root_certificate('Root')

# Intermediate certificate.
intermediate = common.create_intermediate_certificate('Intermediate', root)

# Target certificate.
target = common.create_end_entity_certificate('Target', intermediate)

# Self-signed root certificate, not part of chain, which is saved as trust
# anchor.
bogus_root = common.create_self_signed_root_certificate('BogusRoot')

chain = [target, intermediate]
trusted = common.TrustAnchor(bogus_root, constrained=False)
time = common.DEFAULT_TIME
verify_result = False

common.write_test_file(__doc__, chain, trusted, time, verify_result)
