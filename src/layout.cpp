#include "layout.h"
#include "toplevel.h"
#include <cmath>

LayoutManager::LayoutManager()
    : QObject(nullptr)
{
    createWorkspace();
}

int LayoutManager::createWorkspace()
{
    int id = nextId++;
    workspaces.append({ id, Mode::Tiling, {} });
    return id;
}

void LayoutManager::setWorkspaceLayoutMode(int workspace, Mode mode)
{
    Workspace *ws = findWorkspace(workspace);
    if (ws) ws->mode = mode;
}

LayoutManager::Mode LayoutManager::getWorkspaceLayoutMode(int workspace) const
{
    for (const auto &ws : workspaces) {
        if (ws.id == workspace) return ws.mode;
    }
    return Mode::Tiling;
}

void LayoutManager::addWindow(Toplevel *toplevel, int workspace)
{
    Workspace *ws = findWorkspace(workspace);
    if (!ws) {
        ws = findWorkspace(1);
        if (!ws) return;
    }
    ws->windows.append({ toplevel, false, 0, 0, 0, 0 });
}

void LayoutManager::prependWindow(Toplevel *toplevel, int workspace)
{
    Workspace *ws = findWorkspace(workspace);
    if (!ws) return;
    ws->windows.prepend({ toplevel, false, 0, 0, 0, 0 });
}

void LayoutManager::removeWindow(Toplevel *toplevel)
{
    for (auto &ws : workspaces) {
        for (int i = 0; i < ws.windows.size(); i++) {
            if (ws.windows[i].toplevel == toplevel) {
                ws.windows.removeAt(i);
                return;
            }
        }
    }
}

void LayoutManager::raiseWindow(Toplevel *toplevel)
{
    for (auto &ws : workspaces) {
        for (int i = 0; i < ws.windows.size(); i++) {
            if (ws.windows[i].toplevel == toplevel) {
                auto state = ws.windows.takeAt(i);
                ws.windows.prepend(state);
                return;
            }
        }
    }
}

int LayoutManager::getWindowWorkspace(Toplevel *toplevel) const
{
    for (const auto &ws : workspaces) {
        for (const auto &w : ws.windows) {
            if (w.toplevel == toplevel) return ws.id;
        }
    }
    return -1;
}

void LayoutManager::arrange(struct wlr_output *output, int workspace)
{
    if (output == nullptr) return;
    Workspace *ws = findWorkspace(workspace);
    if (!ws || ws->windows.isEmpty()) return;

    switch (ws->mode) {
    case Mode::Tiling:
        arrangeTiling(ws, output);
        break;
    case Mode::Floating:
        arrangeFloating(ws, output);
        break;
    case Mode::MonoWindow:
        arrangeMonoWindow(ws, output);
        break;
    }
}

void LayoutManager::activateWorkspace(int workspace)
{
    int &refs = workspaceRefs[workspace];
    refs++;
    if (refs > 1) return;

    Workspace *ws = findWorkspace(workspace);
    if (!ws) return;
    for (auto &w : ws->windows)
        wlr_scene_node_set_enabled(&w.toplevel->getSceneTree()->node, true);
}

void LayoutManager::deactivateWorkspace(int workspace)
{
    auto it = workspaceRefs.find(workspace);
    if (it == workspaceRefs.end()) return;
    it.value()--;
    if (it.value() > 0) return;

    workspaceRefs.remove(workspace);
    Workspace *ws = findWorkspace(workspace);
    if (!ws) return;
    for (auto &w : ws->windows)
        wlr_scene_node_set_enabled(&w.toplevel->getSceneTree()->node, false);
}

LayoutManager::Workspace *LayoutManager::findWorkspace(int id)
{
    for (auto &ws : workspaces) {
        if (ws.id == id) return &ws;
    }
    return nullptr;
}

LayoutManager::Workspace *LayoutManager::findWorkspaceByWindow(Toplevel *toplevel)
{
    for (auto &ws : workspaces) {
        for (auto &w : ws.windows) {
            if (w.toplevel == toplevel) return &ws;
        }
    }
    return nullptr;
}

void LayoutManager::arrangeTiling(Workspace *ws, struct wlr_output *output)
{
    int width = output->width;
    int height = output->height;
    int count = ws->windows.size();
    int cols = std::ceil(std::sqrt(count));
    int rows = std::ceil((double)count / cols);
    int cell_w = width / cols;
    int cell_h = height / rows;

    for (int i = 0; i < count; i++) {
        auto &w = ws->windows[i];
        int col = i % cols;
        int row = i / cols;
        w.x = col * cell_w;
        w.y = row * cell_h;
        w.width = cell_w;
        w.height = cell_h;
        w.positioned = true;
        applyWindowGeometry(&w);
        wlr_scene_node_set_enabled(&w.toplevel->getSceneTree()->node, true);
    }
}

void LayoutManager::arrangeFloating(Workspace *ws, struct wlr_output *output)
{
    for (auto &w : ws->windows) {
        if (!w.positioned) {
            w.x = (output->width - 800) / 2;
            w.y = (output->height - 600) / 2;
            w.width = 800;
            w.height = 600;
            w.positioned = true;
            applyWindowGeometry(&w);
        }
        wlr_scene_node_set_enabled(&w.toplevel->getSceneTree()->node, true);
    }
}

void LayoutManager::arrangeMonoWindow(Workspace *ws, struct wlr_output *output)
{
    bool first = true;
    for (auto &w : ws->windows) {
        if (first) {
            w.x = 0;
            w.y = 0;
            w.width = output->width;
            w.height = output->height;
            w.positioned = true;
            applyWindowGeometry(&w);
            wlr_scene_node_set_enabled(&w.toplevel->getSceneTree()->node, true);
            first = false;
        } else {
            wlr_scene_node_set_enabled(&w.toplevel->getSceneTree()->node, false);
        }
    }
}

void LayoutManager::applyWindowGeometry(WindowState *state)
{
    wlr_scene_node_set_position(
        &state->toplevel->getSceneTree()->node, state->x, state->y);
    wlr_xdg_toplevel_set_size(
        state->toplevel->get(), state->width, state->height);
}
