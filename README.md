# G29 LED Plugin for SCS Games

This plugin should enable custom LED effects in the Logitech G29 steering wheel in American Truck Simulator and Euro Truck Simulator 2 games from SCS.

## Installation

Copy the plugin file to your game's `plugins` directory. The directory should be located within the path where the game `.exe` file is (`amtrucks.exe` and `eurotrucks2.exe` respectively). The game installation path depends on your Steam library settings (**properties** > **browse local files**).

## Effects

The goal for the first iteration of this plugin is to figure the fuel tank level as LEDs, being the two center red LEDs indicating an empty tank, and all leds lit up to the green ones, a full tank.

Some special effects are to be attempted, like an animation during refuel (not sure if telemetry data provides information for that), and also blinking frequency of the red LEDs as the tank becomes close to complete depletion.