from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMakeDeps, cmake_layout, CMake
from conan.tools.files import copy, load
import os


class BaresipConan(ConanFile):
    name = "baresip"
    version = "4.0.0"
    
    # Export all source files needed for building
    exports_sources = "CMakeLists.txt", "src/*", "include/*", "modules/*", "test/*", "share/*", "packaging/*", "webrtc/*", "cmake/*", "mk/*", "LICENSE", "README.md"
    
    # Package metadata
    description = "Modular SIP User-Agent with audio and video support"
    topics = ("sip", "voip", "audio", "video", "communication")
    homepage = "https://github.com/baresip/baresip"
    license = "BSD-3-Clause"
    
    # Binary configuration
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "static": [True, False],
        
        # Audio codecs
        "with_opus": [True, False],
        "with_g722": [True, False],
        "with_g711": [True, False],
        "with_aac": [True, False],
        
        # Video codecs  
        "with_vpx": [True, False],
        "with_av1": [True, False],
        "with_ffmpeg": [True, False],
        
        # Audio systems
        "with_alsa": [True, False],
        "with_pulseaudio": [True, False],
        "with_jack": [True, False],
        "with_portaudio": [True, False],
        "with_pipewire": [True, False],
        
        # Other features
        "with_openssl": [True, False],
        "with_gstreamer": [True, False],
        "with_gtk": [True, False],
        "with_sdl": [True, False],
        "with_mosquitto": [True, False],
        "with_png": [True, False],
        "with_sndfile": [True, False],
    }
    
    default_options = {
        "shared": False,
        "fPIC": True,
        "static": False,  # Use shared libraries to save disk space during build
        
        # Enable common codecs by default  
        "with_opus": True,  # Temporarily disabled - Conan package works but baresip CMake interferes with includes
        "with_g722": True,
        "with_g711": True,
        "with_aac": False,  # Requires commercial license
        
        # Video codecs - modern defaults for better experience
        "with_vpx": False,  # Temporarily disabled - Conan libvpx package has include directory issues
        "with_av1": True,  # Modern codec, good hardware support on macOS
        "with_ffmpeg": False,
        
        # Audio systems - defaults overridden per platform in config_options()
        "with_alsa": False,        # Linux-specific (enabled on Linux)
        "with_pulseaudio": False,  # Linux-specific (enabled on Linux) 
        "with_jack": False,        # Professional audio, opt-in
        "with_portaudio": False,   # System package (enabled per platform)
        "with_pipewire": False,    # Linux-specific modern audio (enabled on Linux)
        
        # Modern multimedia features - defaults overridden per platform in config_options()
        "with_openssl": True,
        "with_gstreamer": True,    # Powerful multimedia framework (disabled on mobile)
        "with_gtk": False,         # GUI toolkit, Linux-specific (enabled on Linux)
        "with_sdl": True,          # Great for media display (disabled on mobile)
        "with_mosquitto": True,    # MQTT for modern IoT applications
        "with_png": True,
        "with_sndfile": True,
    }
    
    def config_options(self):
        # Platform-specific option adjustments - must be done before configure()
        
        if self.settings.os == "Linux":
            # Enable Linux-specific audio systems
            self.options.with_alsa = True
            self.options.with_pulseaudio = True
            self.options.with_pipewire = True
            # Enable GTK for Linux GUI applications
            self.options.with_gtk = True
            # PortAudio available on Linux
            self.options.with_portaudio = True
            
        elif self.settings.os == "Macos":
            # macOS has excellent built-in audio, but PortAudio provides cross-platform API
            self.options.with_portaudio = True
            # Keep Linux-specific audio disabled
            self.options.with_alsa = False
            self.options.with_pulseaudio = False
            self.options.with_pipewire = False
            # No GTK on macOS by default
            self.options.with_gtk = False
            
        elif self.settings.os == "Windows":
            # Windows audio systems
            self.options.with_portaudio = True
            # Keep Linux-specific audio disabled
            self.options.with_alsa = False
            self.options.with_pulseaudio = False
            self.options.with_pipewire = False
            # No GTK on Windows by default
            self.options.with_gtk = False
            
        elif self.settings.os == "iOS":
            # iOS has very limited system access - minimal configuration
            self.options.with_alsa = False
            self.options.with_pulseaudio = False
            self.options.with_pipewire = False
            self.options.with_portaudio = False
            self.options.with_gtk = False
            self.options.with_gstreamer = False  # Not available on iOS
            self.options.with_sdl = False        # Limited on iOS
            
        elif self.settings.os == "Android":
            # Android-specific configuration
            self.options.with_alsa = False
            self.options.with_pulseaudio = False
            self.options.with_pipewire = False
            self.options.with_portaudio = False
            self.options.with_gtk = False
            self.options.with_gstreamer = False  # Limited on Android
            
        # Note: FreeBSD, other Unix systems will use the cross-platform defaults
    
    def configure(self):
        # Remove fPIC for shared libraries (standard Conan pattern)
        if self.options.shared:
            self.options.rm_safe("fPIC")
    
    def requirements(self):
        # Core dependencies
        self.requires("libre/4.0.0", transitive_headers=True)
        
        # Version overrides to resolve conflicts
        self.requires("opus/1.4")  # Use version compatible with FFmpeg to avoid conflicts
        if self.options.with_ffmpeg and self.options.with_vpx:
            self.requires("libvpx/1.14.1", override=True)  # Use version compatible with FFmpeg 6.1.1
        
        if self.options.with_openssl:
            self.requires("openssl/[>=1.1 <4]")
            
        # Audio codecs
        # Note: opus version matches FFmpeg dependency to avoid conflicts
            
        # Video codecs
        if self.options.with_vpx:
            if self.options.with_ffmpeg:
                # Version already set as override above for FFmpeg compatibility
                pass
            else:
                self.requires("libvpx/1.15.2")  # Use latest when no FFmpeg constraint
            
        if self.options.with_av1:
            self.requires("libaom-av1/3.6.1")  # Use existing version
            
        if self.options.with_ffmpeg:
            # Note: We also set this as an override above for version consistency
            self.requires("ffmpeg/6.1.1")
            
        # Audio systems
        if self.options.with_alsa and self.settings.os == "Linux":
            # ALSA is typically system-installed on Linux
            pass  # Will be found by the Find module
            
        if self.options.with_pulseaudio and self.settings.os == "Linux":
            self.requires("pulseaudio/17.0")
            
        if self.options.with_jack:
            # JACK is typically system-installed
            pass  # Will be found by the Find module
            
        if self.options.with_portaudio:
            # PortAudio not available in Conan Center - use system version
            pass  # Will be found by the Find module
            
        # Other dependencies
        if self.options.with_gstreamer:
            self.requires("gstreamer/1.24.7")
            
        if self.options.with_gtk:
            # GTK is typically system-installed
            pass  # Will be found by the Find module
            
        if self.options.with_sdl:
            self.requires("sdl/2.30.8")
            
        if self.options.with_mosquitto:
            self.requires("mosquitto/2.0.21")
            
        if self.options.with_png:
            self.requires("libpng/[>=1.6 <2]")
            
        if self.options.with_sndfile:
            self.requires("libsndfile/[>=1.0 <2]")
    
    def build_requirements(self):
        self.tool_requires("cmake/[>=4.0]")
    
    def layout(self):
        cmake_layout(self)
    
    def generate(self):
        # Generate CMake integration files
        tc = CMakeToolchain(self)
        
        # Pass Conan options to CMake
        tc.variables["BARESIP_USE_CONAN"] = True
        tc.variables["CONAN_OPUS_ENABLED"] = self.options.with_opus
        tc.variables["CONAN_VPX_ENABLED"] = self.options.with_vpx
        tc.variables["CONAN_AV1_ENABLED"] = self.options.with_av1
        tc.variables["CONAN_FFMPEG_ENABLED"] = self.options.with_ffmpeg
        tc.variables["CONAN_ALSA_ENABLED"] = self.options.with_alsa
        tc.variables["CONAN_PULSEAUDIO_ENABLED"] = self.options.with_pulseaudio
        tc.variables["CONAN_JACK_ENABLED"] = self.options.with_jack
        tc.variables["CONAN_PORTAUDIO_ENABLED"] = self.options.with_portaudio
        tc.variables["CONAN_PIPEWIRE_ENABLED"] = self.options.with_pipewire
        tc.variables["CONAN_OPENSSL_ENABLED"] = self.options.with_openssl
        tc.variables["CONAN_SDL_ENABLED"] = self.options.with_sdl
        tc.variables["CONAN_MOSQUITTO_ENABLED"] = self.options.with_mosquitto
        tc.variables["CONAN_PNG_ENABLED"] = self.options.with_png
        tc.variables["CONAN_SNDFILE_ENABLED"] = self.options.with_sndfile
        tc.variables["CONAN_VPX_ENABLED"] = self.options.with_vpx
        
        # Note: Keep STATIC=False to avoid complex static linking issues
        # Modules will be .so files but still work properly
        
        tc.generate()
        
        # Generate dependency information for CMake
        deps = CMakeDeps(self)
        deps.generate()
    
    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
    
    def package(self):
        copy(self, "LICENSE", src=self.source_folder, dst=os.path.join(self.package_folder, "licenses"))
        cmake = CMake(self)
        cmake.install()
    
    def package_info(self):
        # Add the bin directory to PATH so the baresip executable can be found
        self.cpp_info.bindirs = ["bin"]
        
        # Main baresip library
        self.cpp_info.libs = ["baresip"]
        
        # Include directories
        self.cpp_info.includedirs = ["include"]
        
        # Set library directories  
        self.cpp_info.libdirs = ["lib"]
        
        # Platform-specific system libraries
        if self.settings.os == "Linux":
            self.cpp_info.system_libs.extend(["dl", "m", "pthread", "rt"])
        elif self.settings.os == "Macos":
            self.cpp_info.frameworks.extend(["CoreFoundation", "CoreAudio", "AudioToolbox", "AudioUnit", "Security"])
            self.cpp_info.system_libs.extend(["m", "pthread"])
        elif self.settings.os == "Windows":
            self.cpp_info.system_libs.extend(["ws2_32", "iphlpapi", "ole32", "dxguid", "winmm"])
            
        # Mark this as an application package (includes executable)
        self.cpp_info.set_property("cmake_file_name", "baresip")
        self.cpp_info.set_property("cmake_target_name", "baresip::baresip")