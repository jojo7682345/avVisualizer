#include "imgui/imgui.h"
extern "C" {
#include "defines.h"
#include <math.h>

static bool my_tool_active = true;
static float my_color[4];
static bool show_demo_window = true;

bool8 renderFrameCpp(struct Application* app_inst){
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0));
    ImGui::Begin("MainDockspace", NULL, window_flags);

    ImGuiID dockspace_id = ImGui::GetID("MainDockspace");
    
    ImGuiDockNodeFlags dockspace_flags =
    ImGuiDockNodeFlags_PassthruCentralNode |
    ImGuiDockNodeFlags_NoDockingInCentralNode;
    ImGui::DockSpace(dockspace_id, ImVec2(0,0), dockspace_flags);


    ImGui::End();
    ImGui::PopStyleColor();

    ImGui::ShowDemoWindow();
    // Create a window called "My First Tool", with a menu bar.
    if(my_tool_active){
        ImGui::Begin("My First Tool", 0, ImGuiWindowFlags_MenuBar);
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Open..", "Ctrl+O")) { /* Do stuff */ }
                if (ImGui::MenuItem("Save", "Ctrl+S"))   { /* Do stuff */ }
                if (ImGui::MenuItem("Close", "Ctrl+W"))  { my_tool_active = false; }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        
        // Edit a color stored as 4 floats
        ImGui::ColorEdit4("Color", my_color);
        
        // Generate samples and plot them
        float samples[100];
        for (int n = 0; n < 100; n++)
            samples[n] = sinf(n * 0.2f + ImGui::GetTime() * 1.5f);
        ImGui::PlotLines("Samples", samples, 100);
        
        // Display contents in a scrolling region
        ImGui::TextColored(ImVec4(1,1,0,1), "Important Stuff");
        ImGui::BeginChild("Scrolling");
        for (int n = 0; n < 50; n++)
            ImGui::Text("%04d: Some text", n);
        ImGui::EndChild();
        ImGui::End();
    }
    return true;
}

bool8 initializeCpp(struct Application* app_inst){
    return true;
}

}