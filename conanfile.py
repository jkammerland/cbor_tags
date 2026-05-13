from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps

class CborTagsConan(ConanFile):
    name = "cbor-tags"
    version = "0.15.0"
    license = "MIT"
    description = "Binary tagging library with automatic encoding/decoding for CBOR"
    homepage = "https://github.com/jkammerland/cbor_tags"
    topics = ("cbor", "serialization", "reflection", "tags")
    settings = "os", "compiler", "build_type", "arch"
    options = {"boost_pfr_names": [True, False]}
    default_options = {"boost_pfr_names": False}
    generators = "CMakeDeps", "CMakeToolchain"
    exports_sources = (
        "CMakeLists.txt",
        "include/*",
        "src/*",
        "cmake/*",
        "tools/*",
        "LICENSE",
    )

    def requirements(self):
        self.requires("tl-expected/1.1.0")
        if self.options.boost_pfr_names:
            self.requires("boost/[>=1.84.0 <2]")

    def configure(self):
        # Header-only library
        self.package_type = "header-library"

    def layout(self):
        # Use the default layout
        self.folders.source = "."
        self.folders.build = "build"
        self.folders.generators = "build"

    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables["CBOR_TAGS_BUILD_TESTS"] = "OFF"
        tc.variables["CBOR_TAGS_INSTALL"] = "ON"
        tc.variables["CBOR_TAGS_USE_SYSTEM_EXPECTED"] = "ON"
        tc.variables["CBOR_TAGS_USE_BOOST_PFR_NAMES"] = "ON" if self.options.boost_pfr_names else "OFF"
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
        self.copy("LICENSE", dst="licenses")

    def package_info(self):
        self.cpp_info.set_property("cmake_file_name", "cbor_tags")
        self.cpp_info.set_property("cmake_target_name", "cbor_tags")
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        if self.options.boost_pfr_names:
            self.cpp_info.defines.append("CBOR_TAGS_USE_BOOST_PFR_NAMES=1")

        # Header-only library
        self.cpp_info.header_only = True
