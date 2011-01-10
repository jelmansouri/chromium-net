// Copyright (c) 2006-2008 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This command-line program converts an effective-TLD data file in UTF-8 from
// the format provided by Mozilla to the format expected by Chrome.  This
// program generates an intermediate file which is then used by gperf to
// generate a perfect hash map.  The benefit of this approach is that no time is
// spent on program initialization to generate the map of this data.
//
// Running this program finds "effective_tld_names.cc" in the expected location
// in the source checkout and generates "effective_tld_names.gperf" next to it.
//
// Any errors or warnings from this program are recorded in tld_cleanup.log.
//
// In particular, it
//  * Strips blank lines and comments, as well as notes for individual rules.
//  * Strips a single leading and/or trailing dot from each rule, if present.
//  * Logs a warning if a rule contains '!' or '*.' other than at the beginning
//    of the rule.  (This also catches multiple ! or *. at the start of a rule.)
//  * Logs a warning if GURL reports a rule as invalid, but keeps the rule.
//  * Canonicalizes each rule's domain by converting it to a GURL and back.
//  * Adds explicit rules for true TLDs found in any rule.

#include <map>
#include <set>
#include <string>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/file_util.h"
#include "base/i18n/icu_util.h"
#include "base/logging.h"
#include "base/file_path.h"
#include "base/file_util.h"
#include "base/path_service.h"
#include "base/process_util.h"
#include "base/string_util.h"
#include "googleurl/src/gurl.h"
#include "googleurl/src/url_parse.h"

namespace {
struct Rule {
  bool exception;
  bool wildcard;
};

typedef std::map<std::string, Rule> RuleMap;
typedef std::set<std::string> RuleSet;
}

// Writes the list of domain rules contained in the 'rules' set to the
// 'outfile', with each rule terminated by a LF.  The file must already have
// been created with write access.
bool WriteRules(const RuleMap& rules, FilePath outfile) {
  std::string data;
  data.append(
      "%{\n"
      "// Copyright (c) 2009 The Chromium Authors. All rights reserved.\n"
      "// Use of this source code is governed by a BSD-style license that\n"
      "// can be found in the LICENSE file.\n\n"
      "// This file is generated by net/tools/tld_cleanup/.\n"
      "// DO NOT MANUALLY EDIT!\n"
      "%}\n"
      "struct DomainRule {\n"
      "  const char *name;\n"
      "  int type;  // 1: exception, 2: wildcard\n"
      "};\n"
      "%%\n"
  );

  for (RuleMap::const_iterator i = rules.begin(); i != rules.end(); ++i) {
    data.append(i->first);
    data.append(", ");
    if (i->second.exception) {
      data.append("1");
    } else if (i->second.wildcard) {
      data.append("2");
    } else {
      data.append("0");
    }
    data.append("\n");
  }

  data.append("%%\n");

  int written = file_util::WriteFile(outfile, data.data(), data.size());

  return written == static_cast<int>(data.size());
}

// These result codes should be in increasing order of severity.
typedef enum {
  kSuccess,
  kWarning,
  kError,
} NormalizeResult;

// Adjusts the rule to a standard form: removes single extraneous dots and
// canonicalizes it using GURL. Returns kSuccess if the rule is interpreted as
// valid; logs a warning and returns kWarning if it is probably invalid; and
// logs an error and returns kError if the rule is (almost) certainly invalid.
NormalizeResult NormalizeRule(std::string* domain, Rule* rule) {
  NormalizeResult result = kSuccess;

  // Strip single leading and trailing dots.
  if (domain->at(0) == '.')
    domain->erase(0, 1);
  if (domain->size() == 0) {
    LOG(WARNING) << "Ignoring empty rule";
    return kWarning;
  }
  if (domain->at(domain->size() - 1) == '.')
    domain->erase(domain->size() - 1, 1);
  if (domain->size() == 0) {
    LOG(WARNING) << "Ignoring empty rule";
    return kWarning;
  }

  // Allow single leading '*.' or '!', saved here so it's not canonicalized.
  size_t start_offset = 0;
  if (domain->at(0) == '!') {
    domain->erase(0, 1);
    rule->exception = true;
  } else if (domain->find("*.") == 0) {
    domain->erase(0, 2);
    rule->wildcard = true;
  }
  if (domain->size() == 0) {
    LOG(WARNING) << "Ignoring empty rule";
    return kWarning;
  }

  // Warn about additional '*.' or '!'.
  if (domain->find("*.", start_offset) != std::string::npos ||
      domain->find('!', start_offset) != std::string::npos) {
    LOG(WARNING) << "Keeping probably invalid rule: " << *domain;
    result = kWarning;
  }

  // Make a GURL and normalize it, then get the host back out.
  std::string url = "http://";
  url.append(*domain);
  GURL gurl(url);
  const std::string& spec = gurl.possibly_invalid_spec();
  url_parse::Component host = gurl.parsed_for_possibly_invalid_spec().host;
  if (host.len < 0) {
    LOG(ERROR) << "Ignoring rule that couldn't be normalized: " << *domain;
    return kError;
  }
  if (!gurl.is_valid()) {
    LOG(WARNING) << "Keeping rule that GURL says is invalid: " << *domain;
    result = kWarning;
  }
  domain->assign(spec.substr(host.begin, host.len));

  return result;
}

// Loads the file described by 'in_filename', converts it to the desired format
// (see the file comments above), and saves it into 'out_filename'.  Returns
// the most severe of the result codes encountered when normalizing the rules.
NormalizeResult NormalizeFile(const FilePath& in_filename,
                              const FilePath& out_filename) {
  std::string data;
  if (!file_util::ReadFileToString(in_filename, &data)) {
    LOG(ERROR) << "Unable to read file";
    // We return success since we've already reported the error.
    return kSuccess;
  }

  // We do a lot of string assignment during parsing, but simplicity is more
  // important than performance here.
  std::string domain;
  NormalizeResult result = kSuccess;
  size_t line_start = 0;
  size_t line_end = 0;
  RuleMap rules;
  RuleSet extra_rules;
  while (line_start < data.size()) {
    // Skip comments.
    if (line_start + 1 < data.size() &&
        data[line_start] == '/' &&
        data[line_start + 1] == '/') {
      line_end = data.find_first_of("\r\n", line_start);
      if (line_end == std::string::npos)
        line_end = data.size();
    } else {
      // Truncate at first whitespace.
      line_end = data.find_first_of("\r\n \t", line_start);
      if (line_end == std::string::npos)
        line_end = data.size();
      domain.assign(data.data(), line_start, line_end - line_start);

      Rule rule;
      rule.wildcard = false;
      rule.exception = false;
      NormalizeResult new_result = NormalizeRule(&domain, &rule);
      if (new_result != kError) {
        // Check the existing rules to make sure we don't have an exception and
        // wildcard for the same rule.  If we did, we'd have to update our
        // parsing code to handle this case.
        CHECK(rules.find(domain) == rules.end());

        rules[domain] = rule;
        // Add true TLD for multi-level rules.  We don't add them right now, in
        // case there's an exception or wild card that either exists or might be
        // added in a later iteration.  In those cases, there's no need to add
        // it and it would just slow down parsing the data.
        size_t tld_start = domain.find_last_of('.');
        if (tld_start != std::string::npos && tld_start + 1 < domain.size())
          extra_rules.insert(domain.substr(tld_start + 1));
      }
      result = std::max(result, new_result);
    }

    // Find beginning of next non-empty line.
    line_start = data.find_first_of("\r\n", line_end);
    if (line_start == std::string::npos)
      line_start = data.size();
    line_start = data.find_first_not_of("\r\n", line_start);
    if (line_start == std::string::npos)
      line_start = data.size();
  }

  for (RuleSet::const_iterator iter = extra_rules.begin();
       iter != extra_rules.end();
       ++iter) {
    if (rules.find(*iter) == rules.end()) {
      Rule rule;
      rule.exception = false;
      rule.wildcard = false;
      rules[*iter] = rule;
    }
  }

  if (!WriteRules(rules, out_filename)) {
    LOG(ERROR) << "Error(s) writing output file";
    result = kError;
  }

  return result;
}

int main(int argc, const char* argv[]) {
  base::EnableTerminationOnHeapCorruption();
  if (argc != 1) {
    fprintf(stderr, "Normalizes and verifies UTF-8 TLD data files\n");
    fprintf(stderr, "Usage: %s\n", argv[0]);
    return 1;
  }

  // Manages the destruction of singletons.
  base::AtExitManager exit_manager;

  // Only use OutputDebugString in debug mode.
#ifdef NDEBUG
  logging::LoggingDestination destination = logging::LOG_ONLY_TO_FILE;
#else
  logging::LoggingDestination destination =
      logging::LOG_TO_BOTH_FILE_AND_SYSTEM_DEBUG_LOG;
#endif

  CommandLine::Init(argc, argv);

  FilePath log_filename;
  PathService::Get(base::DIR_EXE, &log_filename);
  log_filename = log_filename.AppendASCII("tld_cleanup.log");
  logging::InitLogging(log_filename.value().c_str(),
                       destination,
                       logging::LOCK_LOG_FILE,
                       logging::DELETE_OLD_LOG_FILE);

  icu_util::Initialize();

  FilePath input_file;
  PathService::Get(base::DIR_SOURCE_ROOT, &input_file);
  input_file = input_file.Append(FILE_PATH_LITERAL("net"))
                         .Append(FILE_PATH_LITERAL("base"))
                         .Append(FILE_PATH_LITERAL("effective_tld_names.dat"));
  FilePath output_file;
  PathService::Get(base::DIR_SOURCE_ROOT, &output_file);
  output_file = output_file.Append(FILE_PATH_LITERAL("net"))
                           .Append(FILE_PATH_LITERAL("base"))
                           .Append(FILE_PATH_LITERAL(
                               "effective_tld_names.gperf"));
  NormalizeResult result = NormalizeFile(input_file, output_file);
  if (result != kSuccess) {
    fprintf(stderr,
            "Errors or warnings processing file.  See log in tld_cleanup.log.");
  }

  if (result == kError)
    return 1;
  return 0;
}
