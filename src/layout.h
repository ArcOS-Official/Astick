#pragma once

#include <QObject>
#include <QList>
#include <QMap>
#include <QHash>
#include <memory>
#include <vector>
#include <optional>
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

    enum class Orientation {
        Horizontal, // side-by-side (split along x)
        Vertical,   // stacked (split along y)
    };

    struct BspNode {
        enum class Type { Leaf, Branch } type;
        BspNode *parent = nullptr;

        // Leaf
        Toplevel *toplevel = nullptr;
        bool positioned = false;
        int x = 0, y = 0;
        int width = 0, height = 0;

        // Branch
        Orientation orientation = Orientation::Horizontal;
        double ratio = 0.5; // 0.1 .. 0.9
        std::unique_ptr<BspNode> left;
        std::unique_ptr<BspNode> right;

        static std::unique_ptr<BspNode> makeLeaf(Toplevel *tl, BspNode *par = nullptr) {
            auto n = std::make_unique<BspNode>();
            n->type = Type::Leaf;
            n->toplevel = tl;
            n->parent = par;
            n->positioned = false;
            return n;
        }
        static std::unique_ptr<BspNode> makeBranch(Orientation o, double r, std::unique_ptr<BspNode> l, std::unique_ptr<BspNode> rr, BspNode *par = nullptr) {
            auto n = std::make_unique<BspNode>();
            n->type = Type::Branch;
            n->orientation = o;
            n->ratio = r;
            n->left = std::move(l);
            n->right = std::move(rr);
            n->parent = par;
            if (n->left) n->left->parent = n.get();
            if (n->right) n->right->parent = n.get();
            return n;
        }
    };

    LayoutManager();

    int createWorkspace();
    void setWorkspaceLayoutMode(int workspace, Mode mode);
    Mode getWorkspaceLayoutMode(int workspace) const;

    void addWindow(Toplevel *toplevel, int workspace);
    // BSP-aware add with focused hint, usable for root orientation, cursor for fallback
    void addWindow(Toplevel *toplevel, int workspace, Toplevel *focused, struct wlr_box usable, double cursorX = 0, double cursorY = 0);
    void prependWindow(Toplevel *toplevel, int workspace);
    void insertWindowAt(Toplevel *toplevel, int workspace, int index);
    // BSP drop insertion: find leaf closest to cursor and split
    void insertWindowAtCursor(Toplevel *toplevel, int workspace, struct wlr_box usable, double cursorX, double cursorY, double ratioHint = -1);
    void removeWindow(Toplevel *toplevel);
    int getWindowWorkspace(Toplevel *toplevel) const;
    void raiseWindow(Toplevel *toplevel);
    int windowCount(int workspace) const;
    int tiledCount(int workspace) const;
    int floatingCount(int workspace) const;

    void arrange(struct wlr_output *output, int workspace);
    void arrange(struct wlr_box usable, int workspace);
    void arrange(struct wlr_box usable, struct wlr_box full, int workspace);
    void arrangeOthers(const struct wlr_box &usable, int workspace, Toplevel *excluded);
    bool updateWindowGeometry(Toplevel *toplevel, int x, int y, int width, int height);

    // Floating-in-tiling support
    struct FloatingWindow {
        Toplevel *toplevel = nullptr;
        int x = 0, y = 0, width = 0, height = 0;
        bool positioned = false;
    };
    bool isFloating(Toplevel *toplevel) const;
    bool isFloating(int workspace, Toplevel *toplevel) const;
    bool setFloating(Toplevel *toplevel, bool floating, struct wlr_box usable);
    bool toggleFloating(Toplevel *toplevel, struct wlr_box usable);
    bool updateFloatingGeometry(Toplevel *toplevel, int x, int y, int width, int height);
    std::optional<struct wlr_box> getFloatingGeometry(Toplevel *toplevel) const;
    std::vector<Toplevel*> getFloatingWindows(int workspace) const;

    // Fullscreen / maximize support (per-workspace, one window at a time)
    bool isFullscreen(Toplevel *toplevel) const;
    bool isMaximized(Toplevel *toplevel) const;
    bool setFullscreen(Toplevel *toplevel, bool fullscreen, struct wlr_box fullBox);
    bool setMaximized(Toplevel *toplevel, bool maximized);
    bool toggleFullscreen(Toplevel *toplevel, struct wlr_box fullBox);
    bool toggleMaximized(Toplevel *toplevel);
    Toplevel* getFullscreenWindow(int workspace) const;
    Toplevel* getMaximizedWindow(int workspace) const;

    void activateWorkspace(int workspace);
    void deactivateWorkspace(int workspace);

    // BSP operations
    bool toggleSplitOrientation(Toplevel *focused);
    bool toggleSplitOrientation(int workspace, Toplevel *focused);
    double getParentRatio(Toplevel *toplevel) const;
    bool setSplitRatio(Toplevel *toplevel, double ratio);
    bool handleResize(Toplevel *toplevel, struct wlr_box usable, double cursorX, double cursorY, uint32_t edges);
    bool commitResize(Toplevel *toplevel, struct wlr_box usable, uint32_t edges);
    // Decoration helpers: retrieve current leaf geometry for a window (for border positioning)
    bool getWindowGeometry(Toplevel *toplevel, struct wlr_box &out) const;
    bool getWindowGeometry(Toplevel *toplevel, int workspace, struct wlr_box usable, struct wlr_box &out) const;
    std::unordered_map<Toplevel*, struct wlr_box> snapshotGeometries(int workspace) const;
    std::unordered_map<Toplevel*, struct wlr_box> snapshotGeometries(int workspace, struct wlr_box usable) const;
    void applyGeometries(const std::unordered_map<Toplevel*, struct wlr_box> &boxes);
    void setLeafGeometry(Toplevel *tl, const struct wlr_box &box);

    // Configurables (tweakable)
    void setDefaultSplitRatio(double r) { defaultSplitRatio = r; }
    double getDefaultSplitRatio() const { return defaultSplitRatio; }
    void setOppositeOrientation(bool v) { oppositeOrientation = v; }
    bool getOppositeOrientation() const { return oppositeOrientation; }
    void setKeepRatioOnDrop(bool v) { keepRatioOnDrop = v; }
    bool getKeepRatioOnDrop() const { return keepRatioOnDrop; }
    void setMinRatio(double v) { minRatio = v; }
    void setMaxRatio(double v) { maxRatio = v; }

private:
    struct Workspace {
        int id;
        Mode mode;
        std::unique_ptr<BspNode> root;
        // For floating/mono fallback compatibility we still track order via root traversal
        std::vector<FloatingWindow> floating;
        Toplevel* fullscreenWindow = nullptr;
        Toplevel* maximizedWindow = nullptr;
    };

    std::vector<Workspace> workspaces;
    QMap<int, int> workspaceRefs;
    int nextId = 1;

    // tweakables
    double defaultSplitRatio = 0.5;
    bool oppositeOrientation = true;
    bool keepRatioOnDrop = true;
    double minRatio = 0.1;
    double maxRatio = 0.9;

    Workspace *findWorkspace(int id);
    const Workspace *findWorkspace(int id) const;
    Workspace *findWorkspaceByWindow(Toplevel *toplevel);
    const Workspace *findWorkspaceByWindow(Toplevel *toplevel) const;

    void arrangeTiling(Workspace *ws, struct wlr_box usable);
    void arrangeFloating(Workspace *ws, struct wlr_box usable);
    void arrangeMonoWindow(Workspace *ws, struct wlr_box usable);
    void applyWindowGeometry(BspNode *leaf);
    void applyFloatingGeometry(FloatingWindow &fw);
    FloatingWindow* findFloating(Workspace *ws, Toplevel *tl);
    const FloatingWindow* findFloating(const Workspace *ws, Toplevel *tl) const;
    void arrangeFloatingWindows(Workspace *ws, struct wlr_box usable);
    void arrangeFullscreen(Workspace *ws, struct wlr_box fullBox);
    void arrangeMaximized(Workspace *ws, struct wlr_box usable);
    void applyDirectGeometry(Toplevel *tl, struct wlr_box box);
    void setEnabledForWorkspace(Workspace *ws, bool enabled);

    // BSP helpers
    BspNode* findLeaf(BspNode *root, Toplevel *tl) const;
    BspNode* findLeafWithParent(BspNode *root, Toplevel *tl, BspNode **outParent) const;
    BspNode* findParentOfLeaf(BspNode *root, Toplevel *tl) const;
    int countLeaves(const BspNode *root) const;
    void collectLeaves(BspNode *root, std::vector<BspNode*> &out) const;
    void collectLeaves(const BspNode *root, std::vector<const BspNode*> &out) const;
    void setEnabledRecursive(BspNode *root, bool enabled);
    struct wlr_box getBoxForNode(BspNode *root, BspNode *target, struct wlr_box usable) const;
    bool getBoxForNodeRecursive(BspNode *node, BspNode *target, struct wlr_box box, struct wlr_box &out) const;
    BspNode* findBestLeafForInsertion(Workspace *ws, struct wlr_box usable, double cursorX, double cursorY) const;
    BspNode* findLeafClosestToPoint(BspNode *root, struct wlr_box usable, double cx, double cy) const;
    BspNode* findLeafLargestArea(BspNode *root, struct wlr_box usable) const;
    void arrangeNode(BspNode *node, struct wlr_box box);
    void arrangeNodeOthers(BspNode *node, struct wlr_box box, Toplevel *excluded);
    bool removeRecursive(std::unique_ptr<BspNode> &node, Toplevel *tl);
    bool replaceNode(std::unique_ptr<BspNode> &root, BspNode *target, std::unique_ptr<BspNode> replacement);
    static Orientation opposite(Orientation o) { return o == Orientation::Horizontal ? Orientation::Vertical : Orientation::Horizontal; }
    void getAllBoxesRecursive(BspNode *node, struct wlr_box box, std::vector<std::pair<BspNode*, struct wlr_box>> &out) const;
};
