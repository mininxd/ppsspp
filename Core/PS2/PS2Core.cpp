// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include "Core/PS2/PS2Core.h"

#include <cstdio>
#include <cstring>
#include <algorithm>

#if defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
#include <dlfcn.h>
#endif

#if defined(__ANDROID__)
#include <jni.h>
extern JavaVM *gJvm;
#endif

#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/File/FileUtil.h"
#include "Common/File/Path.h"
#include "Common/File/DirListing.h"
#include "Core/Config.h"
#include "Core/Util/PathUtil.h"

namespace PS2 {

PS2Core &PS2Core::GetInstance() {
	static PS2Core s_instance;
	return s_instance;
}

PS2Core::PS2Core() {
	if (!g_Config.ps2BiosPath.empty() && File::Exists(g_Config.ps2BiosPath)) {
		currentBiosPath_ = g_Config.ps2BiosPath;
	}
}

PS2Core::~PS2Core() {
	Shutdown();
}

bool PS2Core::Initialize(const Path &dataDir, const Path &cacheDir) {
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	dataDir_ = dataDir;
	cacheDir_ = cacheDir;

	File::CreateFullPath(dataDir_);
	File::CreateFullPath(cacheDir_);
	File::CreateFullPath(GetDefaultBIOSDirectory());

	WriteDefaultConfiguration();
	LoadNativeLibrary();
	state_ = CoreState::INITIALIZED;
	INFO_LOG(Log::System, "PS2Core initialized. Data: %s, Cache: %s", dataDir_.c_str(), cacheDir_.c_str());
	return true;
}

void PS2Core::Shutdown() {
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	if (state_ == CoreState::RUNNING) {
		Stop(true);
	}
#if defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
	if (libHandle_) {
		dlclose(libHandle_);
		libHandle_ = nullptr;
	}
#endif
	state_ = CoreState::UNINITIALIZED;
}

bool PS2Core::IsLibraryLoaded() const {
	return libHandle_ != nullptr;
}

bool PS2Core::LoadNativeLibrary() {
#if defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
	if (libHandle_) {
		return true;
	}

	const char *libNames[] = {
		"libemucore.so",
		"./libemucore.so",
		"lib/arm64-v8a/libemucore.so",
	};

	for (const char *name : libNames) {
		libHandle_ = dlopen(name, RTLD_NOW | RTLD_GLOBAL);
		if (libHandle_) {
			INFO_LOG(Log::System, "PS2Core: Successfully loaded %s", name);
			break;
		}
	}

	if (!libHandle_) {
		lastError_ = "libemucore.so not loaded";
		return false;
	}

	fnInitialize_ = dlsym(libHandle_, "Java_xyz_aethersx2_android_NativeLibrary_initialize");
	fnRunVMThread_ = dlsym(libHandle_, "Java_xyz_aethersx2_android_NativeLibrary_runVMThread");
	if (!fnRunVMThread_) fnRunVMThread_ = dlsym(libHandle_, "Java_kr_co_iefriends_pcsx2_NativeApp_runVMThread");
	fnStopVMThreadLoop_ = dlsym(libHandle_, "Java_xyz_aethersx2_android_NativeLibrary_stopVMThreadLoop");
	if (!fnStopVMThreadLoop_) fnStopVMThreadLoop_ = dlsym(libHandle_, "Java_kr_co_iefriends_pcsx2_NativeApp_shutdown");
	fnPauseVM_ = dlsym(libHandle_, "Java_xyz_aethersx2_android_NativeLibrary_pauseVM");
	if (!fnPauseVM_) fnPauseVM_ = dlsym(libHandle_, "Java_kr_co_iefriends_pcsx2_NativeApp_pause");
	fnIsVMPaused_ = dlsym(libHandle_, "Java_xyz_aethersx2_android_NativeLibrary_isVMPaused");
	fnResetVM_ = dlsym(libHandle_, "Java_xyz_aethersx2_android_NativeLibrary_resetVM");
	fnChangeSurface_ = dlsym(libHandle_, "Java_xyz_aethersx2_android_NativeLibrary_changeSurface");
	if (!fnChangeSurface_) fnChangeSurface_ = dlsym(libHandle_, "Java_kr_co_iefriends_pcsx2_NativeApp_onNativeSurfaceChanged");
	fnChangeDisc_ = dlsym(libHandle_, "Java_xyz_aethersx2_android_NativeLibrary_changeDisc");
	if (!fnChangeDisc_) fnChangeDisc_ = dlsym(libHandle_, "Java_kr_co_iefriends_pcsx2_NativeApp_changeDisc");
	fnIsBIOSAvailable_ = dlsym(libHandle_, "Java_xyz_aethersx2_android_NativeLibrary_isBIOSAvailable");
	fnGetBIOSDescription_ = dlsym(libHandle_, "Java_xyz_aethersx2_android_NativeLibrary_getBIOSDescription");
	fnHandleControllerAxisEvent_ = dlsym(libHandle_, "Java_xyz_aethersx2_android_NativeLibrary_handleControllerAxisEvent");
	fnHandleControllerButtonEvent_ = dlsym(libHandle_, "Java_xyz_aethersx2_android_NativeLibrary_handleControllerButtonEvent");
	if (!fnHandleControllerButtonEvent_) fnHandleControllerButtonEvent_ = dlsym(libHandle_, "Java_kr_co_iefriends_pcsx2_NativeApp_setPadButton");

	return true;
#else
	return false;
#endif
}

Path PS2Core::GetDefaultBIOSDirectory() const {
	Path memStick = g_Config.memStickDirectory;
	if (memStick.empty()) {
		memStick = g_Config.internalDataDirectory;
	}
	return memStick / "PS2/bios";
}

bool PS2Core::IsBIOSAvailable() const {
	if (!currentBiosPath_.empty() && File::Exists(currentBiosPath_)) {
		return true;
	}
	if (!g_Config.ps2BiosPath.empty() && File::Exists(g_Config.ps2BiosPath)) {
		return true;
	}

	Path defaultDir = GetDefaultBIOSDirectory();
	if (File::Exists(defaultDir)) {
		std::vector<File::FileInfo> files;
		File::GetFilesInDir(defaultDir, &files, "bin:rom:ROM:BIN:");
		if (!files.empty()) {
			return true;
		}
	}
	return false;
}

Path PS2Core::GetCurrentBIOSPath() const {
	if (!currentBiosPath_.empty()) {
		return currentBiosPath_;
	}
	return g_Config.ps2BiosPath;
}

void PS2Core::SetBIOSPath(const Path &path) {
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	currentBiosPath_ = path;
	g_Config.ps2BiosPath = path;
}

bool PS2Core::ValidateBIOS(const Path &biosPath, BIOSInfo *outInfo) {
	if (!File::Exists(biosPath)) {
		return false;
	}

	File::FileInfo info;
	if (!File::GetFileInfo(biosPath, &info)) {
		return false;
	}

	if (info.size < 512 * 1024) {
		return false;
	}

	if (outInfo) {
		outInfo->path = biosPath;
		outInfo->size = info.size;
		outInfo->description = biosPath.GetFilename();
		outInfo->isValid = true;

		std::string filename = biosPath.GetFilename();
		std::string lowerName = filename;
		std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

		if (lowerName.find("usa") != std::string::npos || lowerName.find("u") != std::string::npos) {
			outInfo->region = "USA";
		} else if (lowerName.find("europe") != std::string::npos || lowerName.find("e") != std::string::npos) {
			outInfo->region = "Europe";
		} else if (lowerName.find("japan") != std::string::npos || lowerName.find("j") != std::string::npos) {
			outInfo->region = "Japan";
		} else {
			outInfo->region = "Unknown";
		}
	}

	return true;
}

std::vector<BIOSInfo> PS2Core::ScanBIOSDirectory(const Path &dir) {
	std::vector<BIOSInfo> list;
	if (!File::Exists(dir)) {
		return list;
	}

	std::vector<File::FileInfo> files;
	File::GetFilesInDir(dir, &files, "bin:rom:ROM:BIN:gz:");
	for (const auto &file : files) {
		if (file.isDirectory) {
			continue;
		}
		BIOSInfo info;
		if (ValidateBIOS(file.fullName, &info)) {
			list.push_back(info);
		}
	}
	return list;
}

bool PS2Core::ImportBIOS(const Path &sourcePath, Path *outDestPath) {
	if (!ValidateBIOS(sourcePath)) {
		lastError_ = "Selected file is not a valid PS2 BIOS.";
		return false;
	}

	Path biosDir = GetDefaultBIOSDirectory();
	File::CreateFullPath(biosDir);

	Path destFile = biosDir / sourcePath.GetFilename();
	if (sourcePath != destFile) {
		if (!File::Copy(sourcePath, destFile)) {
			lastError_ = "Failed to copy BIOS file to " + destFile.ToString();
			return false;
		}
	}

	SetBIOSPath(destFile);
	if (outDestPath) {
		*outDestPath = destFile;
	}
	INFO_LOG(Log::System, "PS2Core: Imported BIOS from %s to %s", sourcePath.c_str(), destFile.c_str());
	return true;
}

PS2GPUBackend PS2Core::GetRenderer() const {
	return (PS2GPUBackend)g_Config.iPS2GPUBackend;
}

void PS2Core::SetRenderer(PS2GPUBackend renderer) {
	g_Config.iPS2GPUBackend = (int)renderer;
	WriteDefaultConfiguration();
}

void PS2Core::WriteDefaultConfiguration() {
	Path configDir = dataDir_.empty() ? (GetSysDirectory(DIRECTORY_SYSTEM) / "PS2") : dataDir_;
	Path inisDir = configDir / "inis";
	File::CreateFullPath(inisDir);

	int rendererVal = PS2GPUBackendToGSRenderer((PS2GPUBackend)g_Config.iPS2GPUBackend);

	Path iniFiles[] = {
		inisDir / "PCSX2.ini",
		inisDir / "AetherSX2.ini",
	};

	for (const auto &iniPath : iniFiles) {
		std::string content;
		if (File::Exists(iniPath)) {
			File::ReadTextFileToString(iniPath, &content);
		}

		if (content.find("[EmuCore/GS]") == std::string::npos) {
			content += StringFromFormat("\n[EmuCore/GS]\nRenderer = %d\n", rendererVal);
		} else {
			size_t gsPos = content.find("[EmuCore/GS]");
			size_t nextSec = content.find("[", gsPos + 12);
			size_t endPos = (nextSec == std::string::npos) ? content.size() : nextSec;
			std::string section = content.substr(gsPos, endPos - gsPos);

			size_t renPos = section.find("Renderer");
			if (renPos != std::string::npos) {
				size_t lineEnd = section.find("\n", renPos);
				if (lineEnd == std::string::npos) {
					lineEnd = section.size();
				}
				section.replace(renPos, lineEnd - renPos, StringFromFormat("Renderer = %d", rendererVal));
			} else {
				section += StringFromFormat("Renderer = %d\n", rendererVal);
			}
			content.replace(gsPos, endPos - gsPos, section);
		}

		File::WriteStringToFile(true, content, iniPath);
	}

	INFO_LOG(Log::System, "PS2Core: Rendering backend configured to %s (Renderer=%d)",
		PS2GPUBackendToString((PS2GPUBackend)g_Config.iPS2GPUBackend), rendererVal);
}

bool PS2Core::Boot(const Path &gamePath, const Path &biosPath) {
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	Path bios = biosPath.empty() ? GetCurrentBIOSPath() : biosPath;
	if (bios.empty() || !File::Exists(bios)) {
		lastError_ = "Cannot boot PS2: BIOS file not found. Please configure a PS2 BIOS.";
		ERROR_LOG(Log::System, "%s", lastError_.c_str());
		return false;
	}

	bool isBiosBoot = gamePath.empty() || gamePath.ToString() == "BOOT_PS2_BIOS" || gamePath.ToString() == "ps2_bios";
	if (!isBiosBoot && !File::Exists(gamePath)) {
		lastError_ = "Cannot boot PS2 game: File not found: " + gamePath.ToString();
		ERROR_LOG(Log::System, "%s", lastError_.c_str());
		return false;
	}

	WriteDefaultConfiguration();

	if (vmThread_.joinable()) {
		if (fnStopVMThreadLoop_) {
#if defined(__ANDROID__)
			if (gJvm) {
				JNIEnv *env = nullptr;
				gJvm->AttachCurrentThread(&env, nullptr);
				typedef void (*StopFn)(JNIEnv *, jclass);
				((StopFn)fnStopVMThreadLoop_)(env, nullptr);
				gJvm->DetachCurrentThread();
			} else
#endif
			{
				typedef void (*StopFn)(void *, void *);
				((StopFn)fnStopVMThreadLoop_)(nullptr, nullptr);
			}
		}
		vmThread_.join();
	}

	state_ = CoreState::RUNNING;
	INFO_LOG(Log::System, "PS2Core: Booting PS2 %s with BIOS '%s' on %s renderer",
		isBiosBoot ? "BIOS" : gamePath.c_str(), bios.c_str(), PS2GPUBackendToString((PS2GPUBackend)g_Config.iPS2GPUBackend));

	if (fnRunVMThread_) {
		std::string pathStr = isBiosBoot ? "" : gamePath.ToString();
		vmThread_ = std::thread([this, pathStr]() {
#if defined(__ANDROID__)
			if (gJvm) {
				JNIEnv *env = nullptr;
				gJvm->AttachCurrentThread(&env, nullptr);
				jstring jpath = env->NewStringUTF(pathStr.c_str());
				typedef jboolean (*RunVMFn)(JNIEnv *, jclass, jstring);
				((RunVMFn)fnRunVMThread_)(env, nullptr, jpath);
				env->DeleteLocalRef(jpath);
				gJvm->DetachCurrentThread();
			} else
#endif
			{
				typedef bool (*RunVMFn)(void *, void *, const char *);
				((RunVMFn)fnRunVMThread_)(nullptr, nullptr, pathStr.c_str());
			}
			INFO_LOG(Log::System, "PS2Core: VM thread terminated.");
		});
	}

	return true;
}

void PS2Core::Stop(bool waitForShutdown) {
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	if (fnStopVMThreadLoop_) {
#if defined(__ANDROID__)
		if (gJvm) {
			JNIEnv *env = nullptr;
			gJvm->AttachCurrentThread(&env, nullptr);
			typedef void (*StopFn)(JNIEnv *, jclass);
			((StopFn)fnStopVMThreadLoop_)(env, nullptr);
			gJvm->DetachCurrentThread();
		} else
#endif
		{
			typedef void (*StopFn)(void *, void *);
			((StopFn)fnStopVMThreadLoop_)(nullptr, nullptr);
		}
	}
	if (waitForShutdown && vmThread_.joinable()) {
		vmThread_.join();
	}
	state_ = CoreState::STOPPED;
	INFO_LOG(Log::System, "PS2Core: Emulation stopped.");
}

void PS2Core::Pause(bool paused) {
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	if (state_ == CoreState::RUNNING && paused) {
		state_ = CoreState::PAUSED;
	} else if (state_ == CoreState::PAUSED && !paused) {
		state_ = CoreState::RUNNING;
	}
}

bool PS2Core::IsPaused() const {
	return state_ == CoreState::PAUSED;
}

void PS2Core::Reset() {
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	INFO_LOG(Log::System, "PS2Core: Resetting VM.");
}

void PS2Core::SetSurface(void *surface, int width, int height, float scale) {
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	INFO_LOG(Log::G3D, "PS2Core: Surface updated: %p (%dx%d, scale=%.2f)", surface, width, height, scale);
}

void PS2Core::HandleAxis(int controller, int axis, float value) {
#if defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	if (!libHandle_ || !fnHandleControllerAxisEvent_) {
		return;
	}
#if defined(__ANDROID__)
	if (gJvm) {
		JNIEnv *env = nullptr;
		gJvm->AttachCurrentThread(&env, nullptr);
		typedef void (*AxisFn)(JNIEnv *, jclass, jint, jint, jfloat);
		((AxisFn)fnHandleControllerAxisEvent_)(env, nullptr, controller, axis, value);
		gJvm->DetachCurrentThread();
		return;
	}
#endif
	typedef void (*AxisFn)(void *, void *, int, int, float);
	((AxisFn)fnHandleControllerAxisEvent_)(nullptr, nullptr, controller, axis, value);
#endif
}

void PS2Core::HandleButton(int controller, int button, bool down) {
#if defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
	std::lock_guard<std::recursive_mutex> lock(mutex_);
	if (!libHandle_ || !fnHandleControllerButtonEvent_) {
		return;
	}
#if defined(__ANDROID__)
	if (gJvm) {
		JNIEnv *env = nullptr;
		gJvm->AttachCurrentThread(&env, nullptr);
		typedef void (*BtnFn)(JNIEnv *, jclass, jint, jint, jboolean);
		((BtnFn)fnHandleControllerButtonEvent_)(env, nullptr, controller, button, down);
		gJvm->DetachCurrentThread();
		return;
	}
#endif
	typedef void (*BtnFn)(void *, void *, int, int, bool);
	((BtnFn)fnHandleControllerButtonEvent_)(nullptr, nullptr, controller, button, down);
#endif
}

} // namespace PS2
