/*
* This file and associated .cpp file are licensed under the GPLv3 License Copyright (c) 2025 Sam Groveman
* 
* Contributors: Sam Groveman
*/

#pragma once
#include <Actor.h>
#include <PeriodicTask.h>
#include <ParameterTrigger.h>
#include <ActionTrigger.h>
#include <TimeInterface.h>
#include <Configuration.h>
#include <map>
#include <vector>
#include <deque>

/// @brief Controls a WordClock display
class WordClock : public Actor, public PeriodicTask {
	public:
		/// @brief Clock settings
		struct {
			/// @brief The name of the NeoPixel controller to use
			String NexoPixel_Controller = "";

			/// @brief The minimum percent brightness of the display
			float  brightnessMin = 0.1;

			/// @brief The sensor value at which the brightness is set to the minimum
			float sensorMin = 0.5;

			/// @brief The sensor value at which brightness is set to 100% (1)
			float sensorMax = 30000;

			/// @brief The size of the rolling average to use to smooth brightness (0 disables)
			int sensorSmoothing = 5;

			/// @brief The current color of the display
			std::vector<uint8_t> color {127, 127, 127};			
		} Clock_config;

		WordClock(String Name, String configFile = "WordClock.json");
		bool begin();
		std::tuple<bool, String> receiveAction(int action, String payload);
		String getConfig();
		bool setConfig(String config, bool save);

	protected:
		/// @brief Full path of configuration file
		String config_path;

		/// @brief Parameter trigger object for getting brightness
		ParameterTrigger brightness_sensor;

		/// @brief Allows for setting the LED colors
		ActionTrigger display_controller;

		/// @brief Map of LED ranges for each hour
		std::map<uint8_t, std::array<uint8_t, 2>> hours {{1, {13, 14}}, {2, {29, 30}}, {3, {9, 12}}, {4, {41, 43}}, {5, {26, 28}}, {6, {7, 8}}, {7, {36, 38}}, {8, {20, 22}}, {9, {23, 25}}, {10, {5, 6}}, {11, {15, 19}}, {12, {31, 35}}};
		
		/// @brief Map of LED ranges for each 5-minute interval
		std::map<uint8_t, std::array<uint8_t, 2>> mins {{5, {56, 58}}, {10, {54, 55}}, {15, {63, 67}}, {20, {59, 62}}, {25, {56, 62}}, {30, {50, 53}}};
		
		/// @brief Map of LED ranges for words
		std::map<String, std::array<uint8_t, 2>> words {{"it", {69,69}}, {"is", {68,68}}, {"minutes", {45,49}}, {"oclock", {0,4}}, {"to", {44,44}}, {"past", {39,40}}};

		/// Queue holding readings for the sensor rolling average
		std::deque<float> readings;

		/// @brief Stores the last current that is displayed on the clock
		int previousMin = 0;

		/// @brief The total number of LEDs in the display
		int ledCount = 0;

		/// @brief Stores the current auto brightness value
		float currentBrightness = 0;

		void runTask(ulong elapsed);
		bool updateLEDS(std::vector<std::vector<uint8_t>> RGB_Values);
		bool updateDisplay(bool force = false);
		float getBrightness();
		bool setLEDCount();
};
