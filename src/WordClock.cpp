#include "WordClock.h"

extern bool POSTSuccess;

/// @brief Creates a WordClock object
/// @param Name The device name
/// @param LEDCount The total number of LEDs in the display
/// @param configFile The name of the configuration to use
WordClock::WordClock(String Name, int LEDCount, String configFile) : Actor(Name) {
	config_path =  "/settings/act/" + configFile;
	ledcount = LEDCount;
}

/// @brief Starts the WordClock object
/// @return True on success
bool WordClock::begin() {
	if (Configuration::currentConfig.useNTP) {
		// Waits for time to be set
		int timeout = 0;
		display_controller.actions_config.Enabled = true;
		do {
			delay(1000);
			timeout++;
		} while (TimeInterface::getEpoch() < 10000 && timeout < 20);
		// Check if time was set
		if (TimeInterface::getEpoch() < 10000) {
			return false;
		}
	}
	brightness_sensor.parameter_config.Parameters.resize(1);
	// Set description
	Description.actionQuantity = 1;
	Description.type = "Display";
	Description.actions = {{"update", 0}};
	task_config.set_taskName(Description.name.c_str());
	task_config.taskPeriod = 1000;
	// Create settings directory if necessary
	if (!checkConfig(config_path)) {
		// Set defaults
		return setConfig(getConfig(), true);
	} else {
		// Load settings
		return setConfig(Storage::readFile(config_path), false);
	}
}

/// @brief Receives an action
/// @param action The action to process 
/// @param payload 
/// @return JSON response with OK or error message
std::tuple<bool, String> WordClock::receiveAction(int action, String payload) {
	if (action == 0) {
		// If tasks not running, sensors won't update, this guarantees a fresh sensor value
		if (brightness_sensor.parameter_config.Enabled) {
			SensorManager::takeMeasurement();
		}
		if (updateDisplay(true)) {
			return { true, R"({"success": true})" };
		}
		return { true, R"({"success": false, "Response": "Could not update display"})" };
	}
	return { true, R"({"success": false, "Response": "Invalid action"})" };
}

/// @brief Gets the current config
/// @return A JSON string of the config
String WordClock::getConfig() {
	// Allocate the JSON document
	JsonDocument doc;
	// Assign current values
	doc["Name"] = Description.name;
	// Add all actors to dropdown
	doc["NeoPixel_Controller"]["current"] = Clock_config.NexoPixel_Controller;
	std::map<String, std::map<int, String>> actions = display_controller.listAllActions();
	int i = 0;
	for (std::map<String, std::map<int, String>>::iterator actor = actions.begin(); actor != actions.end(); actor++) {
		if (actor->first != Description.name) {
			doc["NeoPixel_Controller"]["options"][i] = actor->first;
			i++;
		}
	}
	// Add all sensor options to dropdown
	doc["Brightness_Parameter"]["current"] = brightness_sensor.parameter_config.Parameters.size() > 0 ? brightness_sensor.parameter_config.Parameters[0].first + ":" + brightness_sensor.parameter_config.Parameters[0].second : "";
	std::map<String, std::vector<String>> sensors = brightness_sensor.listAllParameters();
	if (sensors.size() > 0) {
		i = 0;
		for (std::map<String, std::vector<String>>::iterator sensor = sensors.begin(); sensor != sensors.end(); sensor++) {
			for (const auto& p : sensor->second) {
				doc["Brightness_Parameter"]["options"][i] = sensor->first + ":" + p;
				i++;
			}
		}
	} else {
		doc["Brightness_Parameter"]["options"][0] = "";
	}

	doc["AutoBrightness"] = brightness_sensor.parameter_config.Enabled;
	doc["brightnessMin"] = Clock_config.brightnessMin;
	doc["sensorMax"] = Clock_config.sensorMax;
	doc["sensorMin"] = Clock_config.sensorMin;
	doc["sensorSmoothing"] = Clock_config.sensorSmoothing;
	// Collect RGB color values
	String color = "";
	for (int j = 0; j < Clock_config.color.size(); j++) {
		if (j > 0) {
			color += ",";
		}
		color += String(Clock_config.color[j]);
	}
	doc["color"] = color;
	doc["TaskPeriod"] = task_config.taskPeriod;

	// Create string to hold output
	String output;
	// Serialize to string
	serializeJson(doc, output);
	return output;
}

/// @brief Sets the configuration for this device
/// @param config A JSON string of the configuration settings
/// @param save If the configuration should be saved to a file
/// @return True on success
bool WordClock::setConfig(String config, bool save) {
	Serial.println(config);
	// Allocate the JSON document
  	JsonDocument doc;
	// Deserialize file contents
	DeserializationError error = deserializeJson(doc, config);
	// Test if parsing succeeds.
	if (error) {
		Logger.print(F("Deserialization failed: "));
		Logger.println(error.f_str());
		return false;
	}
	// Assign loaded values
	Description.name = doc["Name"].as<String>();
	Clock_config.NexoPixel_Controller = doc["NeoPixel_Controller"]["current"].as<String>();
	Clock_config.brightnessMin = doc["brightnessMin"].as<float>();
	Clock_config.sensorMax = doc["sensorMax"].as<float>();
	Clock_config.sensorMin = doc["sensorMin"].as<float>();
	Clock_config.sensorSmoothing = doc["sensorSmoothing"].as<int>();

	// Parse RGB color values
	String colorString = doc["color"].as<String>();
	Clock_config.color.clear();
	int commaCount = 1;
	for(int i = 0; i < colorString.length(); i++) {
		if(colorString[i] == ',') commaCount++;
	}
	Clock_config.color.resize(commaCount);
	int startIndex = 0;
	int commaIndex;
	int colorCount = 0;
	while ((commaIndex = colorString.indexOf(',', startIndex)) != -1) {
		Clock_config.color[colorCount++] = colorString.substring(startIndex, commaIndex).toInt();
		startIndex = commaIndex + 1;
	}
	Clock_config.color[colorCount++] = colorString.substring(startIndex).toInt();
	// Parse sensor parameter for brightness
	String brightness_combined = doc["Brightness_Parameter"]["current"].as<String>();
	int colon;
	if ((colon = brightness_combined.indexOf(':')) != -1) {
		std::pair<String, String> chosen {brightness_combined.substring(0, colon), brightness_combined.substring(colon + 1)};
		brightness_sensor.parameter_config.Parameters[0] = chosen;
	}
	brightness_sensor.parameter_config.Enabled = doc["AutoBrightness"].as<bool>();
	enableTask(false);
	task_config.set_taskName(Description.name.c_str());
	task_config.taskPeriod = doc["TaskPeriod"].as<ulong>();

	if (save) {
		if (!saveConfig(config_path, config)) {
			return false;
		}
	}
	currentBrightness = getBrightness();
	return enableTask(true) && updateDisplay(true);
}

void WordClock::runTask(ulong elapsed) {
	if (taskPeriodTriggered(elapsed)) {
		updateDisplay();
	}
}

/// @brief Updates the LED display
/// @param RGB_Values The RGB(W) values to use
/// @return True on success
bool WordClock::updateLEDS(std::vector<std::vector<uint8_t>> RGB_Values) {
	// Allocate the JSON document
	JsonDocument doc;
	JsonArray RGB = doc["RGB_Values"].to<JsonArray>();
	int colors = Clock_config.color.size();
	for (int i = 0; i < RGB_Values.size(); i++) {
		JsonArray values = doc["RGB_Values"][i].to<JsonArray>();
		values.add(RGB_Values[i][0]);
		values.add(RGB_Values[i][1]);
		values.add(RGB_Values[i][2]);
		if (colors > 3) {
			values.add(RGB_Values[i][3]);
		}
	}
	// Create string to hold output
	String rgb_payload;
	// Serialize to string
	serializeJson(doc, rgb_payload);
	std::map<String, std::map<String, String>> payload;
	Logger.println("Display updating");
	payload[Clock_config.NexoPixel_Controller] = {{"setcolor", rgb_payload}};
	return display_controller.triggerActions(payload);
}

/// @brief Updates the time displayed on the clock
/// @param force Forces a refresh of the display even if the time hasn't changed
/// @return True on success
bool WordClock::updateDisplay(bool force) {
	if (Clock_config.NexoPixel_Controller != "" && POSTSuccess) {
		String cur_time = TimeInterface::getFormattedTime("%I:%M");
		// Get minutes as five minute intervals
		int cur_min = (cur_time.substring(cur_time.indexOf(':') + 1).toInt() / 5) * 5;

		// Check if display brightness needs updating
		if (brightness_sensor.parameter_config.Enabled) {
			float new_brightness = getBrightness();
			if (new_brightness != currentBrightness) {
				currentBrightness = new_brightness;
				force = true;
			}
		}

		// Check if clock display update is necessary
		if (force || cur_min != previousMin) {
			int cur_hour = cur_time.substring(0, cur_time.indexOf(':')).toInt();
			std::vector<uint8_t> color_final = Clock_config.color;
			if (brightness_sensor.parameter_config.Enabled) {
				for (auto& c : color_final) {
					c = std::round((float)c * currentBrightness);
				}
			}
			
			previousMin = cur_min;
			std::vector<std::vector<uint8_t>> colors(ledcount, std::vector<uint8_t>(Clock_config.color.size(), 0));
			// Set hour display
			if (cur_min != 0) {
				// Check if in back half of hour
				if (cur_min > 30) {
					// Fix hour display
					if (cur_hour == 12) {
						cur_hour = 1;
					} else {
						cur_hour++;
					}
					// Set to display
					for (int i = words["to"][0]; i <= words["to"][1]; i++) {
						colors[i] = color_final;
					}
					// Fix minutes
					cur_min = 60 - cur_min;
				} else {
					// Set past display
					for (int i = words["past"][0]; i <= words["past"][1]; i++) {
						colors[i] = color_final;
					}
				}
				// Set minutes word display
				for (int i = words["minutes"][0]; i <= words["minutes"][1]; i++) {
					colors[i] = color_final;
				}
				// Set minute time display
				for (int i = mins[cur_min][0]; i <= mins[cur_min][1]; i++) {
					colors[i] = color_final;
				}
			} else {
				// Set o'clock display
				for (int i = words["oclock"][0]; i <= words["oclock"][1]; i++) {
					colors[i] = color_final;
				}
			}
			for (int i = hours[cur_hour][0]; i <= hours[cur_hour][1]; i++) {
				colors[i] = color_final;
			}
			// Set it is display
			for (int i = words["it"][0]; i <= words["it"][1]; i++) {
				colors[i] = color_final;
			}
			for (int i = words["is"][0]; i <= words["is"][1]; i++) {
				colors[i] = color_final;
			}
			return updateLEDS(colors);
		}
	}
	return true;
}

/// @brief Gets the smoothed brightness value
/// @return The new brightness value
float WordClock::getBrightness() {
	std::map<String, std::map<String, double>> params = brightness_sensor.getParameterValues();
	// Ensure the desired parameter exists 
	if (brightness_sensor.parameter_config.Enabled && params.size() > 0) {
		// Obtain and constrain the measurement
		double value = params[brightness_sensor.parameter_config.Parameters[0].first][brightness_sensor.parameter_config.Parameters[0].second];
		value = constrain(value, Clock_config.sensorMin, Clock_config.sensorMax);
		// Calculate the percent of the reading proportionally to the min/max range
		double newvalue = std::round(((value - Clock_config.sensorMin) / (Clock_config.sensorMax - Clock_config.sensorMin)) * 100) / 100;
		// Constrain brightness to minimum percent
		if (newvalue < Clock_config.brightnessMin) {
			newvalue = Clock_config.brightnessMin;
		}
		if (Clock_config.sensorSmoothing > 1) {
			// Resize average queue if needed
			if(readings.size() > Clock_config.sensorSmoothing) {
				readings.resize(Clock_config.sensorSmoothing);
				readings.shrink_to_fit();
			}
			if (readings.size() == Clock_config.sensorSmoothing) {
				readings.pop_back();
			}
			readings.push_front(newvalue);
			float reading = 0;
			for (const auto& r : readings) {
				reading += r;
			}
			return reading / readings.size();
		} else {
			return newvalue;
		}
	}
	Logger.println("Brightness sensor not found/not enabled");
	return 1;
}