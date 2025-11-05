/*
 * InvalidItemHelper.h
 *
 *  Created on: 05/11/2025
 *      Author: Codex Assist
 *
 *  Utility helpers to detect and flag tangible objects that are in an
 *  invalid state (bad customization data, race restricted wearables, etc).
 */

#ifndef SERVER_ZONE_OBJECTS_TANGIBLE_INVALIDITEMHELPER_H_
#define SERVER_ZONE_OBJECTS_TANGIBLE_INVALIDITEMHELPER_H_

#include "engine/log/Logger.h"

class String;

class TangibleObject;
class CreatureObject;

namespace server {
namespace zone {
namespace objects {
namespace tangible {

namespace InvalidItemHelper {

	static const char* const BAD_ITEM_REASON_KEY = "bad_item_reason";

	bool hasBadItemFlag(const TangibleObject* item);

	/**
	 * Verifies the tangible object for customization data issues and (optionally)
	 * ownership constraints. When an invalid state is detected the helper records
	 * the reason in the object's luaStringData map and emits a log line.
	 *
	 * @param item tangible to validate (must be locked by the caller)
	 * @param owner optional owner creature when evaluating wearables
	 * @param logger optional logger used for diagnostics; falls back to console logging when null
	 * @return true when the item passes validation, false when it was flagged as invalid
	 */
	bool ensureIntegrity(TangibleObject* item, CreatureObject* owner, Logger* logger = nullptr);

	/**
	 * Helper to clear an existing bad item flag (used when the state has been corrected).
	 */
	void clearBadItemFlag(TangibleObject* item, Logger* logger = nullptr);

} // namespace InvalidItemHelper

} // namespace tangible
} // namespace objects
} // namespace zone
} // namespace server

#endif /* SERVER_ZONE_OBJECTS_TANGIBLE_INVALIDITEMHELPER_H_ */

