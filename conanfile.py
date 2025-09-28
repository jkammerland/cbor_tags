from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, CMakeDeps

class CborTagsConan(ConanFile):
    name = "cbor-tags"
    version = "0.9.4"
    license = "MIT"
    description = "Binary tagging library with automatic encoding/decoding for CBOR"
    homepage = "https://github.com/jkammerland/cbor_tags"
    topics = ("cbor", "serialization", "reflection", "tags")
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"
    exports_sources = (
        "CMakeLists.txt",
        "include/*",
        "src/*",
        "cmake/*",
        "tools/*",
        "LICENSE",
    )
    
    requires = "tl-expected/1.1.0"
    
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
        tc.variables["CBOR_TAGS_USE_SYSTEM_EXPECTED"] = "ON"
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
        
        # Header-only library
        self.cpp_info.header_only = True