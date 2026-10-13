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

#include "UI/XMBView.h"

#include <algorithm>
#include <cmath>
#include <ctime>

#include "Common/Render/DrawBuffer.h"
#include "Common/Render/TextureAtlas.h"
#include "Common/Data/Color/RGBAUtil.h"
#include "Common/Data/Text/I18n.h"
#include "Common/StringUtils.h"
#include "Common/TimeUtil.h"
#include "Common/System/System.h"
#include "Common/UI/Root.h"
#include "Common/UI/Context.h"
#include "Common/GPU/thin3d.h"
#include "Core/Config.h"
#include "UI/GameInfoCache.h"

XMBView::XMBView(UI::LayoutParams *layoutParams) : UI::View(layoutParams) {
}

void XMBView::SetCategories(std::vector<XMBCategory> categories) {
	categories_ = std::move(categories);
	if (selectedCategory_ >= (int)categories_.size()) {
		selectedCategory_ = std::max(0, (int)categories_.size() - 1);
	}
	animCatPos_ = (float)selectedCategory_;
	for (auto &cat : categories_) {
		if (cat.selectedItem >= (int)cat.items.size()) {
			cat.selectedItem = std::max(0, (int)cat.items.size() - 1);
		}
		cat.animItemPos = (float)cat.selectedItem;
	}
	UpdateHighlight();
}

void XMBView::SetSelectedCategory(int catIndex) {
	if (categories_.empty()) {
		return;
	}
	selectedCategory_ = std::clamp(catIndex, 0, (int)categories_.size() - 1);
	UpdateHighlight();
	UI::PlayUISound(UI::UISound::SELECT);
}

int XMBView::GetSelectedItem() const {
	if (selectedCategory_ < 0 || selectedCategory_ >= (int)categories_.size()) {
		return -1;
	}
	return categories_[selectedCategory_].selectedItem;
}

const XMBItem *XMBView::GetActiveItem() const {
	if (selectedCategory_ < 0 || selectedCategory_ >= (int)categories_.size()) {
		return nullptr;
	}
	const auto &cat = categories_[selectedCategory_];
	if (cat.items.empty() || cat.selectedItem < 0 || cat.selectedItem >= (int)cat.items.size()) {
		return nullptr;
	}
	return &cat.items[cat.selectedItem];
}

void XMBView::ChangeCategory(int delta) {
	if (categories_.empty()) {
		return;
	}
	int count = (int)categories_.size();
	selectedCategory_ = (selectedCategory_ + delta + count) % count;
	UpdateHighlight();
	UI::PlayUISound(UI::UISound::SELECT);
}

void XMBView::ChangeItem(int delta) {
	if (selectedCategory_ < 0 || selectedCategory_ >= (int)categories_.size()) {
		return;
	}
	auto &cat = categories_[selectedCategory_];
	if (cat.items.empty()) {
		return;
	}
	int newItem = std::clamp(cat.selectedItem + delta, 0, (int)cat.items.size() - 1);
	if (newItem != cat.selectedItem) {
		cat.selectedItem = newItem;
		UpdateHighlight();
		UI::PlayUISound(UI::UISound::SELECT);
	}
}

void XMBView::TriggerSelect() {
	const XMBItem *item = GetActiveItem();
	if (item && item->onSelect) {
		UI::PlayUISound(UI::UISound::CONFIRM);
		item->onSelect();
	}
}

void XMBView::TriggerHold() {
	const XMBItem *item = GetActiveItem();
	if (item && item->onHold) {
		UI::PlayUISound(UI::UISound::SELECT);
		item->onHold();
	}
}

void XMBView::UpdateHighlight() {
	if (selectedCategory_ < 0 || selectedCategory_ >= (int)categories_.size()) {
		if (onClearHighlight) {
			onClearHighlight();
		}
		return;
	}
	const auto &cat = categories_[selectedCategory_];
	if (cat.items.empty() || cat.selectedItem < 0 || cat.selectedItem >= (int)cat.items.size()) {
		if (onClearHighlight) {
			onClearHighlight();
		}
		return;
	}
	const auto &item = cat.items[cat.selectedItem];
	if (item.isGame && !item.gamePath.empty()) {
		if (onHighlightGame) {
			onHighlightGame(item.gamePath);
		}
	} else {
		if (onClearHighlight) {
			onClearHighlight();
		}
	}
}

void XMBView::Update() {
	UI::View::Update();

	// Smooth animation for category and item positions
	float catDiff = (float)selectedCategory_ - animCatPos_;
	animCatPos_ += catDiff * 0.25f;

	for (auto &cat : categories_) {
		float itemDiff = (float)cat.selectedItem - cat.animItemPos;
		cat.animItemPos += itemDiff * 0.25f;
	}
}

bool XMBView::Key(const KeyInput &key) {
	if (key.flags & (KeyInputFlags::DOWN | KeyInputFlags::IS_REPEAT)) {
		if (key.keyCode == NKCODE_DPAD_LEFT || UI::IsTabLeftKey(key)) {
			ChangeCategory(-1);
			return true;
		}
		if (key.keyCode == NKCODE_DPAD_RIGHT || UI::IsTabRightKey(key)) {
			ChangeCategory(1);
			return true;
		}
		if (key.keyCode == NKCODE_DPAD_UP) {
			ChangeItem(-1);
			return true;
		}
		if (key.keyCode == NKCODE_DPAD_DOWN) {
			ChangeItem(1);
			return true;
		}
		if (UI::IsAcceptKey(key) || key.keyCode == NKCODE_DPAD_CENTER || key.keyCode == NKCODE_ENTER || key.keyCode == NKCODE_NUMPAD_ENTER) {
			TriggerSelect();
			return true;
		}
		if (UI::IsInfoKey(key)) {
			TriggerHold();
			return true;
		}
	}
	return UI::View::Key(key);
}

bool XMBView::Touch(const TouchInput &touch) {
	if (touch.flags & TouchInputFlags::WHEEL) {
		if (touch.y > 0) {
			ChangeItem(-1);
		} else if (touch.y < 0) {
			ChangeItem(1);
		}
		return true;
	}

	const float anchorX = bounds_.x + std::clamp(bounds_.w * 0.22f, 110.0f, 220.0f);
	const float catY = bounds_.y + std::clamp(bounds_.h * 0.25f, 75.0f, 150.0f);
	const float catSpacing = 88.0f;
	const float focusY = catY + 70.0f;
	const float itemSpacing = 48.0f;

	if (touch.flags & TouchInputFlags::DOWN) {
		touchDownX_ = touch.x;
		touchDownY_ = touch.y;
		touchDownTime_ = time_now_d();
		isDragging_ = false;
		return true;
	}

	if (touch.flags & TouchInputFlags::MOVE) {
		if (std::abs(touch.y - touchDownY_) > 12.0f) {
			isDragging_ = true;
		}
		return true;
	}

	if (touch.flags & TouchInputFlags::UP) {
		if (!isDragging_ && (time_now_d() - touchDownTime_ < 0.6)) {
			// Check if click was on a category icon
			for (size_t i = 0; i < categories_.size(); i++) {
				float cx = anchorX + (float)((int)i - animCatPos_) * catSpacing;
				if (std::abs(touch.x - cx) < 36.0f && std::abs(touch.y - catY) < 36.0f) {
					SetSelectedCategory((int)i);
					return true;
				}
			}

			// Check if click was on vertical items of active category
			if (selectedCategory_ >= 0 && selectedCategory_ < (int)categories_.size()) {
				auto &cat = categories_[selectedCategory_];
				for (size_t j = 0; j < cat.items.size(); j++) {
					float iy = focusY + ((float)j - cat.animItemPos) * itemSpacing;
					if (std::abs(touch.y - iy) < itemSpacing * 0.5f && touch.x >= anchorX - 32.0f && touch.x <= bounds_.x2() - 20.0f) {
						if ((int)j == cat.selectedItem) {
							TriggerSelect();
						} else {
							cat.selectedItem = (int)j;
							UpdateHighlight();
							UI::PlayUISound(UI::UISound::SELECT);
						}
						return true;
					}
				}
			}
		}
		isDragging_ = false;
		return true;
	}

	return UI::View::Touch(touch);
}

void XMBView::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = 480.0f;
	h = 272.0f;
}

void XMBView::Draw(UIContext &dc) {
	if (categories_.empty()) {
		return;
	}

	const Bounds &bounds = bounds_;
	const float anchorX = bounds.x + std::clamp(bounds.w * 0.22f, 110.0f, 220.0f);
	const float catY = bounds.y + std::clamp(bounds.h * 0.25f, 75.0f, 150.0f);

	DrawStatusBar(dc, bounds);
	DrawCategoryBar(dc, bounds, anchorX, catY);
	DrawVerticalMenu(dc, bounds, anchorX, catY);
	DrawControlHints(dc, bounds);
}

void XMBView::DrawStatusBar(UIContext &dc, const Bounds &bounds) {
	// Top Left PPSSPP Branding
	dc.Draw()->DrawImage(ImageID("I_ICON"), bounds.x + 24.0f, bounds.y + 22.0f, 0.7f, 0xFFFFFFFF, ALIGN_CENTER);
	dc.SetFontStyle(dc.GetTheme().uiFontSmall);
	dc.DrawText("PPSSPP", bounds.x + 42.0f, bounds.y + 22.0f, 0xDDFFFFFF, ALIGN_VCENTER);

	// Top Right Clock & Date
	time_t rawtime;
	time(&rawtime);
	struct tm timeinfo;
#if defined(_WIN32)
	localtime_s(&timeinfo, &rawtime);
#else
	localtime_r(&rawtime, &timeinfo);
#endif
	char timeBuf[32];
	strftime(timeBuf, sizeof(timeBuf), "%H:%M", &timeinfo);
	char dateBuf[32];
	strftime(dateBuf, sizeof(dateBuf), "%m/%d", &timeinfo);

	float rightX = bounds.x2() - 24.0f;
	dc.SetFontStyle(dc.GetTheme().uiFont);
	dc.DrawText(timeBuf, rightX, bounds.y + 22.0f, 0xFFFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
	float tw, th;
	dc.MeasureText(dc.GetFontStyle(), 1.0f, 1.0f, timeBuf, &tw, &th);

	dc.SetFontStyle(dc.GetTheme().uiFontSmall);
	dc.DrawText(dateBuf, rightX - tw - 16.0f, bounds.y + 22.0f, 0xAAFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
	float dw, dh;
	dc.MeasureText(dc.GetFontStyle(), 1.0f, 1.0f, dateBuf, &dw, &dh);

	int battery = System_GetPropertyInt(SYSPROP_BATTERY_PERCENTAGE);
	if (battery >= 0) {
		std::string batStr = StringFromFormat("%d%%", battery);
		dc.DrawText(batStr, rightX - tw - dw - 32.0f, bounds.y + 22.0f, 0xAAFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
	}
	dc.SetFontStyle(dc.GetTheme().uiFont);
}

void XMBView::DrawCategoryBar(UIContext &dc, const Bounds &bounds, float anchorX, float catY) {
	const float catSpacing = 88.0f;

	dc.PushScissor(bounds);

	for (size_t i = 0; i < categories_.size(); i++) {
		const auto &cat = categories_[i];
		float cx = anchorX + ((float)i - animCatPos_) * catSpacing;

		if (cx < bounds.x - 60.0f || cx > bounds.x2() + 60.0f) {
			continue;
		}

		if ((int)i == selectedCategory_) {
			// Selected category: Bold, prominent, bright white, category title above
			dc.Draw()->DrawImage(cat.icon, cx, catY, 1.35f, 0xFFFFFFFF, ALIGN_CENTER);

			dc.SetFontStyle(dc.GetTheme().uiFont);
			dc.DrawTextShadow(cat.title, cx, catY - 32.0f, 0xFFFFFFFF, ALIGN_HCENTER);
		} else {
			// Inactive categories: Smaller, dimmed icons along the horizontal axis
			dc.Draw()->DrawImage(cat.icon, cx, catY, 0.85f, colorAlpha(0xFFFFFFFF, 0.40f), ALIGN_CENTER);
		}
	}

	dc.PopScissor();
}

void XMBView::DrawVerticalMenu(UIContext &dc, const Bounds &bounds, float anchorX, float catY) {
	if (selectedCategory_ < 0 || selectedCategory_ >= (int)categories_.size()) {
		return;
	}

	const auto &cat = categories_[selectedCategory_];
	if (cat.items.empty()) {
		return;
	}

	const float focusY = catY + 70.0f;
	const float itemSpacing = 48.0f;

	dc.PushScissor(bounds);

	for (size_t j = 0; j < cat.items.size(); j++) {
		const auto &item = cat.items[j];
		float iy = focusY + ((float)j - cat.animItemPos) * itemSpacing;

		if (iy < bounds.y - 45.0f || iy > bounds.y2() + 45.0f) {
			continue;
		}

		const bool isSelected = ((int)j == cat.selectedItem);

		if (!isSelected) {
			// Unselected / unhighlighted: ONLY THE ICON IS DRAWN! NO TEXT!
			if (item.isGame && !item.gamePath.empty()) {
				std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), item.gamePath, GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON);
				if (ginfo && ginfo->Ready(GameInfoFlags::ICON) && ginfo->icon.texture) {
					Draw::Texture *texture = ginfo->icon.texture;
					float tw = (float)texture->Width();
					float th = (float)texture->Height();
					float h = 24.0f;
					float w = th > 0 ? (h * tw / th) : 42.0f;
					w = std::min(w, 52.0f);

					dc.Draw()->Flush();
					dc.GetDrawContext()->BindTexture(0, texture);
					dc.Draw()->DrawTexRect(anchorX - w * 0.5f, iy - h * 0.5f, anchorX + w * 0.5f, iy + h * 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, colorAlpha(0xFFFFFFFF, 0.65f));
					dc.Draw()->Flush();
					dc.RebindTexture();
				} else {
					ImageID fallback = item.icon.isValid() ? item.icon : ImageID("I_UMD");
					dc.Draw()->DrawImage(fallback, anchorX, iy, 0.85f, colorAlpha(0xFFFFFFFF, 0.65f), ALIGN_CENTER);
				}
			} else {
				ImageID iconID = item.icon.isValid() ? item.icon : ImageID("I_APP");
				dc.Draw()->DrawImage(iconID, anchorX, iy, 0.85f, colorAlpha(0xFFFFFFFF, 0.65f), ALIGN_CENTER);
			}
		} else {
			// Selected / highlighted: BOTH ICON AND TEXT ARE SHOWN! Plus glowing selection pill!
			float pillW = std::min(bounds.w - (anchorX - 28.0f) - 30.0f, 440.0f);
			Bounds hlBounds(anchorX - 28.0f, iy - 22.0f, pillW, 44.0f);

			// Background highlight bar
			dc.FillRect(UI::Drawable(0x35FFFFFF), hlBounds);

			// Accent strip on left edge
			Bounds accent(hlBounds.x, hlBounds.y, 3.5f, hlBounds.h);
			dc.FillRect(UI::Drawable(0xFFFFFFFF), accent);

			// Drop shadow
			dc.Draw()->Flush();
			dc.Draw()->DrawImage4Grid(dc.GetTheme().dropShadow4Grid, hlBounds.x - 3, hlBounds.y - 3, hlBounds.x2() + 3, hlBounds.y2() + 3, 0x40000000, 1.0f);
			dc.Draw()->Flush();

			// Selected item icon
			std::string displayTitle = item.title;
			std::string displaySubTitle = item.subTitle;

			if (item.isGame && !item.gamePath.empty()) {
				std::shared_ptr<GameInfo> ginfo = g_gameInfoCache->GetInfo(dc.GetDrawContext(), item.gamePath, GameInfoFlags::PARAM_SFO | GameInfoFlags::ICON);
				if (ginfo && ginfo->Ready(GameInfoFlags::PARAM_SFO)) {
					std::string currentTitle = ginfo->GetTitle();
					if (!currentTitle.empty()) {
						displayTitle = ReplaceAll(currentTitle, "\n", " ");
					}
					if (g_Config.bShowIDOnGameIcon && !ginfo->id_version.empty()) {
						displaySubTitle = ginfo->id_version;
					}
				}

				if (ginfo && ginfo->Ready(GameInfoFlags::ICON) && ginfo->icon.texture) {
					Draw::Texture *texture = ginfo->icon.texture;
					float tw = (float)texture->Width();
					float th = (float)texture->Height();
					float h = 30.0f;
					float w = th > 0 ? (h * tw / th) : 54.0f;
					w = std::min(w, 64.0f);

					dc.Draw()->Flush();
					dc.Draw()->DrawImage4Grid(dc.GetTheme().dropShadow4Grid, anchorX - w * 0.5f - 4, iy - h * 0.5f - 4, anchorX + w * 0.5f + 4, iy + h * 0.5f + 4, 0x60000000, 1.0f);
					dc.GetDrawContext()->BindTexture(0, texture);
					dc.Draw()->DrawTexRect(anchorX - w * 0.5f, iy - h * 0.5f, anchorX + w * 0.5f, iy + h * 0.5f, 0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF);
					dc.Draw()->Flush();
					dc.RebindTexture();
				} else {
					ImageID fallback = item.icon.isValid() ? item.icon : ImageID("I_UMD");
					dc.Draw()->DrawImage(fallback, anchorX, iy, 1.15f, 0xFFFFFFFF, ALIGN_CENTER);
				}
			} else {
				ImageID iconID = item.icon.isValid() ? item.icon : ImageID("I_APP");
				dc.Draw()->DrawImage(iconID, anchorX, iy, 1.15f, 0xFFFFFFFF, ALIGN_CENTER);
			}

			// Selected item text label (displayed to the right of the icon)
			float textX = anchorX + 38.0f;
			dc.SetFontStyle(dc.GetTheme().uiFont);
			dc.DrawTextShadow(displayTitle, textX, iy, 0xFFFFFFFF, ALIGN_VCENTER);

			if (!displaySubTitle.empty()) {
				float tw, th;
				dc.MeasureText(dc.GetFontStyle(), 1.0f, 1.0f, displayTitle, &tw, &th);
				dc.SetFontStyle(dc.GetTheme().uiFontSmall);
				dc.DrawText(displaySubTitle, textX + tw + 14.0f, iy, colorAlpha(0xFFFFFFFF, 0.65f), ALIGN_VCENTER);
				dc.SetFontStyle(dc.GetTheme().uiFont);
			}
		}
	}

	dc.PopScissor();
}

void XMBView::DrawControlHints(UIContext &dc, const Bounds &bounds) {
	auto di = GetI18NCategory(I18NCat::DIALOG);

	float hintY = bounds.y2() - 20.0f;
	float curX = bounds.x2() - 30.0f;

	dc.SetFontStyle(dc.GetTheme().uiFontSmall);

	// Triangle: Options
	const XMBItem *active = GetActiveItem();
	if (active && active->onHold) {
		std::string_view optText = di->T("Options", "Options");
		float ow, oh;
		dc.MeasureText(dc.GetFontStyle(), 1.0f, 1.0f, optText, &ow, &oh);
		dc.DrawText(optText, curX, hintY, 0xCCFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
		curX -= ow + 8.0f;
		dc.Draw()->DrawImage(ImageID("I_TRIANGLE"), curX, hintY, 0.75f, 0xCCFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
		curX -= 28.0f;
	}

	// Cross: Enter
	std::string_view enterText = di->T("Enter", "Enter");
	float ew, eh;
	dc.MeasureText(dc.GetFontStyle(), 1.0f, 1.0f, enterText, &ew, &eh);
	dc.DrawText(enterText, curX, hintY, 0xCCFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);
	curX -= ew + 8.0f;
	dc.Draw()->DrawImage(ImageID("I_CROSS"), curX, hintY, 0.75f, 0xCCFFFFFF, ALIGN_RIGHT | ALIGN_VCENTER);

	dc.SetFontStyle(dc.GetTheme().uiFont);
}
