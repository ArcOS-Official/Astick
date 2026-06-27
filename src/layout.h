#pragma once

#include <QObject>
#include <QList>
#include <QMap>
#include <qobject.h>
#include "wlroots.h"

class Toplevel;

class LayoutManager : public QObject
{
    Q_OBJECT

public:
    enum class Mode {
        Tiling,
        Floating,
        MonoWindow,
    };

    LayoutManager();

    int createWorkspace();
    void setWorkspaceLayoutMode(int workspace, Mode mode);
    Mode getWorkspaceLayoutMode(int workspace) const;

    void addWindow(Toplevel *toplevel, int workspace);
    void prependWindow(Toplevel *toplevel, int workspace);
    void removeWindow(Toplevel *toplevel);
    int getWindowWorkspace(Toplevel *toplevel) const;
    void raiseWindow(Toplevel *toplevel);

    void arrange(struct wlr_output *output, int workspace);

    void activateWorkspace(int workspace);
    void deactivateWorkspace(int workspace);

private:
    struct WindowState {
        Toplevel *toplevel;
        bool positioned;
        int x, y;
        int width, height;
    };

    struct Workspace {
        int id;
        Mode mode;
        QList<WindowState> windows;
    };

    QList<Workspace> workspaces;
    QMap<int, int> workspaceRefs;
    int nextId = 1;

    Workspace *findWorkspace(int id);
    Workspace *findWorkspaceByWindow(Toplevel *toplevel);
    void arrangeTiling(Workspace *ws, struct wlr_output *output);
    void arrangeFloating(Workspace *ws, struct wlr_output *output);
    void arrangeMonoWindow(Workspace *ws, struct wlr_output *output);
    void applyWindowGeometry(WindowState *state);
};
