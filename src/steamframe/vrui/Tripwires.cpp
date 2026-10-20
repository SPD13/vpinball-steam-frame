// license:GPLv3+

// Steam Frame Edition: the new VR UI draws upstream's pages from their items. A new kind of item or property added upstream would be skipped by
// the view without anyone noticing, so these checks break the build instead: when one fails, add what is new to the view (and to the
// parity checklist of docs/VR UI.md), then update the check.

#include "core/stdafx.h"
#include "ui/live/ingameui/InGameUIItem.h"

namespace VPX::VRUI
{

using VPX::InGameUI::InGameUIItem;
using VPX::Properties::PropertyDef;
using VPX::Properties::StringPropertyDef;

static_assert(static_cast<int>(InGameUIItem::Type::SaveChanges) == 9, "InGameUIItem::Type changed upstream: update the new VR UI");
static_assert(static_cast<int>(InGameUIItem::LabelType::Markdown) == 3, "InGameUIItem::LabelType changed upstream: update the new VR UI");
static_assert(static_cast<int>(PropertyDef::Type::String) == 4, "PropertyDef::Type changed upstream: update the new VR UI");
static_assert(static_cast<int>(StringPropertyDef::PathType::File) == 2, "StringPropertyDef::PathType changed upstream: update the new VR UI");

}
