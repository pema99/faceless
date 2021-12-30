# Faceless
A proof of concept OpenVR driver for using VR controllers without an HMD.

The goal is to guesstimate the position of the HMD based on the controller positions, so you can interact with the virtual world without needing to wear the HMD at all.

To build, open the solution is Visual Studio 2019 and build both 32 and 64 bit targets in Release mode. The output driver will go to `bin/drivers/faceless`. This driver (the whole faceless folder) can be put in `Steam/steamapps/common/SteamVR/drivers` for use.
