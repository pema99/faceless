// feeder_faceless.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <openvr.h>
#include <thread>
#include <vector>

vr::IVRSystem* vrSystem;

bool vrSystemInit = false;

bool InitOpenVRHelper()
{
    vrSystemInit = true;

    vr::EVRInitError eError = vr::VRInitError_None;
    vrSystem = vr::VR_Init(&eError, vr::VRApplication_Background);

    return eError != vr::VRInitError_None;
}

void CleanupOpenVRHelper()
{
    vr::VR_Shutdown();
}

int main()
{
    if (InitOpenVRHelper())
        printf("Init VR succeeded!");

    while (true)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    CleanupOpenVRHelper();
}