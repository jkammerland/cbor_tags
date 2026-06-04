import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps
from conan.tools.files import copy


class CborTagsConan(ConanFile):
    name = "cbor-tags"
    version = "0.20.0"
    license = "MIT"
    description = "Binary tagging library with automatic encoding/decoding for CBOR"
    homepage = "https://github.com/jkammerland/cbor_tags"
    topics = ("cbor", "serialization", "reflection", "tags")
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "boost_pfr_names": [True, False],
        "cwt_openssl": [True, False],
        "cwt_wolfssl": [True, False],
        "magic_enum_names": [True, False],
        "std_expected": [True, False],
        "stl_only": [True, False],
    }
    default_options = {
        "boost_pfr_names": False,
        "cwt_openssl": False,
        "cwt_wolfssl": False,
        "magic_enum_names": False,
        "std_expected": False,
        "stl_only": False,
    }
    exports_sources = (
        "CMakeLists.txt",
        "cbor_tags_config.h.in",
        "include/*",
        "src/*",
        "cmake/*",
        "tools/*",
        "LICENSE",
    )

    def requirements(self):
        if self.options.cwt_openssl:
            self.requires("openssl/[>=3 <4]")
        if self.options.cwt_wolfssl:
            self.requires("wolfssl/[>=5.9.1 <6]")
        if self.options.stl_only:
            return
        self.requires("fmt/[>=11.0.2 <12]")
        self.requires("nameof/0.10.4")
        if not self.options.std_expected:
            self.requires("tl-expected/1.1.0")
        if self.options.boost_pfr_names:
            self.requires("boost/[>=1.84.0 <2]")
        if self.options.magic_enum_names:
            self.requires("magic_enum/0.9.7")

    def configure(self):
        # Header-only library
        self.package_type = "header-library"
        if self.options.boost_pfr_names:
            self.options["boost/*"].header_only = True
        if self.options.cwt_wolfssl:
            self.options["wolfssl/*"].opensslextra = True
            self.options["wolfssl/*"].opensslall = True

    def validate(self):
        if self.options.stl_only:
            check_min_cppstd(self, "26")
            if str(self.settings.compiler) != "gcc":
                raise ConanInvalidConfiguration("stl_only currently requires GCC C++26 std::meta reflection support")
            if self.options.boost_pfr_names:
                raise ConanInvalidConfiguration("stl_only cannot be combined with boost_pfr_names")
            if self.options.magic_enum_names:
                raise ConanInvalidConfiguration("stl_only cannot be combined with magic_enum_names")
            return
        check_min_cppstd(self, "20")
        if self.options.std_expected:
            check_min_cppstd(self, "23")

    def package_id(self):
        self.info.settings.clear()

    def layout(self):
        # Use the default layout
        self.folders.source = "."
        self.folders.build = "build"
        self.folders.generators = "build"

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["CBOR_TAGS_BUILD_TESTS"] = "OFF"
        tc.variables["CBOR_TAGS_INSTALL"] = "ON"
        tc.variables["CBOR_TAGS_STL_ONLY"] = "ON" if self.options.stl_only else "OFF"
        tc.variables["CBOR_TAGS_USE_STD_EXPECTED"] = "ON" if (self.options.std_expected or self.options.stl_only) else "OFF"
        tc.variables["CBOR_TAGS_USE_SYSTEM_EXPECTED"] = "OFF" if (self.options.std_expected or self.options.stl_only) else "ON"
        tc.variables["CBOR_TAGS_USE_BOOST_PFR_NAMES"] = "ON" if self.options.boost_pfr_names else "OFF"
        tc.variables["CBOR_TAGS_USE_MAGIC_ENUM_NAMES"] = "ON" if self.options.magic_enum_names else "OFF"
        tc.variables["CBOR_TAGS_ENABLE_CWT_OPENSSL"] = "ON" if self.options.cwt_openssl else "OFF"
        tc.variables["CBOR_TAGS_ENABLE_CWT_WOLFSSL"] = "ON" if self.options.cwt_wolfssl else "OFF"
        tc.generate()
        deps = CMakeDeps(self)
        deps.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "cbor_tags")
        self.cpp_info.set_property("cmake_target_name", "cbor::all")
        self.cpp_info.libdirs = []
        self.cpp_info.bindirs = []
        tags = self.cpp_info.components["tags"]
        tags.set_property("cmake_target_name", "cbor::tags")
        tags.includedirs = ["include"]
        tags.libdirs = []
        tags.bindirs = []
        tags.requires = []
        if not self.options.stl_only:
            tags.requires.extend(["fmt::fmt", "nameof::nameof"])
        if not self.options.std_expected and not self.options.stl_only:
            tags.requires.append("tl-expected::expected")
        if self.options.boost_pfr_names and not self.options.stl_only:
            tags.requires.append("boost::headers")
        if self.options.magic_enum_names and not self.options.stl_only:
            tags.requires.append("magic_enum::magic_enum")
        if self.options.boost_pfr_names:
            tags.defines.append("CBOR_TAGS_USE_BOOST_PFR_NAMES=1")
        if self.options.magic_enum_names:
            tags.defines.append("CBOR_TAGS_USE_MAGIC_ENUM_NAMES=1")
        if self.options.std_expected or self.options.stl_only:
            tags.defines.append("CBOR_TAGS_USE_STD_EXPECTED=1")
        if self.options.stl_only:
            tags.defines.append("CBOR_TAGS_STL_ONLY=1")
            tags.defines.append("CBOR_TAGS_USE_STD_REFLECTION=1")
            tags.cxxflags.append("-freflection")

        cwt = self.cpp_info.components["cwt"]
        cwt.set_property("cmake_target_name", "cbor::cwt")
        cwt.includedirs = ["include"]
        cwt.libdirs = []
        cwt.bindirs = []
        cwt.requires = ["tags"]

        if self.options.cwt_openssl:
            cwt_openssl = self.cpp_info.components["cwt_openssl"]
            cwt_openssl.set_property("cmake_target_name", "cbor::cwt_openssl")
            cwt_openssl.includedirs = ["include"]
            cwt_openssl.libdirs = []
            cwt_openssl.bindirs = []
            cwt_openssl.requires = ["cwt", "openssl::crypto"]

        if self.options.cwt_wolfssl:
            cwt_wolfssl = self.cpp_info.components["cwt_wolfssl"]
            cwt_wolfssl.set_property("cmake_target_name", "cbor::cwt_wolfssl")
            cwt_wolfssl.includedirs = ["include"]
            cwt_wolfssl.libdirs = []
            cwt_wolfssl.bindirs = []
            cwt_wolfssl.requires = ["cwt", "wolfssl::wolfssl"]

        # Header-only library
        self.cpp_info.header_only = True
