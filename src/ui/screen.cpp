// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screen.hpp"
#include "receiver/airplay.hpp"
#include "imgui.h"
#include <algorithm>

namespace mirror {
void render_screen(AirPlay& receiver,const Snapshot& status,VkDescriptorSet video,int width,int height) {
    ImGui::SetNextWindowPos(ImVec2(0,0),ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize,ImGuiCond_Always);
    ImGui::Begin("MirrorPSX",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize);
    ImGui::TextUnformatted("MirrorPSX — AirPlay receiver");
    ImGui::Separator();
    if(status.listening) {
        ImGui::Text("Listening: %s (TCP %u)",status.name.c_str(),status.port);
        if(ImGui::Button("Stop receiver")) receiver.stop();
    } else {
        ImGui::TextUnformatted("Receiver stopped");
        if(ImGui::Button("Start receiver")) receiver.start();
    }
    ImGui::Text("Client: %s",status.client.empty()?"None":status.client.c_str());
    ImGui::Text("Connection: %s",status.connected?"Connected":"Waiting");
    if(!status.pin.empty()) ImGui::Text("Pairing PIN: %s",status.pin.c_str());
    if(!status.error.empty()) ImGui::TextWrapped("Error: %s",status.error.c_str());
    ImGui::Text("Decoded frames: %llu | Stream: %d x %d",
                static_cast<unsigned long long>(status.frames),status.width,status.height);
    ImGui::Separator();
    const ImVec2 available=ImGui::GetContentRegionAvail();
    if(video && width>0 && height>0 && available.x>0 && available.y>0) {
        const float scale=std::min(available.x/width,available.y/height);
        const ImVec2 size(width*scale,height*scale);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX()+std::max(0.0f,(available.x-size.x)*0.5f));
        ImGui::Image((ImTextureID)(uintptr_t)video,size);
    } else {
        ImGui::TextWrapped("No video yet. Connect an AirPlay screen-mirroring client on the same network.");
    }
    ImGui::End();
}
}
