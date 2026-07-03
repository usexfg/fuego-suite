Package: openssl[core]:arm64-osx -> 3.1.0#1

**Host Environment**

- Host: arm64-osx
- Compiler: AppleClang 17.0.0.17000603
-    vcpkg-tool version: 2023-03-29-664f8bb619b752430368d0f30a8289b761f5caba
    vcpkg-scripts version: 1712ed517 2023-04-03 (3 years, 3 months ago)

**To Reproduce**

`vcpkg install `

**Failure logs**

```
-- Downloading https://github.com/openssl/openssl/archive/openssl-3.1.0.tar.gz -> openssl-openssl-openssl-3.1.0.tar.gz...
-- Extracting source /Users/aejt/xfgo/swapxfgui/ci_tools_atomic_dex/vcpkg-repo/downloads/openssl-openssl-openssl-3.1.0.tar.gz
-- Applying patch disable-apps.patch
-- Applying patch disable-install-docs.patch
-- Applying patch script-prefix.patch
-- Applying patch windows/install-layout.patch
-- Applying patch windows/install-pdbs.patch
-- Applying patch unix/android-cc.patch
-- Applying patch unix/move-openssldir.patch
-- Applying patch unix/no-empty-dirs.patch
-- Applying patch unix/no-static-libs-for-shared.patch
-- Using source at /Users/aejt/xfgo/swapxfgui/ci_tools_atomic_dex/vcpkg-repo/buildtrees/openssl/src/nssl-3.1.0-1ebd9e680e.clean
CMake Warning (dev) at scripts/cmake/vcpkg_find_acquire_program.cmake:70 (cmake_parse_arguments):
  The INTERPRETER keyword was followed by an empty string or no value at all.
  Policy CMP0174 is not set, so cmake_parse_arguments() will unset the
  arg_INTERPRETER variable rather than setting it to an empty string.
Call Stack (most recent call first):
  scripts/cmake/vcpkg_find_acquire_program.cmake:594 (z_vcpkg_find_acquire_program_find_internal)
  ports/openssl/unix/portfile.cmake:18 (vcpkg_find_acquire_program)
  ports/openssl/portfile.cmake:65 (include)
  scripts/ports.cmake:147 (include)
This warning is for project developers.  Use -Wno-dev to suppress it.

CMake Warning (dev) at scripts/cmake/vcpkg_find_acquire_program.cmake:30 (cmake_parse_arguments):
  The INTERPRETER keyword was followed by an empty string or no value at all.
  Policy CMP0174 is not set, so cmake_parse_arguments() will unset the
  arg_INTERPRETER variable rather than setting it to an empty string.
Call Stack (most recent call first):
  scripts/cmake/vcpkg_find_acquire_program.cmake:600 (z_vcpkg_find_acquire_program_find_external)
  ports/openssl/unix/portfile.cmake:18 (vcpkg_find_acquire_program)
  ports/openssl/portfile.cmake:65 (include)
  scripts/ports.cmake:147 (include)
This warning is for project developers.  Use -Wno-dev to suppress it.

CMake Warning (dev) at scripts/cmake/vcpkg_find_acquire_program.cmake:70 (cmake_parse_arguments):
  The INTERPRETER keyword was followed by an empty string or no value at all.
  Policy CMP0174 is not set, so cmake_parse_arguments() will unset the
  arg_INTERPRETER variable rather than setting it to an empty string.
Call Stack (most recent call first):
  scripts/cmake/vcpkg_find_acquire_program.cmake:594 (z_vcpkg_find_acquire_program_find_internal)
  /Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/share/vcpkg-cmake/vcpkg_cmake_configure.cmake:104 (vcpkg_find_acquire_program)
  /Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/share/vcpkg-cmake-get-vars/vcpkg_cmake_get_vars.cmake:15 (vcpkg_cmake_configure)
  ports/openssl/unix/portfile.cmake:33 (vcpkg_cmake_get_vars)
  ports/openssl/portfile.cmake:65 (include)
  scripts/ports.cmake:147 (include)
This warning is for project developers.  Use -Wno-dev to suppress it.

CMake Warning (dev) at scripts/cmake/vcpkg_find_acquire_program.cmake:30 (cmake_parse_arguments):
  The INTERPRETER keyword was followed by an empty string or no value at all.
  Policy CMP0174 is not set, so cmake_parse_arguments() will unset the
  arg_INTERPRETER variable rather than setting it to an empty string.
Call Stack (most recent call first):
  scripts/cmake/vcpkg_find_acquire_program.cmake:600 (z_vcpkg_find_acquire_program_find_external)
  /Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/share/vcpkg-cmake/vcpkg_cmake_configure.cmake:104 (vcpkg_find_acquire_program)
  /Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/share/vcpkg-cmake-get-vars/vcpkg_cmake_get_vars.cmake:15 (vcpkg_cmake_configure)
  ports/openssl/unix/portfile.cmake:33 (vcpkg_cmake_get_vars)
  ports/openssl/portfile.cmake:65 (include)
  scripts/ports.cmake:147 (include)
This warning is for project developers.  Use -Wno-dev to suppress it.

-- Found external ninja('1.13.0').
-- Getting CMake variables for arm64-osx
-- Getting CMake variables for arm64-osx-dbg
-- Getting CMake variables for arm64-osx-rel
CMake Warning (dev) at scripts/cmake/vcpkg_find_acquire_program.cmake:70 (cmake_parse_arguments):
  The INTERPRETER keyword was followed by an empty string or no value at all.
  Policy CMP0174 is not set, so cmake_parse_arguments() will unset the
  arg_INTERPRETER variable rather than setting it to an empty string.
Call Stack (most recent call first):
  scripts/cmake/vcpkg_find_acquire_program.cmake:594 (z_vcpkg_find_acquire_program_find_internal)
  scripts/cmake/z_vcpkg_setup_pkgconfig_path.cmake:19 (vcpkg_find_acquire_program)
  scripts/cmake/vcpkg_configure_make.cmake:769 (z_vcpkg_setup_pkgconfig_path)
  ports/openssl/unix/portfile.cmake:104 (vcpkg_configure_make)
  ports/openssl/portfile.cmake:65 (include)
  scripts/ports.cmake:147 (include)
This warning is for project developers.  Use -Wno-dev to suppress it.

-- Configuring arm64-osx-dbg
-- Configuring arm64-osx-rel
CMake Warning (dev) at scripts/cmake/vcpkg_build_make.cmake:6 (cmake_parse_arguments):
  The LOGFILE_ROOT keyword was followed by an empty string or no value at
  all.  Policy CMP0174 is not set, so cmake_parse_arguments() will unset the
  arg_LOGFILE_ROOT variable rather than setting it to an empty string.
Call Stack (most recent call first):
  scripts/cmake/vcpkg_install_make.cmake:2 (vcpkg_build_make)
  ports/openssl/unix/portfile.cmake:118 (vcpkg_install_make)
  ports/openssl/portfile.cmake:65 (include)
  scripts/ports.cmake:147 (include)
This warning is for project developers.  Use -Wno-dev to suppress it.

-- Building arm64-osx-dbg
CMake Error at scripts/cmake/vcpkg_execute_build_process.cmake:134 (message):
    Command failed: /usr/bin/make V=1 -j 9 -f Makefile build_sw
    Working Directory: /Users/aejt/xfgo/swapxfgui/ci_tools_atomic_dex/vcpkg-repo/buildtrees/openssl/arm64-osx-dbg/
    See logs for more information:
      /Users/aejt/xfgo/swapxfgui/ci_tools_atomic_dex/vcpkg-repo/buildtrees/openssl/build-arm64-osx-dbg-out.log
      /Users/aejt/xfgo/swapxfgui/ci_tools_atomic_dex/vcpkg-repo/buildtrees/openssl/build-arm64-osx-dbg-err.log

Call Stack (most recent call first):
  scripts/cmake/vcpkg_build_make.cmake:151 (vcpkg_execute_build_process)
  scripts/cmake/vcpkg_install_make.cmake:2 (vcpkg_build_make)
  ports/openssl/unix/portfile.cmake:118 (vcpkg_install_make)
  ports/openssl/portfile.cmake:65 (include)
  scripts/ports.cmake:147 (include)



```
<details><summary>/Users/aejt/xfgo/swapxfgui/ci_tools_atomic_dex/vcpkg-repo/buildtrees/openssl/build-arm64-osx-dbg-out.log</summary>

```
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/crypto/bn_conf.h.in > include/crypto/bn_conf.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/crypto/dso_conf.h.in > include/crypto/dso_conf.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/asn1.h.in > include/openssl/asn1.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/asn1t.h.in > include/openssl/asn1t.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/bio.h.in > include/openssl/bio.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/cmp.h.in > include/openssl/cmp.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/cms.h.in > include/openssl/cms.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/conf.h.in > include/openssl/conf.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/crmf.h.in > include/openssl/crmf.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/crypto.h.in > include/openssl/crypto.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/ct.h.in > include/openssl/ct.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/err.h.in > include/openssl/err.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/ess.h.in > include/openssl/ess.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/fipskey.h.in > include/openssl/fipskey.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/lhash.h.in > include/openssl/lhash.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/ocsp.h.in > include/openssl/ocsp.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/opensslv.h.in > include/openssl/opensslv.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/pkcs12.h.in > include/openssl/pkcs12.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/pkcs7.h.in > include/openssl/pkcs7.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/safestack.h.in > include/openssl/safestack.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/srp.h.in > include/openssl/srp.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/ssl.h.in > include/openssl/ssl.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/ui.h.in > include/openssl/ui.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/x509.h.in > include/openssl/x509.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/x509_vfy.h.in > include/openssl/x509_vfy.h
/usr/bin/perl "-I." -Mconfigdata "../src/nssl-3.1.0-1ebd9e680e.clean/util/dofile.pl" "-oMakefile" ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/x509v3.h.in > include/openssl/x509v3.h
/Library/Developer/CommandLineTools/usr/bin/make depend && /Library/Developer/CommandLineTools/usr/bin/make _build_sw
/usr/bin/cc -isysroot -g -fPIC  -I. -Iinclude -Iproviders/common/include -Iproviders/implementations/include -I../src/nssl-3.1.0-1ebd9e680e.clean -I../src/nssl-3.1.0-1ebd9e680e.clean/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/common/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/implementations/include  -DBSAES_ASM -DECP_NISTZ256_ASM -DKECCAK1600_ASM -DMD5_ASM -DOPENSSL_BN_ASM_MONT -DOPENSSL_CPUID_OBJ -DOPENSSL_SM3_ASM -DPOLY1305_ASM -DSHA1_ASM -DSHA256_ASM -DSHA512_ASM -DSM4_ASM -DSTATIC_LEGACY -DVPAES_ASM -DVPSM4_ASM -fPIC -arch arm64 -fPIC -DL_ENDIAN -DOPENSSL_PIC -DOPENSSLDIR="\"/etc/ssl\"" -DENGINESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/engines-3\"" -DMODULESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/ossl-modules\"" -D_REENTRANT -DOPENSSL_BUILDING_OPENSSL -isysroot -g  -c -o crypto/aes/libcrypto-lib-aes_cbc.o ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_cbc.c
/usr/bin/cc -isysroot -g -fPIC  -I. -Iinclude -Iproviders/common/include -Iproviders/implementations/include -I../src/nssl-3.1.0-1ebd9e680e.clean -I../src/nssl-3.1.0-1ebd9e680e.clean/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/common/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/implementations/include  -DBSAES_ASM -DECP_NISTZ256_ASM -DKECCAK1600_ASM -DMD5_ASM -DOPENSSL_BN_ASM_MONT -DOPENSSL_CPUID_OBJ -DOPENSSL_SM3_ASM -DPOLY1305_ASM -DSHA1_ASM -DSHA256_ASM -DSHA512_ASM -DSM4_ASM -DSTATIC_LEGACY -DVPAES_ASM -DVPSM4_ASM -fPIC -arch arm64 -fPIC -DL_ENDIAN -DOPENSSL_PIC -DOPENSSLDIR="\"/etc/ssl\"" -DENGINESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/engines-3\"" -DMODULESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/ossl-modules\"" -D_REENTRANT -DOPENSSL_BUILDING_OPENSSL -isysroot -g  -c -o crypto/aes/libcrypto-lib-aes_cfb.o ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_cfb.c
/usr/bin/cc -isysroot -g -fPIC  -I. -Iinclude -Iproviders/common/include -Iproviders/implementations/include -I../src/nssl-3.1.0-1ebd9e680e.clean -I../src/nssl-3.1.0-1ebd9e680e.clean/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/common/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/implementations/include  -DBSAES_ASM -DECP_NISTZ256_ASM -DKECCAK1600_ASM -DMD5_ASM -DOPENSSL_BN_ASM_MONT -DOPENSSL_CPUID_OBJ -DOPENSSL_SM3_ASM -DPOLY1305_ASM -DSHA1_ASM -DSHA256_ASM -DSHA512_ASM -DSM4_ASM -DSTATIC_LEGACY -DVPAES_ASM -DVPSM4_ASM -fPIC -arch arm64 -fPIC -DL_ENDIAN -DOPENSSL_PIC -DOPENSSLDIR="\"/etc/ssl\"" -DENGINESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/engines-3\"" -DMODULESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/ossl-modules\"" -D_REENTRANT -DOPENSSL_BUILDING_OPENSSL -isysroot -g  -c -o crypto/aes/libcrypto-lib-aes_core.o ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_core.c
/usr/bin/cc -isysroot -g -fPIC  -I. -Iinclude -Iproviders/common/include -Iproviders/implementations/include -I../src/nssl-3.1.0-1ebd9e680e.clean -I../src/nssl-3.1.0-1ebd9e680e.clean/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/common/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/implementations/include  -DBSAES_ASM -DECP_NISTZ256_ASM -DKECCAK1600_ASM -DMD5_ASM -DOPENSSL_BN_ASM_MONT -DOPENSSL_CPUID_OBJ -DOPENSSL_SM3_ASM -DPOLY1305_ASM -DSHA1_ASM -DSHA256_ASM -DSHA512_ASM -DSM4_ASM -DSTATIC_LEGACY -DVPAES_ASM -DVPSM4_ASM -fPIC -arch arm64 -fPIC -DL_ENDIAN -DOPENSSL_PIC -DOPENSSLDIR="\"/etc/ssl\"" -DENGINESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/engines-3\"" -DMODULESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/ossl-modules\"" -D_REENTRANT -DOPENSSL_BUILDING_OPENSSL -isysroot -g  -c -o crypto/aes/libcrypto-lib-aes_ecb.o ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_ecb.c
/usr/bin/cc -isysroot -g -fPIC  -I. -Iinclude -Iproviders/common/include -Iproviders/implementations/include -I../src/nssl-3.1.0-1ebd9e680e.clean -I../src/nssl-3.1.0-1ebd9e680e.clean/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/common/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/implementations/include  -DBSAES_ASM -DECP_NISTZ256_ASM -DKECCAK1600_ASM -DMD5_ASM -DOPENSSL_BN_ASM_MONT -DOPENSSL_CPUID_OBJ -DOPENSSL_SM3_ASM -DPOLY1305_ASM -DSHA1_ASM -DSHA256_ASM -DSHA512_ASM -DSM4_ASM -DSTATIC_LEGACY -DVPAES_ASM -DVPSM4_ASM -fPIC -arch arm64 -fPIC -DL_ENDIAN -DOPENSSL_PIC -DOPENSSLDIR="\"/etc/ssl\"" -DENGINESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/engines-3\"" -DMODULESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/ossl-modules\"" -D_REENTRANT -DOPENSSL_BUILDING_OPENSSL -isysroot -g  -c -o crypto/aes/libcrypto-lib-aes_ige.o ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_ige.c
/usr/bin/cc -isysroot -g -fPIC  -I. -Iinclude -Iproviders/common/include -Iproviders/implementations/include -I../src/nssl-3.1.0-1ebd9e680e.clean -I../src/nssl-3.1.0-1ebd9e680e.clean/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/common/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/implementations/include  -DBSAES_ASM -DECP_NISTZ256_ASM -DKECCAK1600_ASM -DMD5_ASM -DOPENSSL_BN_ASM_MONT -DOPENSSL_CPUID_OBJ -DOPENSSL_SM3_ASM -DPOLY1305_ASM -DSHA1_ASM -DSHA256_ASM -DSHA512_ASM -DSM4_ASM -DSTATIC_LEGACY -DVPAES_ASM -DVPSM4_ASM -fPIC -arch arm64 -fPIC -DL_ENDIAN -DOPENSSL_PIC -DOPENSSLDIR="\"/etc/ssl\"" -DENGINESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/engines-3\"" -DMODULESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/ossl-modules\"" -D_REENTRANT -DOPENSSL_BUILDING_OPENSSL -isysroot -g  -c -o crypto/aes/libcrypto-lib-aes_misc.o ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_misc.c
/usr/bin/cc -isysroot -g -fPIC  -I. -Iinclude -Iproviders/common/include -Iproviders/implementations/include -I../src/nssl-3.1.0-1ebd9e680e.clean -I../src/nssl-3.1.0-1ebd9e680e.clean/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/common/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/implementations/include  -DBSAES_ASM -DECP_NISTZ256_ASM -DKECCAK1600_ASM -DMD5_ASM -DOPENSSL_BN_ASM_MONT -DOPENSSL_CPUID_OBJ -DOPENSSL_SM3_ASM -DPOLY1305_ASM -DSHA1_ASM -DSHA256_ASM -DSHA512_ASM -DSM4_ASM -DSTATIC_LEGACY -DVPAES_ASM -DVPSM4_ASM -fPIC -arch arm64 -fPIC -DL_ENDIAN -DOPENSSL_PIC -DOPENSSLDIR="\"/etc/ssl\"" -DENGINESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/engines-3\"" -DMODULESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/ossl-modules\"" -D_REENTRANT -DOPENSSL_BUILDING_OPENSSL -isysroot -g  -c -o crypto/aes/libcrypto-lib-aes_ofb.o ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_ofb.c
/usr/bin/cc -isysroot -g -fPIC  -I. -Iinclude -Iproviders/common/include -Iproviders/implementations/include -I../src/nssl-3.1.0-1ebd9e680e.clean -I../src/nssl-3.1.0-1ebd9e680e.clean/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/common/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/implementations/include  -DBSAES_ASM -DECP_NISTZ256_ASM -DKECCAK1600_ASM -DMD5_ASM -DOPENSSL_BN_ASM_MONT -DOPENSSL_CPUID_OBJ -DOPENSSL_SM3_ASM -DPOLY1305_ASM -DSHA1_ASM -DSHA256_ASM -DSHA512_ASM -DSM4_ASM -DSTATIC_LEGACY -DVPAES_ASM -DVPSM4_ASM -fPIC -arch arm64 -fPIC -DL_ENDIAN -DOPENSSL_PIC -DOPENSSLDIR="\"/etc/ssl\"" -DENGINESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/engines-3\"" -DMODULESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/ossl-modules\"" -D_REENTRANT -DOPENSSL_BUILDING_OPENSSL -isysroot -g  -c -o crypto/aes/libcrypto-lib-aes_wrap.o ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_wrap.c
CC="/usr/bin/cc -isysroot -g -fPIC" /usr/bin/perl ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/asm/aesv8-armx.pl "ios64" -Icrypto -I../src/nssl-3.1.0-1ebd9e680e.clean/crypto -I. -Iinclude -Iproviders/common/include -Iproviders/implementations/include -I../src/nssl-3.1.0-1ebd9e680e.clean -I../src/nssl-3.1.0-1ebd9e680e.clean/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/common/include -I../src/nssl-3.1.0-1ebd9e680e.clean/providers/implementations/include -fPIC -arch arm64 -fPIC -DL_ENDIAN -DOPENSSL_PIC -DOPENSSLDIR="\"/etc/ssl\"" -DENGINESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/engines-3\"" -DMODULESDIR="\"/Users/aejt/xfgo/swapxfgui/vcpkg_installed/arm64-osx/debug/lib/ossl-modules\"" -D_REENTRANT -DOPENSSL_BUILDING_OPENSSL -isysroot -g -DBSAES_ASM -DECP_NISTZ256_ASM -DKECCAK1600_ASM -DMD5_ASM -DOPENSSL_BN_ASM_MONT -DOPENSSL_CPUID_OBJ -DOPENSSL_SM3_ASM -DPOLY1305_ASM -DSHA1_ASM -DSHA256_ASM -DSHA512_ASM -DSM4_ASM -DSTATIC_LEGACY -DVPAES_ASM -DVPSM4_ASM  crypto/aes/aesv8-armx.S
```
</details>
<details><summary>/Users/aejt/xfgo/swapxfgui/ci_tools_atomic_dex/vcpkg-repo/buildtrees/openssl/build-arm64-osx-dbg-err.log</summary>

```
clang: warning: no such sysroot directory: '-g' [-Wmissing-sysroot]
clang: warning: no such sysroot directory: '-g' [-Wmissing-sysroot]
clang: warning: no such sysroot directory: '-g' [-Wmissing-sysroot]
clang: warning: no such sysroot directory: '-g' [-Wmissing-sysroot]
clang: warning: no such sysroot directory: '-g' [-Wmissing-sysroot]
clangclang: warning: no such sysroot directory: '-g' [-Wmissing-sysroot]
: warning: no such sysroot directory: '-g' [-Wmissing-sysroot]
clang: warning: no such sysroot directory: '-g' [-Wmissing-sysroot]
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_ige.c:16:
../src/nssl-3.1.0-1ebd9e680e.clean/include/internal/cryptlib.h:14:11: fatal error: 'stdlib.h' file not found
   14 | # include <stdlib.../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_ecb.c:10:10: fatal error: 'assert.h' file not found
   10 | #include <assert.h>
      |          ^~~~~~~~~~
../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_core.c:46:10: fatal error: 'assert.h' file not found
h>
      |           ^~~~~~~~~~
   46 | #include <assert.h>
      |          ^~~~~~~~~~
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_wrap.c:16:
../src/nssl-3.1.0-1ebd9e680e.clean/include/internal/cryptlib.h:14:11: fatal error: 'stdlib.h' file not found
   14 | # include <stdlib.h>
      |           ^~~~~~~~~~
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_cfb.c:17:
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/modes.h:20:
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/types.h:32:
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/e_os2.h:234:
/Library/Developer/CommandLineTools/usr/lib/clang/17/include/inttypes.h:24:15: fatal error: 'inttypes.h' file not found
   24 | #include_next <inttypes.h>
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_ofb.c:      |               ^~~~~~~~~~~~
17:
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/modes.h:20:
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/types.h:32:
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/e_os2.h:234:
/Library/Developer/CommandLineTools/usr/lib/clang/17/include/inttypes.h:24:15: fatal error: 'inttypes.h' file not found
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_cbc.c:18:
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/modes.h:20:
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/types.h:32:
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/e_os2.h:234:
/Library/Developer/CommandLineTools/usr/lib/clang/17/include/inttypes.h:24:15: fatal error: 'inttypes.h' file not found
   24 | #include_next <inttypes.h>
      |               ^~~~~~~~~~~~
   24 | #include_next <inttypes.h>
      |               ^~~~~~~~~~~~
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_misc.c:12:
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/crypto/aes/aes_local.h:13:
In file included from ../src/nssl-3.1.0-1ebd9e680e.clean/include/openssl/e_os2.h:234:
/Library/Developer/CommandLineTools/usr/lib/clang/17/include/inttypes.h:24:15: fatal error: 'inttypes.h' file not found
   24 | #include_next <inttypes.h>
      |               ^~~~~~~~~~~~
1 error generated.
1 error generated.
1 error generated.
1 error generated.
1 error generated.
make[1]: *** [crypto/aes/libcrypto-lib-aes_misc.o] Error 1
make[1]: *** Waiting for unfinished jobs....
make[1]: *** [crypto/aes/libcrypto-lib-aes_ecb.o] Error 1
make[1]: *** [crypto/aes/libcrypto-lib-aes_cbc.o] Error 1
make[1]: *** [crypto/aes/libcrypto-lib-aes_ofb.o] Error 1
make[1]: *** [crypto/aes/libcrypto-lib-aes_cfb.o] Error 1
1 error generated.
make[1]: *** [crypto/aes/libcrypto-lib-aes_core.o] Error 1
1 error generated.
1 error generated.
make[1]: *** [crypto/aes/libcrypto-lib-aes_wrap.o] Error 1
make[1]: *** [crypto/aes/libcrypto-lib-aes_ige.o] Error 1
make: *** [build_sw] Error 2
```
</details>

**Additional context**

<details><summary>vcpkg.json</summary>

```
{
  "name": "swapxfg",
  "version-string": "0.7.0",
  "dependencies": [
    "entt",
    "boost-multiprecision",
    "boost-filesystem",
    "boost-thread",
    "boost-random",
    "boost-lockfree",
    "boost-stacktrace",
    "doctest",
    "fmt",
    "nlohmann-json",
    "range-v3",
    "libsodium",
    "spdlog",
    {
      "name": "spdlog",
      "features": [
        "wchar",
        "wchar-filenames"
      ],
      "platform": "windows"
    },
    {
      "name": "date",
      "features": [
        "remote-api"
      ],
      "platform": "windows"
    },
    "date",
    "cpprestsdk",
    "taskflow"
  ]
}

```
</details>
