# Faceless
A proof of concept OpenVR driver for using VR controllers without an HMD.

The goal is to guesstimate the position of the HMD based on the controller positions, or use a tracker for the head, so you can interact with the virtual world without needing to wear the HMD at all.

# How to use
To build, open the solution is Visual Studio 2019 and build both 32 and 64 bit targets in Release mode. The output driver will go to `bin/drivers/faceless`. This driver (the whole faceless folder) can be put in `Steam/steamapps/common/SteamVR/drivers` for use.

To use this, you must open your `Steam/config/steamvr.vrsettings` file and set the `activateMultipleDrivers` key true under the `steamvr` sections:
```json
"steamvr" : {
      "activateMultipleDrivers" : true
}
```
This will allow the driver used for the VR controllers to work concurrently with this driver. Finally, enable the driver in the Startup section of SteamVR settings.

# Tracker support
You can use an extra tracker to track your head. This functionality can be disabled by setting `useTracker: false` in the `driver_faceless` section of your `Steam/config/steamvr.vrsettings`. If you use full body tracking, you might want to disable it, as it defaults to enabled.

To use this in practice, first calibrate using room setup like you would normally. After this, your orientation will very likely still be off, so use the following keybinds to adjust it:
| Keybind         | Effect                                 |
| --------------- | -------------------------------------- |
| `Home`+`A`      | Adjust yaw                             |
| `Home`+`D`      | Adjust yaw                             |
| `Home`+`Q`      | Adjust roll                            |
| `Home`+`E`      | Adjust roll                            |
| `Home`+`W`      | Adjust pitch                           |
| `Home`+`S`      | Adjust pitch                           |
| `Home`+`End`    | Save orientation to config file        |
| `Home`+`Delete` | Reset orientation to default settings  |

Saving the orientation will cause it to be loaded automatically next time the driver is started.

Playspace mover is strongly recommended to adjust your height/position.
