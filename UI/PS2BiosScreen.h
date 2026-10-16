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

#include "Common/File/Path.h"
#include "Common/UI/UIScreen.h"
#include "UI/BaseScreens.h"
#include "Core/PS2/PS2Core.h"

class PS2BiosScreen : public UIBaseDialogScreen {
public:
	explicit PS2BiosScreen(bool initialSetup = false);
	~PS2BiosScreen() = default;

	const char *tag() const override { return "PS2Bios"; }

protected:
	void CreateViews() override;
	void update() override;

private:
	void OnImportBiosFile(UI::EventParams &e);
	void OnBrowseBiosFolder(UI::EventParams &e);
	void OnSelectBios(const Path &path);
	void OnContinue(UI::EventParams &e);
	void OnSkip(UI::EventParams &e);

	bool initialSetup_ = false;
	std::vector<PS2::BIOSInfo> availableBioses_;
	std::string statusMessage_;
};
