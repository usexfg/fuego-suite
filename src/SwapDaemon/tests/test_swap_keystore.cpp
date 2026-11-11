// Copyright (c) 2017-2026 Fuego Developers
//
// This file is part of Fuego.
//
// Fuego is free software distributed in the hope that it
// will be useful, but WITHOUT ANY WARRANTY; without even the
// implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. You can redistribute it and/or modify it under the terms
// of the GNU General Public License v3 or later versions as published
// by the Free Software Foundation. Fuego includes elements written
// by third parties. See file labeled LICENSE for more details.
// You should have received a copy of the GNU General Public License
// along with Fuego. If not, see <https://www.gnu.org/licenses/>.

#include "SwapDaemon/SwapDaemon.h"
#include "SwapDaemon/SwapKeystore.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace XfgSwap;

static int g_failures = 0;
static int g_checks = 0;

static void check(bool condition, const char* label) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("FAIL  %s\n", label);
  }
}

static void setPassphraseVariable(const char* value) {
#ifdef _WIN32
  _putenv_s("XFG_SWAPD_KEYSTORE_PASSPHRASE", value);
#else
  setenv("XFG_SWAPD_KEYSTORE_PASSPHRASE", value, 1);
#endif
}

static const char* const KEYSTORE = "test_swap_keystore.json";
static const char* const PASSPHRASE = "correct horse battery staple";
static const std::string XFG_KEY(64, 'a');
static const std::string BTC_WIF = "KwntMbt59tTsj8xqpqYqRRWufyjGunvhSyeMo3NTYpFYzZbXJ5Hp";

static std::string readAll(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}

static void testRoundTrip() {
  std::string error;
  check(saveKeystore(KEYSTORE, PASSPHRASE, {{"xfg", XFG_KEY}, {"btc_wif", BTC_WIF}}, error),
        "keystore saves");
  const std::string onDisk = readAll(KEYSTORE);
  check(onDisk.find(XFG_KEY) == std::string::npos && onDisk.find(BTC_WIF) == std::string::npos,
        "no secret appears in the file");
#ifndef _WIN32
  struct stat info {};
  check(stat(KEYSTORE, &info) == 0 && (info.st_mode & 0777) == 0600, "keystore is owner-only");
#endif
  KeystoreSecrets loaded;
  check(loadKeystore(KEYSTORE, PASSPHRASE, loaded, error) && loaded.size() == 2 &&
            loaded["xfg"] == XFG_KEY && loaded["btc_wif"] == BTC_WIF,
        "keystore opens with the passphrase");
  check(!loadKeystore(KEYSTORE, "wrong passphrase!", loaded, error), "wrong passphrase refused");

  std::string tampered = onDisk;
  const size_t at = tampered.find("\"ciphertext\":\"") + 14;
  tampered[at] = tampered[at] == '0' ? '1' : '0';
  {
    std::ofstream out(KEYSTORE, std::ios::binary | std::ios::trunc);
    out << tampered;
  }
  check(!loadKeystore(KEYSTORE, PASSPHRASE, loaded, error), "modified ciphertext refused");
  check(!saveKeystore(KEYSTORE, "short", {{"xfg", XFG_KEY}}, error), "short passphrase refused");
  check(!saveKeystore(KEYSTORE, PASSPHRASE, {{"bad\"name", XFG_KEY}}, error), "unsafe entry name refused");
  check(!saveKeystore(KEYSTORE, PASSPHRASE, {{"xfg", "has\"quote"}}, error), "unsafe secret refused");
  std::remove(KEYSTORE);
}

static void testReferences() {
  const KeystoreSecrets secrets = {{"xfg", XFG_KEY}};
  std::string json = "{\"xfg_secret_key\":\"keystore:xfg\",\"other\":\"keystore\"}";
  std::string error;
  check(hasKeystoreReferences(json), "reference detected");
  check(resolveKeystoreReferences(json, secrets, error) &&
            json == "{\"xfg_secret_key\":\"" + XFG_KEY + "\",\"other\":\"keystore\"}",
        "reference replaced by the secret only");
  std::string missing = "{\"btc_wif\":\"keystore:btc\"}";
  check(!resolveKeystoreReferences(missing, secrets, error), "unknown entry refused");
}

static void testConfigLoadsThroughKeystore() {
  std::string error;
  check(saveKeystore(KEYSTORE, PASSPHRASE, {{"xfg", XFG_KEY}}, error), "keystore for config saves");
  const std::string configPath = "test_swap_keystore_config.json";
  {
    std::ofstream out(configPath);
    out << "{\"keystore\":\"" << KEYSTORE << "\",\"xfg_secret_key\":\"keystore:xfg\"}";
  }
  setPassphraseVariable(PASSPHRASE);
  ChainClientConfig cfg;
  check(loadChainClientConfig(configPath, cfg, error) && cfg.xfgSecretKeyHex == XFG_KEY,
        "config resolves its key from the keystore");
  setPassphraseVariable("not the passphrase");
  ChainClientConfig refused;
  check(!loadChainClientConfig(configPath, refused, error), "config with a wrong passphrase refused");
  {
    std::ofstream out(configPath, std::ios::trunc);
    out << "{\"xfg_secret_key\":\"keystore:xfg\"}";
  }
  check(!loadChainClientConfig(configPath, refused, error), "reference without a keystore path refused");
  std::remove(configPath.c_str());
  std::remove(KEYSTORE);
}

int main() {
  testRoundTrip();
  testReferences();
  testConfigLoadsThroughKeystore();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
