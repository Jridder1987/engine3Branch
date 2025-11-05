/*
 * InvalidItemHelper.cpp
 *
 *  Created on: 05/11/2025
 *      Author: Codex Assist
 */

#include "server/zone/objects/tangible/InvalidItemHelper.h"

#include "server/zone/objects/tangible/TangibleObject.h"
#include "server/zone/objects/creature/CreatureObject.h"
#include "templates/SharedTangibleObjectTemplate.h"
#include "templates/customization/AssetCustomizationManagerTemplate.h"
#include "templates/customization/CustomizationIdManager.h"
#include "templates/params/RangedIntCustomizationVariable.h"
#include "server/zone/objects/scene/variables/CustomizationVariables.h"
#include "server/zone/objects/scene/SceneObject.h"

#include "engine/util/bytell_hash_map.hpp"
#include "system/lang/String.h"
#include "system/lang/StringBuffer.h"
#include "system/thread/Mutex.h"

using namespace engine::log;
using namespace server::zone::objects::tangible;

namespace {

ska::bytell_hash_set<uint64> gLoggedBadItems;
Mutex gBadItemMutex;

String buildObjectDescriptor(TangibleObject* item) {
	StringBuffer buffer;

	buffer << "0x" << hex << item->getObjectID() << dec;

	String appearance = "";
	SharedObjectTemplate* templ = item->getObjectTemplate();
	if (templ != nullptr) {
		appearance = templ->getAppearanceFilename();
	}

	if (!item->getDisplayedName().isEmpty()) {
		buffer << " \"" << item->getDisplayedName() << "\"";
	} else if (!item->getCustomObjectName().isEmpty()) {
		buffer << " \"" << item->getCustomObjectName() << "\"";
	}

	if (!appearance.isEmpty()) {
		buffer << " [" << appearance << "]";
	}

	return buffer.toString();
}

bool validateCustomization(TangibleObject* item, String& outReason) {
	outReason = "";

	CustomizationVariables vars = item->getCustomizationVariables();
	if (vars.getSize() == 0) {
		return true;
	}

	SharedTangibleObjectTemplate* tmpl = dynamic_cast<SharedTangibleObjectTemplate*>(item->getObjectTemplate());
	if (tmpl == nullptr) {
		return true;
	}

	const String& appearance = tmpl->getAppearanceFilename();
	if (appearance.isEmpty()) {
		return true;
	}

	VectorMap<String, Reference<CustomizationVariable*> > allowed;
	try {
		AssetCustomizationManagerTemplate::instance()->getCustomizationVariables(appearance.hashCode(), allowed, false);
	} catch (const Exception& e) {
		outReason = "failed to resolve customization variables (" + e.getMessage() + ")";
		return false;
	}

	if (allowed.size() == 0) {
		// Nothing to validate against.
		return true;
	}

	for (int i = 0; i < vars.getSize(); ++i) {
		uint8 key;
		int16 value;
		vars.getVariable(i, key, value);

		String varName = CustomizationIdManager::instance()->getCustomizationVariable(key);
		if (varName.isEmpty()) {
			StringBuffer buf;
			buf << "unknown customization id 0x";
			buf << hex << (int) key << dec << " detected";
			outReason = buf.toString();
			return false;
		}

		if (!allowed.contains(varName)) {
			outReason = "customization '" + varName + "' not supported by appearance '" + appearance + "'";
			return false;
		}

		Reference<CustomizationVariable*> specRef = allowed.get(varName);
		CustomizationVariable* spec = specRef.get();

		if (spec == nullptr) {
			continue;
		}

		RangedIntCustomizationVariable* rangeSpec = dynamic_cast<RangedIntCustomizationVariable*>(spec);

		if (rangeSpec != nullptr) {
			const int minVal = rangeSpec->getMinValueInclusive();
			const int maxVal = rangeSpec->getMaxValueExclusive();

			if (value < minVal || value >= maxVal) {
				StringBuffer buf;
				buf << "customization '" << varName << "' value " << value
					<< " out of range [" << minVal << ", " << maxVal << ")";
				outReason = buf.toString();
				return false;
			}
		}
	}

	return true;
}

bool validateOwnerCompatibility(TangibleObject* item, CreatureObject* owner, String& outReason) {
	outReason = "";

	if (owner == nullptr) {
		return true;
	}

	SharedTangibleObjectTemplate* tmpl = dynamic_cast<SharedTangibleObjectTemplate*>(item->getObjectTemplate());
	if (tmpl == nullptr) {
		return true;
	}

	const Vector<uint32>* races = tmpl->getPlayerRaces();
	if (races == nullptr || races->size() == 0) {
		return true;
	}

	String ownerTemplate = owner->getObjectTemplate()->getFullTemplateString();
	const uint32 raceHash = ownerTemplate.hashCode();

	if (!races->contains(raceHash)) {
		StringBuffer reason;
		reason << "owner '" << owner->getDisplayedName()
			   << "' (" << ownerTemplate << ") is not allowed to equip this item";
		outReason = reason.toString();
		return false;
	}

	return true;
}

void broadcastToOwner(TangibleObject* item, CreatureObject* owner, const String& reason) {
	if (owner == nullptr || !owner->isPlayerCreature()) {
		return;
	}

	StringBuffer msg;
	msg << "Item flagged as invalid: " << reason;
	owner->sendSystemMessage(msg.toString());
}

void logInvalid(TangibleObject* item, Logger* logger, const String& reason) {
	String descriptor = buildObjectDescriptor(item);

	Locker guard(&gBadItemMutex);
	bool firstLog = gLoggedBadItems.emplace(item->getObjectID()).second;
	guard.release();

	if (!firstLog) {
		return;
	}

	if (logger != nullptr) {
		logger->error(true) << "Detected invalid item " << descriptor << " - " << reason;
	} else {
		Logger::console.error() << "Detected invalid item " << descriptor << " - " << reason;
	}
}

void logCleared(TangibleObject* item, Logger* logger) {
	Locker guard(&gBadItemMutex);
	bool wasLogged = gLoggedBadItems.erase(item->getObjectID()) != 0;
	guard.release();

	if (!wasLogged) {
		return;
	}

	String descriptor = buildObjectDescriptor(item);

	if (logger != nullptr) {
		logger->info(true) << "Cleared invalid state flag for item " << descriptor;
	} else {
		Logger::console.info(true) << "Cleared invalid state flag for item " << descriptor;
	}
}

} // anonymous namespace

bool InvalidItemHelper::hasBadItemFlag(const TangibleObject* item) {
	if (item == nullptr) {
		return false;
	}

	String stored = item->getLuaStringData(BAD_ITEM_REASON_KEY);
	return !stored.isEmpty();
}

bool InvalidItemHelper::ensureIntegrity(TangibleObject* item, CreatureObject* owner, Logger* logger) {
	if (item == nullptr) {
		return true;
	}

	String reason;

	if (!validateCustomization(item, reason)) {
		String existing = item->getLuaStringData(BAD_ITEM_REASON_KEY);
		if (existing != reason) {
			item->setLuaStringData(BAD_ITEM_REASON_KEY, reason);
			logInvalid(item, logger, reason);
			broadcastToOwner(item, owner, reason);
		}
		return false;
	}

	if (!validateOwnerCompatibility(item, owner, reason)) {
		String existing = item->getLuaStringData(BAD_ITEM_REASON_KEY);
		if (existing != reason) {
			item->setLuaStringData(BAD_ITEM_REASON_KEY, reason);
			logInvalid(item, logger, reason);
			broadcastToOwner(item, owner, reason);
		}
		return false;
	}

	if (hasBadItemFlag(item)) {
		clearBadItemFlag(item, logger);
	}

	return true;
}

void InvalidItemHelper::clearBadItemFlag(TangibleObject* item, Logger* logger) {
	if (item == nullptr) {
		return;
	}

	String existing = item->getLuaStringData(BAD_ITEM_REASON_KEY);
	if (existing.isEmpty()) {
		return;
	}

	item->deleteLuaStringData(BAD_ITEM_REASON_KEY);
	logCleared(item, logger);
}
