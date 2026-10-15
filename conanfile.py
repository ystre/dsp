from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class Project(ConanFile):
    # The package name is not used for anything (the project is not published
    # to a Conan remote); the real name lives in project.env.
    name = "project"
    settings = "os", "compiler", "build_type", "arch"

    options = {"with_tests": [True, False]}
    default_options = {"with_tests": True}

    def requirements(self):
        if self.options.with_tests:
            self.requires("gtest/1.14.0")
            self.requires("benchmark/1.9.5")

    def generate(self):
        CMakeDeps(self).generate()
        tc = CMakeToolchain(self)
        tc.cache_variables["BUILD_TESTING"] = bool(self.options.with_tests)
        tc.cache_variables["PROJ_BUILD_BENCH"] = bool(self.options.with_tests)
        tc.generate()
