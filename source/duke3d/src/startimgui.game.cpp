//-------------------------------------------------------------------------
/*
Copyright (C) 2025 EDuke32 developers and contributors

This file is part of EDuke32.

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

#define IMGUI_DEFINE_MATH_OPERATORS
# include "imgui.h"
# include "imgui_impl_sdl2.h"
#ifdef USE_OPENGL
# include "imgui_impl_opengl3.h"
#endif

#include "sdlayer.h"
#include "cmdline.h"
#include "duke3d.h"

int32_t startwin_idle(void *s) { UNREFERENCED_PARAMETER(s); return 0; }
int32_t startwin_settitle(const char *s) {
    SDL_Window *sdl_window = sdl_get_window();
    SDL_SetWindowTitle(sdl_window, s);

    return 0;
}
bool startwin_isopen(void) { return true; }

static struct
{
    grpfile_t const * grp;
    char *gamedir;
    ud_setup_t shared;
    ImGuiTextBuffer message_buf;
    ImVector<char*> video_modes;
    ImVector<char*> custom_games;
    ImVector<int> video_modes_ids;
    ImFont *font;
    int selected_video_mode;
    int selected_input;
    int selected_custom_game;
    bool fullscreen;
    bool autoload;
    bool forcesetup;
    bool polymer;
} settings;

const char *controlstrings[] = { "Keyboard only", "Keyboard and mouse", "Keyboard and joystick", "All supported devices" };

// Attempt to get current video mode id
int check_video_modes(void) {
    int mode3d = videoCheckMode(&settings.shared.xdim, &settings.shared.ydim, settings.shared.bpp, settings.fullscreen, 1);
    if (mode3d < 0)
    {
        int32_t i, cd[] = { 32, 24, 16, 15, 8, 0 };

        for (i=0; cd[i];) { if (cd[i] >= settings.shared.bpp) i++; else break; }
        for (; cd[i]; i++)
        {
            mode3d = videoCheckMode(&settings.shared.xdim, &settings.shared.ydim, cd[i], settings.fullscreen, 1);
            if (mode3d < 0) continue;
            settings.shared.bpp = cd[i];
            break;
        }
    }
    return mode3d;
}

void clear_video_modes() {
    for (char* vm : settings.video_modes)
    {
        Xfree(vm);
    }
    settings.video_modes.clear();
    settings.video_modes_ids.clear();
}

void build_video_mode_list() {
    clear_video_modes();
    int mode3d = check_video_modes();
    if (settings.grp != NULL)
    {
        int i;
        for (i=0; i<validmodecnt; i++)
        {
            char mode_buf[64];
            int32_t const flags = settings.grp ? settings.grp->type->game : 0;

            if (flags & GAMEFLAG_NOCLASSIC && validmode[i].bpp == 8) continue;
            if (validmode[i].fs != settings.fullscreen) continue;

            // all modes get added to the 3D mode list
            Bsprintf(mode_buf, "%dx%d %s", validmode[i].xdim, validmode[i].ydim,
                        validmode[i].bpp == 8          ? "software"
                        : (flags & GAMEFLAG_NOCLASSIC) ? ""
                                                    : "OpenGL");
            settings.video_modes.push_back(Xstrdup(mode_buf));
            settings.video_modes_ids.push_back(i);
            if (i == mode3d)
            {
                settings.selected_video_mode = settings.video_modes_ids.size() - 1;
            }
        }
    }
}

int32_t startwin_open(void) {
    sdl_get_window();

    SDL_GL_SetSwapInterval(1);

    engineSetupImGui();

    // Enable gamepad controls for imgui
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // TODO: Fonts and HiDPI
    // settings.font = ImGui::GetIO().Fonts->AddFontFromFileTTF("/usr/share/fonts/google-droid-sans-fonts/DroidSans.ttf", 16.0f);
    // ImGui::GetStyle().ScaleAllSizes(1.0);
    settings.font = NULL;

    return 0;
}

int32_t startwin_close(void) {
    settings.message_buf.clear();
    settings.custom_games.clear();
    clear_video_modes();
    return 0;
}

const ImVec2 button_offset = ImVec2(0, -25);

void build_custom_game_list()
{
    char *homedir;
    char pdir[BMAX_PATH];
    unsigned char iternumb = 0;
    BUILDVFS_FIND_REC *dirs = NULL;

    int const bakpathsearchmode = pathsearchmode;
    pathsearchmode = 1;

    if ((homedir = Bgethomedir()))
    {
        if (buildvfs_exists("user_profiles_disabled"))
            buildvfs_getcwd(pdir, sizeof(pdir));
        else
            Bsnprintf(pdir, sizeof(pdir), "%s/.config/" APPBASENAME, homedir);

        dirs = klistpath(pdir, "*", BUILDVFS_FIND_DIR);

        settings.custom_games.clear();
        settings.custom_games.push_back((char*)"None");
        for (; dirs != NULL; dirs=dirs->next)
        {
            if ((Bstrcmp(dirs->name, "autoload") == 0) ||
                    (Bstrcmp(dirs->name, "..") == 0) ||
                    (Bstrcmp(dirs->name, ".") == 0))
                continue;
            else
            {
                settings.custom_games.push_back(dirs->name);
                if (Bstrcmp(dirs->name, settings.gamedir) == 0)
                    settings.selected_custom_game = settings.custom_games.Size - 1;
                iternumb++;
            }
        }
    }

    klistfree(dirs);
    dirs = NULL;

    pathsearchmode = bakpathsearchmode;
}

int32_t startwin_run() {
    int32_t retval = -1;
    SDL_Window *sdl_window = sdl_get_window();
    bool prev_fullscreen;

    settings.shared = ud.setup;
    settings.gamedir = g_modDir;
    settings.grp = g_selectedGrp;

#ifdef POLYMER
    settings.polymer = (glrendmode == REND_POLYMER) & (settings.shared.bpp != 8);
#endif
    settings.fullscreen = settings.shared.fullscreen;
    settings.autoload = !settings.shared.noautoload;
    settings.forcesetup = settings.shared.forcesetup;

    settings.selected_input = (settings.shared.usejoystick & 1) << 1 | (settings.shared.usemouse & 1);

    prev_fullscreen = settings.fullscreen;

    build_video_mode_list();
    build_custom_game_list();

    while (retval == -1) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                retval = 0;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(sdl_window))
                retval = 0;
        }

        if (prev_fullscreen != settings.fullscreen) {
            build_video_mode_list();
            prev_fullscreen = settings.fullscreen;
        }

        engineBeginImGuiFrame();
#ifdef IMGUI_HAS_VIEWPORT
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
#else
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
#endif
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::Begin("##startwin", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoResize);
        ImGui::PushFont(settings.font);

        if (ImGui::BeginTabBar("TabBar", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Setup")) {
                ImGui::BeginChild("##setup_tab", ImGui::GetContentRegionAvail() + button_offset);
                ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5);
                ImGui::BeginGroup();
                ImGui::Combo("Video mode", &settings.selected_video_mode, settings.video_modes.begin(), settings.video_modes.Size);

                if (ImGui::BeginCombo("Input devices", controlstrings[settings.selected_input])) {
                    for (int i = 0; i < IM_ARRAYSIZE(controlstrings); i++) {
                        const bool is_selected = settings.selected_input == i;

                        if (ImGui::Selectable(controlstrings[i], is_selected)) {
                            settings.selected_input = i;
                        }

                        if (is_selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndGroup();
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ImGui::Checkbox("Fullscreen", &settings.fullscreen);
#ifdef POLYMER
                int32_t const flags = settings.grp ? settings.grp->type->game : 0;
                if (flags & GAMEFLAG_NOPOLYMER) {
                    ImGui::BeginDisabled();
                }
                ImGui::SameLine();
                ImGui::Checkbox("Polymer", (bool*)&settings.polymer);
                if (flags & GAMEFLAG_NOPOLYMER) {
                    ImGui::EndDisabled();
                }
#endif

                if (ImGui::BeginTable("##game_table", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersV)) {
                    uint32_t game_idx = 0;
                    ImGui::TableSetupColumn("Game");
                    ImGui::TableSetupColumn("File");
                    ImGui::TableHeadersRow();
                    for (grpfile_t const * fg = foundgrps; fg; fg=fg->next)
                    {
                        ImGui::PushID(game_idx);
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        if (ImGui::Selectable(fg->type->name, settings.grp == fg, ImGuiSelectableFlags_SpanAllColumns)) {
                            settings.grp = fg;
                        }
                        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                            retval = 1;
                        }
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(fg->filename);
                        ImGui::PopID();
                        game_idx++;
                    }
                    ImGui::EndTable();
                }

                ImGui::Combo("Custom game content directory", &settings.selected_custom_game, settings.custom_games.begin(), settings.custom_games.Size);

                ImGui::Checkbox("Enable \"autoload\" folder", &settings.autoload);
                ImGui::SameLine();
                ImGui::Checkbox("Always show this window at startup", &settings.forcesetup);
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Message Log")) {
                ImGui::InputTextMultiline("##log_panel", (char*)settings.message_buf.begin(), settings.message_buf.size(), ImGui::GetContentRegionAvail() + button_offset, ImGuiInputTextFlags_ReadOnly);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        if(ImGui::Button("Start"))
            retval = 1;
        ImGui::SameLine();
        if(ImGui::Button("Quit"))
        {
            retval = 0;
        }
        ImGui::PopFont();
        ImGui::End();
        ImGui::PopStyleVar(1);

        ImGui::Render();
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        g_ImGuiFrameActive = false;
        SDL_GL_SwapWindow(sdl_window);

        if (settings.selected_video_mode < settings.video_modes_ids.size()) {
            int i = settings.video_modes_ids[settings.selected_video_mode];
            settings.shared.xdim = validmode[i].xdim;
            settings.shared.ydim = validmode[i].ydim;
            settings.shared.bpp = validmode[i].bpp;
        }
    }

    // Clean up startwin ImGui context
    engineDestroyImGui();

    if (retval) // launch the game with these parameters
    {
        settings.shared.fullscreen = settings.fullscreen;
        settings.shared.noautoload = !settings.autoload;
        settings.shared.forcesetup = settings.forcesetup;

        settings.shared.usejoystick = settings.selected_input & 1;
        settings.shared.usemouse = settings.selected_input >> 1 & 1;

        char *gamedir = settings.custom_games[settings.selected_custom_game];
        Bstrcpy(g_modDir, (g_noSetup == 0 && gamedir != NULL) ? gamedir : "/");

        ud.setup = settings.shared;
    #ifdef POLYMER
        glrendmode = settings.polymer ? REND_POLYMER : REND_POLYMOST;
    #endif
        g_selectedGrp = settings.grp;
    }

    return retval;
}

int32_t startwin_puts(const char *message) {
    settings.message_buf.append(message);
    return 0;
}
