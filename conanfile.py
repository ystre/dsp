from conan import ConanFile
from conan.tools.files import copy
from conan.tools.cmake import cmake_layout
import os

class Dsp(ConanFile):
    package_type = "application"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps"

    def layout(self):
        cmake_layout(self)

    def configure(self):
        self.options["fmt/*"].header_only = True
        self.options["spdlog/*"].header_only = True
        self.options["spdlog/*"].use_std_fmt = False
        self.options["librdkafka/*"].ssl = True

    def requirements(self):
        requirements = self.conan_data.get("requirements", [])
        for req in requirements:
            self.requires(req)

    def build_requirements(self):
        self.tool_requires("protobuf/<host_version>")
