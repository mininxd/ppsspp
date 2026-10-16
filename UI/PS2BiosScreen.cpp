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

#include "UI/PS2BiosScreen.h"
#include "UI/MainScreen.h"

#include "Common/UI/UI.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Context.h"
#include "Common/System/System.h"
#include "Common/System/Request.h"
#include "Common/Data/Text/I18n.h"
#include "Common/File/FileUtil.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/PS2/PS2Core.h"

PS2BiosScreen::PS2BiosScreen(bool initialSetup)
	: UIBaseDialogScreen(), initialSetup_(initialSetup) {
}

void PS2BiosScreen::update() {
	UIBaseDialogScreen::update();
}

void PS2BiosScreen::CreateViews() {
	using namespace UI;

	auto di = GetI18NCategory(I18NCat::DIALOG);
	Margins actionMenuMargins(15, 0, 15, 0);

	root_ = new LinearLayout(ORIENT_HORIZONTAL);

	Spacer *spacerColumn = new Spacer(new LinearLayoutParams(20.0f, FILL_PARENT, 0.0f));
	ScrollView *mainColumnScroll = new ScrollView(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	ViewGroup *mainColumn = new LinearLayoutList(ORIENT_VERTICAL);
	mainColumnScroll->Add(mainColumn);

	root_->Add(spacerColumn);
	root_->Add(mainColumnScroll);

	mainColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 12.0f, 0.0f)));

	if (initialSetup_) {
		mainColumn->Add(new TextView("PlayStation 2 Emulation Setup", ALIGN_LEFT, false));
	} else {
		mainColumn->Add(new TextView("PlayStation 2 BIOS Settings", ALIGN_LEFT, false));
	}

	mainColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 12.0f, 0.0f)));

	mainColumn->Add(new TextView(
		"To run PlayStation 2 games, a valid PS2 BIOS (.bin) is required.\n"
		"You can import your BIOS dump now, or skip this step and configure it later in Settings.",
		ALIGN_LEFT, false));

	mainColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 16.0f, 0.0f)));

	ViewGroup *subColumns = new LinearLayoutList(ORIENT_HORIZONTAL);
	mainColumn->Add(subColumns);

	ViewGroup *leftColumn = new LinearLayoutList(ORIENT_VERTICAL, new LinearLayoutParams(1.0f));
	subColumns->Add(leftColumn);

	ViewGroup *rightColumnItems = new LinearLayout(ORIENT_VERTICAL, new LinearLayoutParams(250, FILL_PARENT, actionMenuMargins));
	subColumns->Add(rightColumnItems);

	leftColumn->Add(new ItemHeader("Current BIOS"));

	Path currentPath = PS2::PS2Core::GetInstance().GetCurrentBIOSPath();
	if (!currentPath.empty() && File::Exists(currentPath)) {
		std::string biosText = StringFromFormat("Active BIOS: %s", currentPath.GetFilename().c_str());
		leftColumn->Add(new TextView(biosText, ALIGN_LEFT, false));
		std::string pathText = StringFromFormat("Location: %s", currentPath.ToVisualString().c_str());
		leftColumn->Add(new TextView(pathText, ALIGN_LEFT, false));
	} else {
		leftColumn->Add(new TextView("Status: No BIOS currently configured.", ALIGN_LEFT, false));
	}

	if (!statusMessage_.empty()) {
		leftColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 8.0f, 0.0f)));
		leftColumn->Add(new TextView(statusMessage_, ALIGN_LEFT, false));
	}

	leftColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 12.0f, 0.0f)));
	leftColumn->Add(new ItemHeader("Graphics Backend"));
	leftColumn->Add(new TextView("Renderer: Vulkan (Default)", ALIGN_LEFT, false));

	leftColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 16.0f, 0.0f)));
	leftColumn->Add(new ItemHeader("Import & Select"));

	if (System_GetPropertyBool(SYSPROP_HAS_FILE_BROWSER)) {
		leftColumn->Add(new Choice("Import PS2 BIOS (.bin)...", ImageID("I_FOLDER_OPEN")))->OnClick.Handle(this, &PS2BiosScreen::OnImportBiosFile);
	}

	if (System_GetPropertyBool(SYSPROP_HAS_FOLDER_BROWSER)) {
		leftColumn->Add(new Choice("Choose BIOS Directory...", ImageID("I_FOLDER")))->OnClick.Handle(this, &PS2BiosScreen::OnBrowseBiosFolder);
	}

	Path defaultBiosDir = PS2::PS2Core::GetInstance().GetDefaultBIOSDirectory();
	availableBioses_ = PS2::PS2Core::GetInstance().ScanBIOSDirectory(defaultBiosDir);

	if (!availableBioses_.empty()) {
		leftColumn->Add(new Spacer(new LinearLayoutParams(FILL_PARENT, 12.0f, 0.0f)));
		leftColumn->Add(new ItemHeader("Detected BIOS Dumps"));
		for (const auto &bios : availableBioses_) {
			std::string title = bios.description;
			if (!bios.region.empty()) {
				title += " (" + bios.region + ")";
			}
			Choice *item = leftColumn->Add(new Choice(title, ImageID("I_UMD")));
			item->OnClick.Add([this, path = bios.path](UI::EventParams &) {
				OnSelectBios(path);
			});
		}
	}

	if (initialSetup_) {
		rightColumnItems->Add(new Choice(di->T("Continue"), ImageID("I_CHECK")))->OnClick.Handle(this, &PS2BiosScreen::OnContinue);
		rightColumnItems->Add(new Spacer(12.0f));
		rightColumnItems->Add(new Choice("Skip for now", ImageID("I_ARROW_RIGHT")))->OnClick.Handle(this, &PS2BiosScreen::OnSkip);
	} else {
		rightColumnItems->Add(new Choice(di->T("Back"), ImageID("I_NAVIGATE_BACK")))->OnClick.Handle<UIScreen>(this, &UIScreen::OnBack);
	}
}

void PS2BiosScreen::OnImportBiosFile(UI::EventParams &e) {
	System_BrowseForFile(GetRequesterToken(), "Import PS2 BIOS", BrowseFileType::ANY, [this](std::string_view value, int) {
		if (value.empty()) {
			return;
		}

		Path sourcePath(value);
		Path destPath;
		if (PS2::PS2Core::GetInstance().ImportBIOS(sourcePath, &destPath)) {
			statusMessage_ = StringFromFormat("Successfully imported: %s", destPath.GetFilename().c_str());
			g_Config.bPS2BiosPromptShown = true;
			g_Config.Save("PS2BiosImport");
			RecreateViews();
		} else {
			statusMessage_ = PS2::PS2Core::GetInstance().GetLastError();
			RecreateViews();
		}
	});
}

void PS2BiosScreen::OnBrowseBiosFolder(UI::EventParams &e) {
	Path initialDir = PS2::PS2Core::GetInstance().GetDefaultBIOSDirectory();
	System_BrowseForFolder(GetRequesterToken(), "Choose PS2 BIOS Folder", initialDir, [this](std::string_view value, int) {
		if (value.empty()) {
			return;
		}

		Path folder(value);
		availableBioses_ = PS2::PS2Core::GetInstance().ScanBIOSDirectory(folder);
		if (!availableBioses_.empty()) {
			PS2::PS2Core::GetInstance().SetBIOSPath(availableBioses_[0].path);
			statusMessage_ = StringFromFormat("Found %d BIOS file(s). Selected %s", (int)availableBioses_.size(), availableBioses_[0].description.c_str());
		} else {
			statusMessage_ = "No valid PS2 BIOS found in selected folder.";
		}
		g_Config.Save("PS2BiosFolder");
		RecreateViews();
	});
}

void PS2BiosScreen::OnSelectBios(const Path &path) {
	PS2::PS2Core::GetInstance().SetBIOSPath(path);
	statusMessage_ = StringFromFormat("Active BIOS set to: %s", path.GetFilename().c_str());
	g_Config.bPS2BiosPromptShown = true;
	g_Config.Save("PS2BiosSelect");
	RecreateViews();
}

void PS2BiosScreen::OnContinue(UI::EventParams &e) {
	g_Config.bPS2BiosPromptShown = true;
	g_Config.Save("PS2BiosContinue");
	screenManager()->switchScreen(new MainScreen());
}

void PS2BiosScreen::OnSkip(UI::EventParams &e) {
	g_Config.bPS2BiosPromptShown = true;
	g_Config.Save("PS2BiosSkip");
	screenManager()->switchScreen(new MainScreen());
}
