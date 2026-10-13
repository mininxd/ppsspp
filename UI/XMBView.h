// Copyright (c) 2013- PPSSPP Project.

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

#include <functional>
#include <string>
#include <vector>

#include "Common/Common.h"
#include "Common/File/Path.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/UI/View.h"
#include "Common/UI/ViewGroup.h"
#include "Common/UI/Context.h"
#include "Common/Input/KeyCodes.h"
#include "Common/Input/InputState.h"

struct XMBItem {
	std::string title;
	std::string subTitle;
	Path gamePath;
	bool isGame = false;
	ImageID icon = ImageID::invalid();
	std::function<void()> onSelect;
	std::function<void()> onHold;
};

struct XMBCategory {
	std::string title;
	ImageID icon = ImageID::invalid();
	std::vector<XMBItem> items;
	int selectedItem = 0;
	float animItemPos = 0.0f;
};

class XMBView : public UI::View {
public:
	XMBView(UI::LayoutParams *layoutParams = nullptr);

	void SetCategories(std::vector<XMBCategory> categories);
	void SetSelectedCategory(int catIndex);
	int GetSelectedCategory() const { return selectedCategory_; }
	int GetSelectedItem() const;
	const XMBItem *GetActiveItem() const;

	bool Key(const KeyInput &key) override;
	bool Touch(const TouchInput &touch) override;
	void Update() override;
	void Draw(UIContext &dc) override;

	void GetContentDimensions(const UIContext &dc, float &w, float &h) const override;
	bool CanBeFocused() const override { return true; }

	std::function<void(const Path &gamePath)> onHighlightGame;
	std::function<void()> onClearHighlight;

private:
	void ChangeCategory(int delta);
	void ChangeItem(int delta);
	void TriggerSelect();
	void TriggerHold();
	void UpdateHighlight();

	void DrawCategoryBar(UIContext &dc, const Bounds &bounds, float anchorX, float catY);
	void DrawVerticalMenu(UIContext &dc, const Bounds &bounds, float anchorX, float catY);
	void DrawStatusBar(UIContext &dc, const Bounds &bounds);
	void DrawControlHints(UIContext &dc, const Bounds &bounds);

	std::vector<XMBCategory> categories_;
	int selectedCategory_ = 0;
	float animCatPos_ = 0.0f;

	bool isDragging_ = false;
	float dragStartY_ = 0.0f;
	float touchDownX_ = 0.0f;
	float touchDownY_ = 0.0f;
	double touchDownTime_ = 0.0;
};
