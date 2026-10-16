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

#pragma once

#include <string>
#include <vector>
#include <mutex>
#include "Common/File/Path.h"

namespace PS2 {

enum class CoreState {
	UNINITIALIZED,
	INITIALIZED,
	RUNNING,
	PAUSED,
	STOPPED,
	ERROR_STATE,
};

enum class PS2GPUBackend : int {
	VULKAN = 0,
	OPENGL = 1,
	SOFTWARE = 2,
};

inline int PS2GPUBackendToGSRenderer(PS2GPUBackend backend) {
	switch (backend) {
	case PS2GPUBackend::OPENGL: return 12;
	case PS2GPUBackend::SOFTWARE: return 13;
	case PS2GPUBackend::VULKAN:
	default: return 14;
	}
}

inline const char *PS2GPUBackendToString(PS2GPUBackend backend) {
	switch (backend) {
	case PS2GPUBackend::OPENGL: return "OpenGL";
	case PS2GPUBackend::SOFTWARE: return "Software";
	case PS2GPUBackend::VULKAN:
	default: return "Vulkan";
	}
}

struct BIOSInfo {
	Path path;
	std::string description;
	std::string region;
	std::string version;
	uint64_t size = 0;
	bool isValid = false;
};

class PS2Core {
public:
	static PS2Core &GetInstance();

	bool Initialize(const Path &dataDir, const Path &cacheDir);
	void Shutdown();

	bool IsLibraryLoaded() const;
	bool LoadNativeLibrary();

	bool IsBIOSAvailable() const;
	Path GetCurrentBIOSPath() const;
	void SetBIOSPath(const Path &path);
	Path GetDefaultBIOSDirectory() const;
	std::vector<BIOSInfo> ScanBIOSDirectory(const Path &dir);
	bool ValidateBIOS(const Path &biosPath, BIOSInfo *outInfo = nullptr);
	bool ImportBIOS(const Path &sourcePath, Path *outDestPath = nullptr);

	PS2GPUBackend GetRenderer() const;
	void SetRenderer(PS2GPUBackend renderer);
	void WriteDefaultConfiguration();

	bool Boot(const Path &gamePath, const Path &biosPath = Path());
	void Stop(bool waitForShutdown = true);
	void Pause(bool paused);
	bool IsPaused() const;
	void Reset();

	CoreState GetState() const { return state_; }
	const std::string &GetLastError() const { return lastError_; }

	void SetSurface(void *surface, int width, int height, float scale);
	void HandleAxis(int controller, int axis, float value);
	void HandleButton(int controller, int button, bool down);

private:
	PS2Core();
	~PS2Core();

	CoreState state_ = CoreState::UNINITIALIZED;
	std::string lastError_;
	mutable std::mutex mutex_;
	void *libHandle_ = nullptr;
	Path dataDir_;
	Path cacheDir_;
	Path currentBiosPath_;

	void *fnInitialize_ = nullptr;
	void *fnRunVMThread_ = nullptr;
	void *fnStopVMThreadLoop_ = nullptr;
	void *fnPauseVM_ = nullptr;
	void *fnIsVMPaused_ = nullptr;
	void *fnResetVM_ = nullptr;
	void *fnChangeSurface_ = nullptr;
	void *fnChangeDisc_ = nullptr;
	void *fnIsBIOSAvailable_ = nullptr;
	void *fnGetBIOSDescription_ = nullptr;
	void *fnHandleControllerAxisEvent_ = nullptr;
	void *fnHandleControllerButtonEvent_ = nullptr;
};

} // namespace PS2
