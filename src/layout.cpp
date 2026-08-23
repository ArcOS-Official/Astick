#include "layout.h"
#include "toplevel.h"
#include <cmath>
#include <algorithm>
#include <limits>

LayoutManager::LayoutManager()
    : QObject(nullptr)
{
    createWorkspace();
}

int LayoutManager::createWorkspace()
{
    int id = nextId++;
    Workspace ws;
    ws.id = id;
    ws.mode = Mode::Tiling;
    ws.root = nullptr;
    workspaces.push_back(std::move(ws));
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

LayoutManager::Workspace *LayoutManager::findWorkspace(int id)
{
    for (auto &ws : workspaces) {
        if (ws.id == id) return &ws;
    }
    return nullptr;
}
const LayoutManager::Workspace *LayoutManager::findWorkspace(int id) const
{
    for (const auto &ws : workspaces) {
        if (ws.id == id) return &ws;
    }
    return nullptr;
}

LayoutManager::Workspace *LayoutManager::findWorkspaceByWindow(Toplevel *toplevel)
{
    for (auto &ws : workspaces) {
        if (ws.fullscreenWindow == toplevel) return &ws;
        if (ws.maximizedWindow == toplevel) return &ws;
        if (findLeaf(ws.root.get(), toplevel)) return &ws;
        for (auto &fw : ws.floating) if (fw.toplevel == toplevel) return &ws;
    }
    return nullptr;
}
const LayoutManager::Workspace *LayoutManager::findWorkspaceByWindow(Toplevel *toplevel) const
{
    for (const auto &ws : workspaces) {
        if (ws.fullscreenWindow == toplevel) return &ws;
        if (ws.maximizedWindow == toplevel) return &ws;
        if (findLeaf(ws.root.get(), toplevel)) return &ws;
        for (auto &fw : ws.floating) if (fw.toplevel == toplevel) return &ws;
    }
    return nullptr;
}

// BSP helpers

LayoutManager::BspNode* LayoutManager::findLeaf(BspNode *root, Toplevel *tl) const
{
    if (!root || !tl) return nullptr;
    if (root->type == BspNode::Type::Leaf) {
        return root->toplevel == tl ? root : nullptr;
    }
    if (auto *l = findLeaf(root->left.get(), tl)) return l;
    if (auto *r = findLeaf(root->right.get(), tl)) return r;
    return nullptr;
}

LayoutManager::BspNode* LayoutManager::findLeafWithParent(BspNode *root, Toplevel *tl, BspNode **outParent) const
{
    if (!root || !tl) return nullptr;
    if (root->type == BspNode::Type::Leaf) {
        return root->toplevel == tl ? root : nullptr;
    }
    // check direct children
    if (root->left && root->left->type == BspNode::Type::Leaf && root->left->toplevel == tl) {
        if (outParent) *outParent = root;
        return root->left.get();
    }
    if (root->right && root->right->type == BspNode::Type::Leaf && root->right->toplevel == tl) {
        if (outParent) *outParent = root;
        return root->right.get();
    }
    if (auto *l = findLeafWithParent(root->left.get(), tl, outParent)) return l;
    if (auto *r = findLeafWithParent(root->right.get(), tl, outParent)) return r;
    return nullptr;
}

LayoutManager::BspNode* LayoutManager::findParentOfLeaf(BspNode *root, Toplevel *tl) const
{
    BspNode *parent = nullptr;
    findLeafWithParent(root, tl, &parent);
    // For deeper leaves where leaf is not direct child of returned parent? Actually findLeafWithParent already finds correct parent for direct leaf. For nested structure where leaf is inside subtree, the parent is still its immediate branch. Our helper recurses, so it will find.
    // However if leaf is inside a branch subtree, the parent is that branch, handled above.
    // For leaves that are not direct children at current level, recursion will find appropriate parent.
    if (parent) return parent;
    // Fallback: brute search for any branch that contains leaf in its subtree and leaf is leaf child
    // Do explicit parent search via traversal keeping parent pointer
    std::function<BspNode*(BspNode*, BspNode*)> dfs = [&](BspNode* node, BspNode* par) -> BspNode* {
        if (!node) return nullptr;
        if (node->type == BspNode::Type::Leaf) return nullptr;
        if ((node->left && node->left->type == BspNode::Type::Leaf && node->left->toplevel == tl) ||
            (node->right && node->right->type == BspNode::Type::Leaf && node->right->toplevel == tl)) {
            return node;
        }
        if (auto *a = dfs(node->left.get(), node)) return a;
        if (auto *b = dfs(node->right.get(), node)) return b;
        return nullptr;
    };
    return dfs(root, nullptr);
}

int LayoutManager::countLeaves(const BspNode *root) const
{
    if (!root) return 0;
    if (root->type == BspNode::Type::Leaf) return 1;
    return countLeaves(root->left.get()) + countLeaves(root->right.get());
}

void LayoutManager::collectLeaves(BspNode *root, std::vector<BspNode*> &out) const
{
    if (!root) return;
    if (root->type == BspNode::Type::Leaf) { out.push_back(root); return; }
    collectLeaves(root->left.get(), out);
    collectLeaves(root->right.get(), out);
}
void LayoutManager::collectLeaves(const BspNode *root, std::vector<const BspNode*> &out) const
{
    if (!root) return;
    if (root->type == BspNode::Type::Leaf) { out.push_back(root); return; }
    collectLeaves(root->left.get(), out);
    collectLeaves(root->right.get(), out);
}

void LayoutManager::setEnabledRecursive(BspNode *root, bool enabled)
{
    if (!root) return;
    if (root->type == BspNode::Type::Leaf) {
        if (root->toplevel && root->toplevel->getSceneTree())
            wlr_scene_node_set_enabled(&root->toplevel->getSceneTree()->node, enabled);
        return;
    }
    setEnabledRecursive(root->left.get(), enabled);
    setEnabledRecursive(root->right.get(), enabled);
}

LayoutManager::FloatingWindow* LayoutManager::findFloating(Workspace *ws, Toplevel *tl)
{
    if (!ws || !tl) return nullptr;
    for (auto &fw : ws->floating) if (fw.toplevel == tl) return &fw;
    return nullptr;
}
const LayoutManager::FloatingWindow* LayoutManager::findFloating(const Workspace *ws, Toplevel *tl) const
{
    if (!ws || !tl) return nullptr;
    for (auto &fw : ws->floating) if (fw.toplevel == tl) return &fw;
    return nullptr;
}

void LayoutManager::applyFloatingGeometry(FloatingWindow &fw)
{
    if (!fw.toplevel || !fw.toplevel->getSceneTree()) return;
    wlr_scene_node_set_position(&fw.toplevel->getSceneTree()->node, fw.x, fw.y);
    wlr_xdg_toplevel_set_size(fw.toplevel->get(), fw.width, fw.height);
}

void LayoutManager::arrangeFloatingWindows(Workspace *ws, struct wlr_box usable)
{
    for (auto &fw : ws->floating) {
        bool needsApply = false;
        if (!fw.positioned) {
            fw.width = std::min(800, usable.width);
            fw.height = std::min(600, usable.height);
            fw.x = usable.x + (usable.width - fw.width) / 2;
            fw.y = usable.y + (usable.height - fw.height) / 2;
            fw.positioned = true;
            needsApply = true;
        } else {
            if (fw.width > usable.width) { fw.width = usable.width; needsApply = true; }
            if (fw.height > usable.height) { fw.height = usable.height; needsApply = true; }
            int nx = fw.x;
            int ny = fw.y;
            if (nx < usable.x) nx = usable.x;
            if (ny < usable.y) ny = usable.y;
            if (nx + fw.width > usable.x + usable.width) nx = usable.x + usable.width - fw.width;
            if (ny + fw.height > usable.y + usable.height) ny = usable.y + usable.height - fw.height;
            if (nx != fw.x || ny != fw.y) { fw.x = nx; fw.y = ny; needsApply = true; }
        }
        if (needsApply) applyFloatingGeometry(fw);
        if (fw.toplevel && fw.toplevel->getSceneTree())
            wlr_scene_node_set_enabled(&fw.toplevel->getSceneTree()->node, true);
    }
}

void LayoutManager::getAllBoxesRecursive(BspNode *node, struct wlr_box box, std::vector<std::pair<BspNode*, struct wlr_box>> &out) const
{
    if (!node) return;
    if (node->type == BspNode::Type::Leaf) {
        out.emplace_back(node, box);
        return;
    }
    double r = std::clamp(node->ratio, minRatio, maxRatio);
    if (node->orientation == Orientation::Horizontal) {
        int lw = (int)(box.width * r);
        int rw = box.width - lw;
        struct wlr_box lb = { box.x, box.y, lw, box.height };
        struct wlr_box rb = { box.x + lw, box.y, rw, box.height };
        getAllBoxesRecursive(node->left.get(), lb, out);
        getAllBoxesRecursive(node->right.get(), rb, out);
    } else {
        int th = (int)(box.height * r);
        int bh = box.height - th;
        struct wlr_box tb = { box.x, box.y, box.width, th };
        struct wlr_box bb = { box.x, box.y + th, box.width, bh };
        getAllBoxesRecursive(node->left.get(), tb, out);
        getAllBoxesRecursive(node->right.get(), bb, out);
    }
}

bool LayoutManager::getBoxForNodeRecursive(BspNode *node, BspNode *target, struct wlr_box box, struct wlr_box &out) const
{
    if (!node) return false;
    if (node == target) { out = box; return true; }
    if (node->type == BspNode::Type::Leaf) return false;
    double r = std::clamp(node->ratio, minRatio, maxRatio);
    if (node->orientation == Orientation::Horizontal) {
        int lw = (int)(box.width * r);
        int rw = box.width - lw;
        struct wlr_box lb = { box.x, box.y, lw, box.height };
        struct wlr_box rb = { box.x + lw, box.y, rw, box.height };
        if (getBoxForNodeRecursive(node->left.get(), target, lb, out)) return true;
        if (getBoxForNodeRecursive(node->right.get(), target, rb, out)) return true;
    } else {
        int th = (int)(box.height * r);
        int bh = box.height - th;
        struct wlr_box tb = { box.x, box.y, box.width, th };
        struct wlr_box bb = { box.x, box.y + th, box.width, bh };
        if (getBoxForNodeRecursive(node->left.get(), target, tb, out)) return true;
        if (getBoxForNodeRecursive(node->right.get(), target, bb, out)) return true;
    }
    return false;
}

struct wlr_box LayoutManager::getBoxForNode(BspNode *root, BspNode *target, struct wlr_box usable) const
{
    struct wlr_box out = usable;
    getBoxForNodeRecursive(root, target, usable, out);
    return out;
}

LayoutManager::BspNode* LayoutManager::findLeafClosestToPoint(BspNode *root, struct wlr_box usable, double cx, double cy) const
{
    if (!root) return nullptr;
    std::vector<std::pair<BspNode*, struct wlr_box>> boxes;
    getAllBoxesRecursive(root, usable, boxes);
    if (boxes.empty()) return nullptr;
    BspNode *best = nullptr;
    double bestDist = std::numeric_limits<double>::max();
    for (auto &p : boxes) {
        auto *node = p.first;
        auto box = p.second;
        double bx = box.x + box.width / 2.0;
        double by = box.y + box.height / 2.0;
        double dx = cx - bx;
        double dy = cy - by;
        double dist = dx*dx + dy*dy;
        if (dist < bestDist) {
            bestDist = dist;
            best = node;
        }
    }
    return best;
}

LayoutManager::BspNode* LayoutManager::findLeafLargestArea(BspNode *root, struct wlr_box usable) const
{
    if (!root) return nullptr;
    std::vector<std::pair<BspNode*, struct wlr_box>> boxes;
    getAllBoxesRecursive(root, usable, boxes);
    if (boxes.empty()) return nullptr;
    BspNode *best = nullptr;
    int bestArea = -1;
    for (auto &p : boxes) {
        int area = p.second.width * p.second.height;
        if (area > bestArea) {
            bestArea = area;
            best = p.first;
        }
    }
    return best;
}

LayoutManager::BspNode* LayoutManager::findBestLeafForInsertion(Workspace *ws, struct wlr_box usable, double cursorX, double cursorY) const
{
    if (!ws || !ws->root) return nullptr;
    // Policy: closest to mouse but prefer largest space. We blend: find leaf closest to cursor; if cursor is within some leaf, prefer that; otherwise choose leaf with minimal distance weighted by area.
    // Simple: if cursor inside usable, find closest; else largest area.
    // For now choose closest; break ties by largest area among near candidates.

    // If usable is empty (0 size) fallback to largest
    if (usable.width <= 0 || usable.height <= 0) {
        return findLeafLargestArea(ws->root.get(), usable);
    }
    // Find all boxes
    std::vector<std::pair<BspNode*, struct wlr_box>> boxes;
    getAllBoxesRecursive(ws->root.get(), usable, boxes);
    if (boxes.empty()) return nullptr;

    // Check if cursor inside any leaf: immediately return that leaf (closest containment)
    for (auto &p : boxes) {
        auto b = p.second;
        if (cursorX >= b.x && cursorX < b.x + b.width && cursorY >= b.y && cursorY < b.y + b.height) {
            return p.first;
        }
    }

    // Else find best trade-off: score = distance - k*areaFactor ; we approximate by distance then area
    BspNode *best = nullptr;
    double bestScore = std::numeric_limits<double>::max();
    // Determine max area for normalization
    int maxArea = 0;
    for (auto &p : boxes) maxArea = std::max(maxArea, p.second.width * p.second.height);
    if (maxArea == 0) maxArea = 1;
    for (auto &p : boxes) {
        auto b = p.second;
        double bx = b.x + b.width / 2.0;
        double by = b.y + b.height / 2.0;
        double dx = cursorX - bx;
        double dy = cursorY - by;
        double dist = std::sqrt(dx*dx + dy*dy);
        double area = b.width * b.height;
        // Score: distance penalized, larger area rewarded (subtract)
        // Weight area influence modest: 200px equivalent for max area
        double areaBonus = (area / (double)maxArea) * 150.0;
        double score = dist - areaBonus;
        if (score < bestScore) {
            bestScore = score;
            best = p.first;
        }
    }
    return best;
}

bool LayoutManager::replaceNode(std::unique_ptr<BspNode> &root, BspNode *target, std::unique_ptr<BspNode> replacement)
{
    if (!root) return false;
    if (root.get() == target) {
        // replacement's parent should be target's parent (caller will set)
        replacement->parent = target->parent;
        root = std::move(replacement);
        return true;
    }
    if (root->type == BspNode::Type::Branch) {
        if (replaceNode(root->left, target, std::move(replacement))) return true;
        if (replaceNode(root->right, target, std::move(replacement))) return true;
    }
    return false;
}

// Public API

void LayoutManager::addWindow(Toplevel *toplevel, int workspace)
{
    // Fallback simple add: create usable dummy 1920x1080 for orientation decisions
    struct wlr_box dummy = {0,0,1920,1080};
    addWindow(toplevel, workspace, nullptr, dummy, 0, 0);
}

void LayoutManager::addWindow(Toplevel *toplevel, int workspace, Toplevel *focused, struct wlr_box usable, double cursorX, double cursorY)
{
    Workspace *ws = findWorkspace(workspace);
    if (!ws) {
        ws = findWorkspace(1);
        if (!ws) return;
    }
    if (!toplevel) return;

    // Prevent duplicate
    if (findLeaf(ws->root.get(), toplevel)) return;

    if (!ws->root) {
        ws->root = BspNode::makeLeaf(toplevel, nullptr);
        return;
    }

    // Find leaf to split
    BspNode *leafToSplit = nullptr;
    BspNode *parentOfLeaf = nullptr;
    if (focused) {
        leafToSplit = findLeaf(ws->root.get(), focused);
        if (leafToSplit) {
            parentOfLeaf = leafToSplit->parent;
        }
    }
    if (!leafToSplit) {
        // No focused in this workspace: use heuristic fallback
        // If ws has root but no focused, choose best leaf via mouse/area
        leafToSplit = findBestLeafForInsertion(ws, usable, cursorX, cursorY);
        if (!leafToSplit) {
            // As fallback, pick any leaf
            std::vector<BspNode*> leaves;
            collectLeaves(ws->root.get(), leaves);
            if (!leaves.empty()) leafToSplit = leaves.front();
        }
        if (leafToSplit) parentOfLeaf = leafToSplit->parent;
    }
    if (!leafToSplit) {
        // Should not happen; just make new root branch with existing root and new leaf
        // But root exists so this is error fallback: wrap root
        auto newLeaf = BspNode::makeLeaf(toplevel, nullptr);
        Orientation o = (usable.width >= usable.height) ? Orientation::Horizontal : Orientation::Vertical;
        if (parentOfLeaf && oppositeOrientation) {
            // parent is null here, so use larger axis
        }
        double r = std::clamp(defaultSplitRatio, minRatio, maxRatio);
        auto branch = BspNode::makeBranch(o, r, std::move(ws->root), std::move(newLeaf), nullptr);
        ws->root = std::move(branch);
        return;
    }

    // Determine orientation for new branch
    Orientation newOrient;
    if (parentOfLeaf) {
        if (oppositeOrientation)
            newOrient = opposite(parentOfLeaf->orientation);
        else
            newOrient = parentOfLeaf->orientation;
    } else {
        // leaf was root: use larger axis
        newOrient = (usable.width >= usable.height) ? Orientation::Horizontal : Orientation::Vertical;
    }
    double ratio = std::clamp(defaultSplitRatio, minRatio, maxRatio);

    struct wlr_box leafBox = getBoxForNode(ws->root.get(), leafToSplit, usable);
    bool placeLeftTop = false;
    if (newOrient == Orientation::Horizontal) {
        double centreX = leafBox.x + leafBox.width / 2.0;
        placeLeftTop = cursorX < centreX;
    } else {
        double centreY = leafBox.y + leafBox.height / 2.0;
        placeLeftTop = cursorY < centreY;
    }

    // Create new leaf for new window
    auto newLeaf = BspNode::makeLeaf(toplevel, nullptr);
    // Need to extract leafToSplit from its owning unique_ptr and replace with branch
    // Create branch that will own old leaf and new leaf
    // We'll create branch and then move leafToSplit ownership.

    // Helper to find and replace leafToSplit with branch
    std::function<bool(std::unique_ptr<BspNode>&)> doSplit = [&](std::unique_ptr<BspNode> &node) -> bool {
        if (!node) return false;
        if (node.get() == leafToSplit) {
            // leafToSplit is this node; replace it with branch
            // Move ownership of leaf node
            std::unique_ptr<BspNode> oldLeaf = std::move(node);
            // oldLeaf parent will be branch
            auto branch = std::make_unique<BspNode>();
            branch->type = BspNode::Type::Branch;
            branch->orientation = newOrient;
            branch->ratio = ratio;
            branch->parent = oldLeaf->parent; // grandparent
            oldLeaf->parent = branch.get();
            newLeaf->parent = branch.get();
            if (placeLeftTop) {
                branch->left = std::move(newLeaf);
                branch->right = std::move(oldLeaf);
            } else {
                branch->left = std::move(oldLeaf);
                branch->right = std::move(newLeaf);
            }
            node = std::move(branch);
            return true;
        }
        if (node->type == BspNode::Type::Branch) {
            if (doSplit(node->left)) return true;
            if (doSplit(node->right)) return true;
        }
        return false;
    };

    bool ok = doSplit(ws->root);
    if (!ok) {
        // fallback: shouldn't happen, just log
    }
}

void LayoutManager::prependWindow(Toplevel *toplevel, int workspace)
{
    // For BSP, prepend is same as add but tries to put new window as first leaf (leftmost)
    // We'll just call addWindow with no focused (fallback will pick first leaf closest to origin)
    Workspace *ws = findWorkspace(workspace);
    if (!ws) return;
    if (!ws->root) {
        ws->root = BspNode::makeLeaf(toplevel, nullptr);
        return;
    }
    struct wlr_box dummy = {0,0,1920,1080};
    // Find leftmost leaf (first in collection)
    std::vector<BspNode*> leaves;
    collectLeaves(ws->root.get(), leaves);
    if (!leaves.empty()) {
        // pick first leaf
        Toplevel *target = leaves.front()->toplevel;
        addWindow(toplevel, workspace, target, dummy, 0, 0);
    } else {
        addWindow(toplevel, workspace, nullptr, dummy, 0, 0);
    }
}

void LayoutManager::insertWindowAt(Toplevel *toplevel, int workspace, int index)
{
    // For BSP, index is not meaningful; map index to leaf position by traversal order
    Workspace *ws = findWorkspace(workspace);
    if (!ws) return;
    if (!ws->root) {
        ws->root = BspNode::makeLeaf(toplevel, nullptr);
        return;
    }
    std::vector<BspNode*> leaves;
    collectLeaves(ws->root.get(), leaves);
    if (leaves.empty()) {
        ws->root = BspNode::makeLeaf(toplevel, nullptr);
        return;
    }
    if (index < 0) index = 0;
    if (index >= (int)leaves.size()) index = leaves.size() - 1;
    Toplevel *target = leaves[index]->toplevel;
    struct wlr_box dummy = {0,0,1920,1080};
    addWindow(toplevel, workspace, target, dummy, 0, 0);
}

void LayoutManager::insertWindowAtCursor(Toplevel *toplevel, int workspace, struct wlr_box usable, double cursorX, double cursorY, double ratioHint)
{
    Workspace *ws = findWorkspace(workspace);
    if (!ws) {
        ws = findWorkspace(1);
        if (!ws) return;
    }
    if (!toplevel) return;
    if (findLeaf(ws->root.get(), toplevel)) return;
    if (!ws->root) {
        ws->root = BspNode::makeLeaf(toplevel, nullptr);
        return;
    }
    BspNode *leaf = findBestLeafForInsertion(ws, usable, cursorX, cursorY);
    if (!leaf) {
        std::vector<BspNode*> leaves;
        collectLeaves(ws->root.get(), leaves);
        if (!leaves.empty()) leaf = leaves.front();
        else {
            ws->root = BspNode::makeLeaf(toplevel, nullptr);
            return;
        }
    }
    BspNode *parent = leaf->parent;
    Orientation newOrient;
    if (parent) {
        if (oppositeOrientation) newOrient = opposite(parent->orientation);
        else newOrient = parent->orientation;
    } else {
        newOrient = (usable.width >= usable.height) ? Orientation::Horizontal : Orientation::Vertical;
    }
    double ratio = ratioHint > 0 ? std::clamp(ratioHint, minRatio, maxRatio) : std::clamp(defaultSplitRatio, minRatio, maxRatio);

    struct wlr_box leafBox = getBoxForNode(ws->root.get(), leaf, usable);
    bool placeLeftTop = false;
    if (newOrient == Orientation::Horizontal) {
        double centreX = leafBox.x + leafBox.width / 2.0;
        placeLeftTop = cursorX < centreX;
    } else {
        double centreY = leafBox.y + leafBox.height / 2.0;
        placeLeftTop = cursorY < centreY;
    }

    auto newLeaf = BspNode::makeLeaf(toplevel, nullptr);
    std::function<bool(std::unique_ptr<BspNode>&)> doSplit = [&](std::unique_ptr<BspNode> &node) -> bool {
        if (!node) return false;
        if (node.get() == leaf) {
            std::unique_ptr<BspNode> oldLeaf = std::move(node);
            auto branch = std::make_unique<BspNode>();
            branch->type = BspNode::Type::Branch;
            branch->orientation = newOrient;
            branch->ratio = ratio;
            branch->parent = oldLeaf->parent;
            oldLeaf->parent = branch.get();
            newLeaf->parent = branch.get();
            if (placeLeftTop) {
                branch->left = std::move(newLeaf);
                branch->right = std::move(oldLeaf);
            } else {
                branch->left = std::move(oldLeaf);
                branch->right = std::move(newLeaf);
            }
            node = std::move(branch);
            return true;
        }
        if (node->type == BspNode::Type::Branch) {
            if (doSplit(node->left)) return true;
            if (doSplit(node->right)) return true;
        }
        return false;
    };
    doSplit(ws->root);
}

bool LayoutManager::removeRecursive(std::unique_ptr<BspNode> &node, Toplevel *tl)
{
    if (!node) return false;
    if (node->type == BspNode::Type::Leaf) {
        if (node->toplevel == tl) {
            // Leaf to be removed: signal to parent that this leaf should be removed.
            // This case is handled by parent branch collapsing, not here.
            // Return true to indicate found, but caller (parent) will handle.
            return true;
        }
        return false;
    }
    // Branch
    bool leftFound = false, rightFound = false;
    if (node->left) {
        if (node->left->type == BspNode::Type::Leaf && node->left->toplevel == tl) {
            leftFound = true;
        } else if (removeRecursive(node->left, tl)) {
            // If child branch reported removal and collapsed, we need to propagate? Actually removal of deep leaf would have collapsed that child branch already.
            // But our collapse logic below handles only direct leaf children.
            // For deeper, the recursion would have collapsed subtree, so no need to act here.
            return true;
        }
    }
    if (!leftFound && node->right) {
        if (node->right->type == BspNode::Type::Leaf && node->right->toplevel == tl) {
            rightFound = true;
        } else if (removeRecursive(node->right, tl)) {
            return true;
        }
    }
    if (leftFound || rightFound) {
        // Collapse this branch: promote sibling
        std::unique_ptr<BspNode> sibling;
        BspNode *parentOfBranch = node->parent;
        if (leftFound) {
            sibling = std::move(node->right);
        } else {
            sibling = std::move(node->left);
        }
        if (sibling) sibling->parent = parentOfBranch;
        node = std::move(sibling);
        return true;
    }
    return false;
}

void LayoutManager::removeWindow(Toplevel *toplevel)
{
    for (auto &ws : workspaces) {
        if (ws.fullscreenWindow == toplevel) ws.fullscreenWindow = nullptr;
        if (ws.maximizedWindow == toplevel) ws.maximizedWindow = nullptr;
        // Check floating list first
        for (auto it = ws.floating.begin(); it != ws.floating.end(); ++it) {
            if (it->toplevel == toplevel) {
                ws.floating.erase(it);
                return;
            }
        }
        if (!ws.root) continue;
        // Special case: root is leaf and matches
        if (ws.root->type == BspNode::Type::Leaf && ws.root->toplevel == toplevel) {
            ws.root.reset();
            return;
        }
        if (removeRecursive(ws.root, toplevel)) {
            return;
        }
    }
}

int LayoutManager::windowCount(int workspace) const
{
    const Workspace *ws = findWorkspace(workspace);
    if (!ws) return 0;
    return countLeaves(ws->root.get()) + (int)ws->floating.size();
}

int LayoutManager::tiledCount(int workspace) const
{
    const Workspace *ws = findWorkspace(workspace);
    if (!ws) return 0;
    return countLeaves(ws->root.get());
}

int LayoutManager::floatingCount(int workspace) const
{
    const Workspace *ws = findWorkspace(workspace);
    if (!ws) return 0;
    return (int)ws->floating.size();
}

int LayoutManager::getWindowWorkspace(Toplevel *toplevel) const
{
    for (const auto &ws : workspaces) {
        if (ws.fullscreenWindow == toplevel) return ws.id;
        if (ws.maximizedWindow == toplevel) return ws.id;
        if (findLeaf(ws.root.get(), toplevel)) return ws.id;
        for (auto &fw : ws.floating) if (fw.toplevel == toplevel) return ws.id;
    }
    return -1;
}

void LayoutManager::raiseWindow(Toplevel *toplevel)
{
    // For floating windows in tiling, keep logical stacking order (vector order)
    // Scene stacking is handled by compositor (place_below popupTree) so we only
    // reorder the vector here and don't touch scene nodes.
    for (auto &ws : workspaces) {
        for (auto &fw : ws.floating) {
            if (fw.toplevel == toplevel) {
                for (auto it = ws.floating.begin(); it != ws.floating.end(); ++it) {
                    if (it->toplevel == toplevel) {
                        FloatingWindow tmp = *it;
                        ws.floating.erase(it);
                        ws.floating.push_back(tmp);
                        break;
                    }
                }
                return;
            }
        }
    }
    (void)toplevel;
}

void LayoutManager::arrange(struct wlr_output *output, int workspace)
{
    if (output == nullptr) return;
    struct wlr_box usable = {0, 0, output->width, output->height};
    arrange(usable, workspace);
}

void LayoutManager::arrange(struct wlr_box usable, int workspace)
{
    // Backwards compat: use usable as full for fullscreen fallback
    arrange(usable, usable, workspace);
}

void LayoutManager::arrange(struct wlr_box usable, struct wlr_box full, int workspace)
{
    Workspace *ws = findWorkspace(workspace);
    if (!ws) return;
    if (!ws->root && ws->floating.empty() && !ws->fullscreenWindow && !ws->maximizedWindow) return;
    // Fullscreen takes precedence over everything (covers whole output, hides shell)
    if (ws->fullscreenWindow) {
        arrangeFullscreen(ws, full);
        return;
    }
    // Maximized takes precedence over tiling/floating/mono (shows shell, fills usable)
    if (ws->maximizedWindow) {
        arrangeMaximized(ws, usable);
        return;
    }
    switch (ws->mode) {
    case Mode::Tiling:
        arrangeTiling(ws, usable);
        break;
    case Mode::Floating:
        arrangeFloating(ws, usable);
        break;
    case Mode::MonoWindow:
        arrangeMonoWindow(ws, usable);
        break;
    }
}

void LayoutManager::applyDirectGeometry(Toplevel *tl, struct wlr_box box)
{
    if (!tl || !tl->getSceneTree()) return;
    wlr_scene_node_set_position(&tl->getSceneTree()->node, box.x, box.y);
    wlr_xdg_toplevel_set_size(tl->get(), box.width, box.height);
}

void LayoutManager::setEnabledForWorkspace(Workspace *ws, bool enabled)
{
    if (!ws) return;
    setEnabledRecursive(ws->root.get(), enabled);
    for (auto &fw : ws->floating) {
        if (fw.toplevel && fw.toplevel->getSceneTree())
            wlr_scene_node_set_enabled(&fw.toplevel->getSceneTree()->node, enabled);
    }
}

void LayoutManager::arrangeFullscreen(Workspace *ws, struct wlr_box fullBox)
{
    if (!ws || !ws->fullscreenWindow) return;
    Toplevel *fs = ws->fullscreenWindow;
    // Hide all tiled leaves except fullscreen window (if it is tiled, it will be hidden then re-enabled as fullscreen)
    std::vector<BspNode*> leaves;
    collectLeaves(ws->root.get(), leaves);
    for (auto *leaf : leaves) {
        if (leaf->toplevel == fs) continue;
        if (leaf->toplevel && leaf->toplevel->getSceneTree())
            wlr_scene_node_set_enabled(&leaf->toplevel->getSceneTree()->node, false);
    }
    for (auto &fw : ws->floating) {
        if (fw.toplevel == fs) continue;
        if (fw.toplevel && fw.toplevel->getSceneTree())
            wlr_scene_node_set_enabled(&fw.toplevel->getSceneTree()->node, false);
    }
    // Hide maximized window if different
    if (ws->maximizedWindow && ws->maximizedWindow != fs) {
        if (ws->maximizedWindow->getSceneTree())
            wlr_scene_node_set_enabled(&ws->maximizedWindow->getSceneTree()->node, false);
    }
    // Position fullscreen window to full output (shell hidden via compositor disabling layers)
    applyDirectGeometry(fs, fullBox);
    if (fs->getSceneTree()) {
        wlr_scene_node_set_enabled(&fs->getSceneTree()->node, true);
    }
    // Also ensure fullscreen window is not considered tiled geometry overwritten: we don't update leaf->x
}

void LayoutManager::arrangeMaximized(Workspace *ws, struct wlr_box usable)
{
    if (!ws || !ws->maximizedWindow) return;
    Toplevel *mx = ws->maximizedWindow;
    std::vector<BspNode*> leaves;
    collectLeaves(ws->root.get(), leaves);
    for (auto *leaf : leaves) {
        if (leaf->toplevel == mx) continue;
        if (leaf->toplevel && leaf->toplevel->getSceneTree())
            wlr_scene_node_set_enabled(&leaf->toplevel->getSceneTree()->node, false);
    }
    for (auto &fw : ws->floating) {
        if (fw.toplevel == mx) continue;
        if (fw.toplevel && fw.toplevel->getSceneTree())
            wlr_scene_node_set_enabled(&fw.toplevel->getSceneTree()->node, false);
    }
    if (ws->fullscreenWindow && ws->fullscreenWindow != mx) {
        if (ws->fullscreenWindow->getSceneTree())
            wlr_scene_node_set_enabled(&ws->fullscreenWindow->getSceneTree()->node, false);
    }
    applyDirectGeometry(mx, usable);
    if (mx->getSceneTree()) {
        wlr_scene_node_set_enabled(&mx->getSceneTree()->node, true);
    }
}

void LayoutManager::activateWorkspace(int workspace)
{
    int &refs = workspaceRefs[workspace];
    refs++;
    if (refs > 1) return;
    Workspace *ws = findWorkspace(workspace);
    if (!ws) return;
    setEnabledRecursive(ws->root.get(), true);
    for (auto &fw : ws->floating) {
        if (fw.toplevel && fw.toplevel->getSceneTree())
            wlr_scene_node_set_enabled(&fw.toplevel->getSceneTree()->node, true);
    }
    if (ws->fullscreenWindow && ws->fullscreenWindow->getSceneTree())
        wlr_scene_node_set_enabled(&ws->fullscreenWindow->getSceneTree()->node, true);
    if (ws->maximizedWindow && ws->maximizedWindow->getSceneTree())
        wlr_scene_node_set_enabled(&ws->maximizedWindow->getSceneTree()->node, true);
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
    setEnabledRecursive(ws->root.get(), false);
    for (auto &fw : ws->floating) {
        if (fw.toplevel && fw.toplevel->getSceneTree())
            wlr_scene_node_set_enabled(&fw.toplevel->getSceneTree()->node, false);
    }
    if (ws->fullscreenWindow && ws->fullscreenWindow->getSceneTree())
        wlr_scene_node_set_enabled(&ws->fullscreenWindow->getSceneTree()->node, false);
    if (ws->maximizedWindow && ws->maximizedWindow->getSceneTree())
        wlr_scene_node_set_enabled(&ws->maximizedWindow->getSceneTree()->node, false);
}

void LayoutManager::arrangeNode(BspNode *node, struct wlr_box box)
{
    if (!node) return;
    if (node->type == BspNode::Type::Leaf) {
        node->x = box.x;
        node->y = box.y;
        node->width = box.width;
        node->height = box.height;
        node->positioned = true;
        applyWindowGeometry(node);
        if (node->toplevel && node->toplevel->getSceneTree())
            wlr_scene_node_set_enabled(&node->toplevel->getSceneTree()->node, true);
        return;
    }
    double r = std::clamp(node->ratio, minRatio, maxRatio);
    if (node->orientation == Orientation::Horizontal) {
        int lw = (int)(box.width * r);
        int rw = box.width - lw;
        struct wlr_box lb = { box.x, box.y, lw, box.height };
        struct wlr_box rb = { box.x + lw, box.y, rw, box.height };
        arrangeNode(node->left.get(), lb);
        arrangeNode(node->right.get(), rb);
    } else {
        int th = (int)(box.height * r);
        int bh = box.height - th;
        struct wlr_box tb = { box.x, box.y, box.width, th };
        struct wlr_box bb = { box.x, box.y + th, box.width, bh };
        arrangeNode(node->left.get(), tb);
        arrangeNode(node->right.get(), bb);
    }
}

void LayoutManager::arrangeTiling(Workspace *ws, struct wlr_box usable)
{
    int width = usable.width;
    int height = usable.height;
    if (width <= 0 || height <= 0) return;
    if (ws->root) {
        arrangeNode(ws->root.get(), usable);
    }
    // Arrange floating windows on top of tiled layout
    arrangeFloatingWindows(ws, usable);
}

void LayoutManager::arrangeFloating(Workspace *ws, struct wlr_box usable)
{
    std::vector<BspNode*> leaves;
    collectLeaves(ws->root.get(), leaves);
    for (auto *leaf : leaves) {
        bool needsApply = false;
        if (!leaf->positioned) {
            leaf->width = std::min(800, usable.width);
            leaf->height = std::min(600, usable.height);
            leaf->x = usable.x + (usable.width - leaf->width) / 2;
            leaf->y = usable.y + (usable.height - leaf->height) / 2;
            leaf->positioned = true;
            needsApply = true;
        } else {
            if (leaf->width > usable.width) { leaf->width = usable.width; needsApply = true; }
            if (leaf->height > usable.height) { leaf->height = usable.height; needsApply = true; }
            int nx = leaf->x;
            int ny = leaf->y;
            if (nx < usable.x) nx = usable.x;
            if (ny < usable.y) ny = usable.y;
            if (nx + leaf->width > usable.x + usable.width) nx = usable.x + usable.width - leaf->width;
            if (ny + leaf->height > usable.y + usable.height) ny = usable.y + usable.height - leaf->height;
            if (nx != leaf->x || ny != leaf->y) { leaf->x = nx; leaf->y = ny; needsApply = true; }
        }
        if (needsApply) applyWindowGeometry(leaf);
        if (leaf->toplevel && leaf->toplevel->getSceneTree())
            wlr_scene_node_set_enabled(&leaf->toplevel->getSceneTree()->node, true);
    }
    arrangeFloatingWindows(ws, usable);
}

void LayoutManager::arrangeMonoWindow(Workspace *ws, struct wlr_box usable)
{
    std::vector<BspNode*> leaves;
    collectLeaves(ws->root.get(), leaves);
    bool first = true;
    for (auto *leaf : leaves) {
        if (first) {
            leaf->x = usable.x;
            leaf->y = usable.y;
            leaf->width = usable.width;
            leaf->height = usable.height;
            leaf->positioned = true;
            applyWindowGeometry(leaf);
            if (leaf->toplevel && leaf->toplevel->getSceneTree())
                wlr_scene_node_set_enabled(&leaf->toplevel->getSceneTree()->node, true);
            first = false;
        } else {
            if (leaf->toplevel && leaf->toplevel->getSceneTree())
                wlr_scene_node_set_enabled(&leaf->toplevel->getSceneTree()->node, false);
        }
    }
    // Floating windows stay visible on top even in monowindow mode
    arrangeFloatingWindows(ws, usable);
}

void LayoutManager::applyWindowGeometry(BspNode *leaf)
{
    if (!leaf || !leaf->toplevel) return;
    wlr_scene_node_set_position(&leaf->toplevel->getSceneTree()->node, leaf->x, leaf->y);
    wlr_xdg_toplevel_set_size(leaf->toplevel->get(), leaf->width, leaf->height);
}

bool LayoutManager::updateWindowGeometry(Toplevel *toplevel, int x, int y, int width, int height)
{
    for (auto &ws : workspaces) {
        if (auto *fw = findFloating(&ws, toplevel)) {
            fw->x = x;
            fw->y = y;
            fw->width = width;
            fw->height = height;
            fw->positioned = true;
            return true;
        }
        BspNode *leaf = findLeaf(ws.root.get(), toplevel);
        if (leaf) {
            leaf->x = x;
            leaf->y = y;
            leaf->width = width;
            leaf->height = height;
            leaf->positioned = true;
            return true;
        }
    }
    return false;
}

bool LayoutManager::isFloating(Toplevel *toplevel) const
{
    for (auto &ws : workspaces) {
        if (findFloating(&ws, toplevel)) return true;
    }
    return false;
}

bool LayoutManager::isFloating(int workspace, Toplevel *toplevel) const
{
    const Workspace *ws = findWorkspace(workspace);
    if (!ws) return false;
    return findFloating(ws, toplevel) != nullptr;
}

bool LayoutManager::updateFloatingGeometry(Toplevel *toplevel, int x, int y, int width, int height)
{
    for (auto &ws : workspaces) {
        if (auto *fw = findFloating(&ws, toplevel)) {
            fw->x = x;
            fw->y = y;
            fw->width = width;
            fw->height = height;
            fw->positioned = true;
            return true;
        }
    }
    return false;
}

std::optional<struct wlr_box> LayoutManager::getFloatingGeometry(Toplevel *toplevel) const
{
    for (auto &ws : workspaces) {
        if (auto *fw = findFloating(&ws, toplevel)) {
            struct wlr_box b = { fw->x, fw->y, fw->width, fw->height };
            return b;
        }
    }
    return std::nullopt;
}

std::vector<Toplevel*> LayoutManager::getFloatingWindows(int workspace) const
{
    const Workspace *ws = findWorkspace(workspace);
    if (!ws) return {};
    std::vector<Toplevel*> out;
    out.reserve(ws->floating.size());
    for (auto &fw : ws->floating) out.push_back(fw.toplevel);
    return out;
}

bool LayoutManager::setFloating(Toplevel *toplevel, bool makeFloating, struct wlr_box usable)
{
    if (!toplevel) return false;
    Workspace *ws = findWorkspaceByWindow(toplevel);
    if (!ws) return false;
    if (ws->fullscreenWindow == toplevel || ws->maximizedWindow == toplevel) return false;
    bool currentlyFloating = findFloating(ws, toplevel) != nullptr;
    if (makeFloating == currentlyFloating) return false;

    if (makeFloating) {
        // Remove from BSP tree, preserve geometry
        BspNode *leaf = findLeaf(ws->root.get(), toplevel);
        if (!leaf) return false; // not in tiled tree, maybe already floating?
        int fx = leaf->x;
        int fy = leaf->y;
        int fw = leaf->width;
        int fh = leaf->height;
        bool pos = leaf->positioned;
        if (!pos || fw <= 0 || fh <= 0) {
            fw = std::min(800, usable.width > 0 ? usable.width : 800);
            fh = std::min(600, usable.height > 0 ? usable.height : 600);
            fx = usable.x + (usable.width - fw) / 2;
            fy = usable.y + (usable.height - fh) / 2;
            pos = true;
        }
        // Remove leaf from BSP (without affecting floating list)
        // Re-use remove logic but need to avoid floating check duplication
        // Special case: root is leaf
        if (ws->root && ws->root->type == BspNode::Type::Leaf && ws->root->toplevel == toplevel) {
            ws->root.reset();
        } else {
            removeRecursive(ws->root, toplevel);
        }
        FloatingWindow f;
        f.toplevel = toplevel;
        f.x = fx;
        f.y = fy;
        f.width = fw;
        f.height = fh;
        f.positioned = pos;
        ws->floating.push_back(f);
        applyFloatingGeometry(ws->floating.back());
        if (toplevel->getSceneTree()) {
            wlr_scene_node_set_enabled(&toplevel->getSceneTree()->node, true);
        }
        return true;
    } else {
        // Make tiled: remove from floating, insert back into BSP
        FloatingWindow *fw = findFloating(ws, toplevel);
        if (!fw) return false;
        int fx = fw->x, fy = fw->y, fwW = fw->width, fwH = fw->height;
        // Remove from floating vector
        for (auto it = ws->floating.begin(); it != ws->floating.end(); ++it) {
            if (it->toplevel == toplevel) { ws->floating.erase(it); break; }
        }
        // If no root, make leaf directly
        if (!ws->root) {
            ws->root = BspNode::makeLeaf(toplevel, nullptr);
            auto *leaf = ws->root.get();
            leaf->x = fx; leaf->y = fy; leaf->width = fwW; leaf->height = fwH; leaf->positioned = true;
            return true;
        }
        // Find best leaf to split near the floating window's center
        double cx = fx + fwW / 2.0;
        double cy = fy + fwH / 2.0;
        BspNode *target = findBestLeafForInsertion(ws, usable, cx, cy);
        if (!target) {
            std::vector<BspNode*> leaves;
            collectLeaves(ws->root.get(), leaves);
            if (!leaves.empty()) target = leaves.front();
        }
        if (!target) {
            ws->root = BspNode::makeLeaf(toplevel, nullptr);
            return true;
        }
        BspNode *parent = target->parent;
        Orientation newOrient;
        if (parent) {
            if (oppositeOrientation) newOrient = opposite(parent->orientation);
            else newOrient = parent->orientation;
        } else {
            newOrient = (usable.width >= usable.height) ? Orientation::Horizontal : Orientation::Vertical;
        }
        double ratio = std::clamp(defaultSplitRatio, minRatio, maxRatio);

        struct wlr_box targetBox = getBoxForNode(ws->root.get(), target, usable);
        bool placeLeftTop = false;
        if (newOrient == Orientation::Horizontal) {
            double centreX = targetBox.x + targetBox.width / 2.0;
            placeLeftTop = cx < centreX;
        } else {
            double centreY = targetBox.y + targetBox.height / 2.0;
            placeLeftTop = cy < centreY;
        }

        auto newLeaf = BspNode::makeLeaf(toplevel, nullptr);
        // Split target leaf
        std::function<bool(std::unique_ptr<BspNode>&)> doSplit = [&](std::unique_ptr<BspNode> &node) -> bool {
            if (!node) return false;
            if (node.get() == target) {
                std::unique_ptr<BspNode> oldLeaf = std::move(node);
                auto branch = std::make_unique<BspNode>();
                branch->type = BspNode::Type::Branch;
                branch->orientation = newOrient;
                branch->ratio = ratio;
                branch->parent = oldLeaf->parent;
                oldLeaf->parent = branch.get();
                newLeaf->parent = branch.get();
                if (placeLeftTop) {
                    branch->left = std::move(newLeaf);
                    branch->right = std::move(oldLeaf);
                } else {
                    branch->left = std::move(oldLeaf);
                    branch->right = std::move(newLeaf);
                }
                node = std::move(branch);
                return true;
            }
            if (node->type == BspNode::Type::Branch) {
                if (doSplit(node->left)) return true;
                if (doSplit(node->right)) return true;
            }
            return false;
        };
        doSplit(ws->root);
        return true;
    }
}

bool LayoutManager::toggleFloating(Toplevel *toplevel, struct wlr_box usable)
{
    if (!toplevel) return false;
    bool cur = isFloating(toplevel);
    return setFloating(toplevel, !cur, usable);
}

bool LayoutManager::isFullscreen(Toplevel *toplevel) const
{
    for (auto &ws : workspaces) if (ws.fullscreenWindow == toplevel) return true;
    return false;
}

bool LayoutManager::isMaximized(Toplevel *toplevel) const
{
    for (auto &ws : workspaces) if (ws.maximizedWindow == toplevel) return true;
    return false;
}

Toplevel* LayoutManager::getFullscreenWindow(int workspace) const
{
    const Workspace *ws = findWorkspace(workspace);
    return ws ? ws->fullscreenWindow : nullptr;
}

Toplevel* LayoutManager::getMaximizedWindow(int workspace) const
{
    const Workspace *ws = findWorkspace(workspace);
    return ws ? ws->maximizedWindow : nullptr;
}

bool LayoutManager::setFullscreen(Toplevel *toplevel, bool fullscreen, struct wlr_box fullBox)
{
    if (!toplevel) return false;
    Workspace *ws = findWorkspaceByWindow(toplevel);
    if (!ws) return false;
    bool cur = ws->fullscreenWindow == toplevel;
    if (fullscreen == cur) return false;
    if (fullscreen) {
        // If maximized, clear it first (fullscreen takes precedence)
        if (ws->maximizedWindow == toplevel) ws->maximizedWindow = nullptr;
        else if (ws->maximizedWindow) ws->maximizedWindow = nullptr; // only one maximized at a time, clear
        // If another window was fullscreen, replace
        ws->fullscreenWindow = toplevel;
        applyDirectGeometry(toplevel, fullBox);
        if (toplevel->getSceneTree()) {
            wlr_scene_node_set_enabled(&toplevel->getSceneTree()->node, true);
        }
        // Notify client state will be set by compositor via wlr_xdg_toplevel_set_fullscreen
        return true;
    } else {
        if (ws->fullscreenWindow != toplevel) return false;
        ws->fullscreenWindow = nullptr;
        // Geometry will be restored on next arrange
        return true;
    }
}

bool LayoutManager::setMaximized(Toplevel *toplevel, bool maximized)
{
    if (!toplevel) return false;
    Workspace *ws = findWorkspaceByWindow(toplevel);
    if (!ws) return false;
    bool cur = ws->maximizedWindow == toplevel;
    if (maximized == cur) return false;
    if (maximized) {
        if (ws->fullscreenWindow == toplevel) return false; // cannot maximize a fullscreen window
        if (ws->fullscreenWindow) return false; // fullscreen takes precedence, refuse maximize while fullscreen active
        ws->maximizedWindow = toplevel;
        // Clear any other maximized
        return true;
    } else {
        if (ws->maximizedWindow != toplevel) return false;
        ws->maximizedWindow = nullptr;
        return true;
    }
}

bool LayoutManager::toggleFullscreen(Toplevel *toplevel, struct wlr_box fullBox)
{
    if (!toplevel) return false;
    return setFullscreen(toplevel, !isFullscreen(toplevel), fullBox);
}

bool LayoutManager::toggleMaximized(Toplevel *toplevel)
{
    if (!toplevel) return false;
    return setMaximized(toplevel, !isMaximized(toplevel));
}

void LayoutManager::arrangeNodeOthers(BspNode *node, struct wlr_box box, Toplevel *excluded)
{
    if (!node) return;
    if (node->type == BspNode::Type::Leaf) {
        if (node->toplevel == excluded) {
            // Keep excluded window as is (don't reposition), but ensure enabled
            if (node->toplevel && node->toplevel->getSceneTree())
                wlr_scene_node_set_enabled(&node->toplevel->getSceneTree()->node, true);
            return;
        }
        node->x = box.x;
        node->y = box.y;
        node->width = box.width;
        node->height = box.height;
        node->positioned = true;
        applyWindowGeometry(node);
        if (node->toplevel && node->toplevel->getSceneTree())
            wlr_scene_node_set_enabled(&node->toplevel->getSceneTree()->node, true);
        return;
    }
    double r = std::clamp(node->ratio, minRatio, maxRatio);
    if (node->orientation == Orientation::Horizontal) {
        int lw = (int)(box.width * r);
        int rw = box.width - lw;
        struct wlr_box lb = { box.x, box.y, lw, box.height };
        struct wlr_box rb = { box.x + lw, box.y, rw, box.height };
        arrangeNodeOthers(node->left.get(), lb, excluded);
        arrangeNodeOthers(node->right.get(), rb, excluded);
    } else {
        int th = (int)(box.height * r);
        int bh = box.height - th;
        struct wlr_box tb = { box.x, box.y, box.width, th };
        struct wlr_box bb = { box.x, box.y + th, box.width, bh };
        arrangeNodeOthers(node->left.get(), tb, excluded);
        arrangeNodeOthers(node->right.get(), bb, excluded);
    }
}

void LayoutManager::arrangeOthers(const struct wlr_box &usable, int workspace, Toplevel *excluded)
{
    Workspace *ws = findWorkspace(workspace);
    if (!ws) return;
    if (!ws->root && ws->floating.empty() && !ws->fullscreenWindow && !ws->maximizedWindow) return;
    // Fullscreen/maximized windows are not resizable via tiling; just keep them
    if (ws->fullscreenWindow) return;
    if (ws->maximizedWindow) {
        // Maximized windows are not tiled-resizable either
        if (ws->maximizedWindow == excluded) return;
        // For other windows, maximized stays, tiled others hidden already
        return;
    }
    bool excludedIsFloating = excluded && findFloating(ws, excluded) != nullptr;
    if (ws->mode == Mode::MonoWindow) {
        // Mono: others stay disabled, excluded is the one being resized? For mono we disable others
        std::vector<BspNode*> leaves;
        collectLeaves(ws->root.get(), leaves);
        for (auto *leaf : leaves) {
            if (leaf->toplevel == excluded) continue;
            if (leaf->toplevel && leaf->toplevel->getSceneTree())
                wlr_scene_node_set_enabled(&leaf->toplevel->getSceneTree()->node, false);
        }
        for (auto &fw : ws->floating) {
            if (fw.toplevel == excluded) continue;
            if (fw.toplevel && fw.toplevel->getSceneTree())
                wlr_scene_node_set_enabled(&fw.toplevel->getSceneTree()->node, true);
        }
        return;
    }
    if (ws->mode == Mode::Floating) {
        // For floating, keep excluded as is, clamp others to usable
        std::vector<BspNode*> leaves;
        collectLeaves(ws->root.get(), leaves);
        for (auto *leaf : leaves) {
            if (leaf->toplevel == excluded) continue;
            if (leaf->width > usable.width) leaf->width = usable.width;
            if (leaf->height > usable.height) leaf->height = usable.height;
            if (leaf->x < usable.x) leaf->x = usable.x;
            if (leaf->y < usable.y) leaf->y = usable.y;
            if (leaf->x + leaf->width > usable.x + usable.width) leaf->x = usable.x + usable.width - leaf->width;
            if (leaf->y + leaf->height > usable.y + usable.height) leaf->y = usable.y + usable.height - leaf->height;
            applyWindowGeometry(leaf);
        }
        for (auto &fw : ws->floating) {
            if (fw.toplevel == excluded) continue;
            if (fw.width > usable.width) fw.width = usable.width;
            if (fw.height > usable.height) fw.height = usable.height;
            if (fw.x < usable.x) fw.x = usable.x;
            if (fw.y < usable.y) fw.y = usable.y;
            if (fw.x + fw.width > usable.x + usable.width) fw.x = usable.x + usable.width - fw.width;
            if (fw.y + fw.height > usable.y + usable.height) fw.y = usable.y + usable.height - fw.height;
            applyFloatingGeometry(fw);
        }
        return;
    }
    // Tiling: re-arrange but skip excluded
    if (excludedIsFloating) {
        // Tiling with floating excluded: rearrange tiled fully, clamp other floatings
        if (ws->root) arrangeNode(ws->root.get(), usable);
        for (auto &fw : ws->floating) {
            if (fw.toplevel == excluded) {
                if (fw.toplevel && fw.toplevel->getSceneTree())
                    wlr_scene_node_set_enabled(&fw.toplevel->getSceneTree()->node, true);
                continue;
            }
            if (fw.width > usable.width) fw.width = usable.width;
            if (fw.height > usable.height) fw.height = usable.height;
            if (fw.x < usable.x) fw.x = usable.x;
            if (fw.y < usable.y) fw.y = usable.y;
            if (fw.x + fw.width > usable.x + usable.width) fw.x = usable.x + usable.width - fw.width;
            if (fw.y + fw.height > usable.y + usable.height) fw.y = usable.y + usable.height - fw.height;
            applyFloatingGeometry(fw);
        }
        return;
    }
    if (ws->root) arrangeNodeOthers(ws->root.get(), usable, excluded);
    // Also ensure floating windows remain clamped but visible
    for (auto &fw : ws->floating) {
        if (fw.width > usable.width) fw.width = usable.width;
        if (fw.height > usable.height) fw.height = usable.height;
        if (fw.x < usable.x) fw.x = usable.x;
        if (fw.y < usable.y) fw.y = usable.y;
        if (fw.x + fw.width > usable.x + usable.width) fw.x = usable.x + usable.width - fw.width;
        if (fw.y + fw.height > usable.y + usable.height) fw.y = usable.y + usable.height - fw.height;
        applyFloatingGeometry(fw);
    }
}

bool LayoutManager::toggleSplitOrientation(Toplevel *focused)
{
    if (!focused) return false;
    int wsId = getWindowWorkspace(focused);
    if (wsId < 0) return false;
    return toggleSplitOrientation(wsId, focused);
}

bool LayoutManager::toggleSplitOrientation(int workspace, Toplevel *focused)
{
    Workspace *ws = findWorkspace(workspace);
    if (!ws || !ws->root || !focused) return false;
    if (ws->fullscreenWindow == focused || ws->maximizedWindow == focused) return false;
    // Find parent branch that directly contains focused leaf
    BspNode *parent = findParentOfLeaf(ws->root.get(), focused);
    if (!parent) {
        // Single leaf has no parent => cannot toggle
        return false;
    }
    parent->orientation = opposite(parent->orientation);
    return true;
}

double LayoutManager::getParentRatio(Toplevel *toplevel) const
{
    for (const auto &ws : workspaces) {
        BspNode *parent = findParentOfLeaf(ws.root.get(), toplevel);
        if (parent) return parent->ratio;
    }
    return -1;
}

bool LayoutManager::setSplitRatio(Toplevel *toplevel, double ratio)
{
    for (auto &ws : workspaces) {
        BspNode *parent = findParentOfLeaf(ws.root.get(), toplevel);
        if (parent) {
            parent->ratio = std::clamp(ratio, minRatio, maxRatio);
            return true;
        }
    }
    return false;
}

static bool isDescendant(LayoutManager::BspNode *ancestor, LayoutManager::BspNode *leaf, bool checkLeft)
{
    if (!ancestor || ancestor->type != LayoutManager::BspNode::Type::Branch) return false;
    LayoutManager::BspNode *child = checkLeft ? ancestor->left.get() : ancestor->right.get();
    if (!child) return false;
    // BFS/DFS to see if leaf is inside child subtree
    std::function<bool(LayoutManager::BspNode*)> dfs = [&](LayoutManager::BspNode *node) -> bool {
        if (!node) return false;
        if (node == leaf) return true;
        if (node->type == LayoutManager::BspNode::Type::Branch) {
            if (dfs(node->left.get())) return true;
            if (dfs(node->right.get())) return true;
        }
        return false;
    };
    return dfs(child);
}

bool LayoutManager::handleResize(Toplevel *toplevel, struct wlr_box usable, double cursorX, double cursorY, uint32_t edges)
{
    for (auto &ws : workspaces) {
        if (ws.fullscreenWindow == toplevel || ws.maximizedWindow == toplevel) return false;
        BspNode *leaf = findLeaf(ws.root.get(), toplevel);
        if (!leaf) continue;
        bool needH = (edges & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)) != 0;
        bool needV = (edges & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)) != 0;
        auto tryHandle = [&](Orientation orient) -> bool {
            for (BspNode *anc = leaf->parent; anc != nullptr; anc = anc->parent) {
                if (anc->orientation != orient) continue;
                bool isLeftOrTop = isDescendant(anc, leaf, true);
                bool isRightOrBottom = isDescendant(anc, leaf, false);
                if (!isLeftOrTop && !isRightOrBottom) continue;
                if (orient == Orientation::Horizontal) {
                    if (isLeftOrTop && !(edges & WLR_EDGE_RIGHT) && edges != 0) continue;
                    if (isRightOrBottom && !(edges & WLR_EDGE_LEFT) && edges != 0) continue;
                    struct wlr_box ancBox = getBoxForNode(ws.root.get(), anc, usable);
                    if (ancBox.width <= 0) continue;
                    double rel = (cursorX - ancBox.x) / (double)ancBox.width;
                    // stop maintaining ratio when overflow - allow free resize
                    rel = std::clamp(rel, 0.01, 0.99);
                    anc->ratio = rel;
                    return true;
                } else {
                    if (isLeftOrTop && !(edges & WLR_EDGE_BOTTOM) && edges != 0) continue;
                    if (isRightOrBottom && !(edges & WLR_EDGE_TOP) && edges != 0) continue;
                    struct wlr_box ancBox = getBoxForNode(ws.root.get(), anc, usable);
                    if (ancBox.height <= 0) continue;
                    double rel = (cursorY - ancBox.y) / (double)ancBox.height;
                    rel = std::clamp(rel, 0.01, 0.99);
                    anc->ratio = rel;
                    return true;
                }
            }
            return false;
        };
        if (needH && needV) {
            bool h = tryHandle(Orientation::Horizontal);
            bool v = tryHandle(Orientation::Vertical);
            if (h || v) return true;
        } else if (needH) {
            if (tryHandle(Orientation::Horizontal)) return true;
        } else if (needV) {
            if (tryHandle(Orientation::Vertical)) return true;
        } else {
            if (tryHandle(Orientation::Horizontal)) return true;
            if (tryHandle(Orientation::Vertical)) return true;
        }
        // No ancestor could handle this edge -> refuse (don't fallback to wrong orientation)
        return false;
    }
    return false;
}

bool LayoutManager::commitResize(Toplevel *toplevel, struct wlr_box usable, uint32_t edges)
{
    for (auto &ws : workspaces) {
        if (ws.fullscreenWindow == toplevel || ws.maximizedWindow == toplevel) return false;
        BspNode *leaf = findLeaf(ws.root.get(), toplevel);
        if (!leaf) continue;
        // Leaf geometry already updated via updateWindowGeometry
        bool needH = (edges & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)) != 0;
        bool needV = (edges & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)) != 0;
        auto tryCommit = [&](Orientation orient) -> bool {
            for (BspNode *anc = leaf->parent; anc != nullptr; anc = anc->parent) {
                if (anc->orientation != orient) continue;
                bool isLeftOrTop = isDescendant(anc, leaf, true);
                bool isRightOrBottom = isDescendant(anc, leaf, false);
                if (!isLeftOrTop && !isRightOrBottom) continue;
                if (orient == Orientation::Horizontal) {
                    if (isLeftOrTop && !(edges & WLR_EDGE_RIGHT) && edges != 0) continue;
                    if (isRightOrBottom && !(edges & WLR_EDGE_LEFT) && edges != 0) continue;
                    struct wlr_box ancBox = getBoxForNode(ws.root.get(), anc, usable);
                    if (ancBox.width <= 0) continue;
                    double leftWidth;
                    if (isLeftOrTop) {
                        // left side: right edge of leaf's subtree
                        leftWidth = (leaf->x + leaf->width) - ancBox.x;
                    } else {
                        leftWidth = leaf->x - ancBox.x;
                    }
                    double rel = leftWidth / (double)ancBox.width;
                    anc->ratio = std::clamp(rel, minRatio, maxRatio);
                    return true;
                } else {
                    if (isLeftOrTop && !(edges & WLR_EDGE_BOTTOM) && edges != 0) continue;
                    if (isRightOrBottom && !(edges & WLR_EDGE_TOP) && edges != 0) continue;
                    struct wlr_box ancBox = getBoxForNode(ws.root.get(), anc, usable);
                    if (ancBox.height <= 0) continue;
                    double topHeight;
                    if (isLeftOrTop) {
                        topHeight = (leaf->y + leaf->height) - ancBox.y;
                    } else {
                        topHeight = leaf->y - ancBox.y;
                    }
                    double rel = topHeight / (double)ancBox.height;
                    anc->ratio = std::clamp(rel, minRatio, maxRatio);
                    return true;
                }
            }
            return false;
        };
        if (needH && needV) {
            bool h = tryCommit(Orientation::Horizontal);
            bool v = tryCommit(Orientation::Vertical);
            if (h || v) return true;
        } else if (needH) {
            if (tryCommit(Orientation::Horizontal)) return true;
        } else if (needV) {
            if (tryCommit(Orientation::Vertical)) return true;
        } else {
            if (tryCommit(Orientation::Horizontal)) return true;
            if (tryCommit(Orientation::Vertical)) return true;
        }
        return false;
    }
    return false;
}

bool LayoutManager::getWindowGeometry(Toplevel *toplevel, struct wlr_box &out) const {
    for (auto &ws : workspaces) {
        if (auto *fw = findFloating(&ws, toplevel)) {
            out = {fw->x, fw->y, fw->width, fw->height};
            return true;
        }
        if (ws.fullscreenWindow == toplevel || ws.maximizedWindow == toplevel) {
            // For fs/max, leaf geometry is not valid; use the node's direct geometry if any leaf exists
            // Fallback to floating-like: return last known leaf box if present
        }
        BspNode *leaf = findLeaf(ws.root.get(), toplevel);
        if (leaf && leaf->positioned) {
            out = {leaf->x, leaf->y, leaf->width, leaf->height};
            return true;
        }
    }
    return false;
}

bool LayoutManager::getWindowGeometry(Toplevel *toplevel, int workspace, struct wlr_box usable, struct wlr_box &out) const {
    const Workspace *ws = findWorkspace(workspace);
    if (!ws) return getWindowGeometry(toplevel, out);
    if (auto *fw = findFloating(ws, toplevel)) {
        out = {fw->x, fw->y, fw->width, fw->height};
        return true;
    }
    BspNode *leaf = findLeaf(ws->root.get(), toplevel);
    if (!leaf) return false;
    // compute box via traversal
    out = getBoxForNode(const_cast<BspNode*>(ws->root.get()), leaf, usable);
    return true;
}

std::unordered_map<Toplevel*, struct wlr_box> LayoutManager::snapshotGeometries(int workspace) const {
    std::unordered_map<Toplevel*, struct wlr_box> out;
    const Workspace *ws = findWorkspace(workspace);
    if (!ws) return out;
    std::vector<BspNode*> leaves;
    collectLeaves(ws->root.get(), leaves);
    for(auto *leaf: leaves){
        if(leaf->toplevel && leaf->positioned){
            struct wlr_box b = {leaf->x, leaf->y, leaf->width, leaf->height};
            out.emplace(leaf->toplevel, b);
        }
    }
    for(auto &fw: ws->floating){
        if(fw.toplevel && fw.positioned){
            struct wlr_box b = {fw.x, fw.y, fw.width, fw.height};
            out.emplace(fw.toplevel, b);
        }
    }
    return out;
}
std::unordered_map<Toplevel*, struct wlr_box> LayoutManager::snapshotGeometries(int workspace, struct wlr_box usable) const {
    std::unordered_map<Toplevel*, struct wlr_box> out;
    const Workspace *ws = findWorkspace(workspace);
    if (!ws) return out;
    std::vector<std::pair<BspNode*, struct wlr_box>> boxes;
    getAllBoxesRecursive(const_cast<BspNode*>(ws->root.get()), usable, boxes);
    for(auto &p: boxes){
        if(p.first->toplevel) out.emplace(p.first->toplevel, p.second);
    }
    for(auto &fw: ws->floating){
        if(fw.toplevel){
            struct wlr_box b = {fw.x, fw.y, fw.width, fw.height};
            if(fw.positioned) out.emplace(fw.toplevel, b);
        }
    }
    return out;
}

void LayoutManager::setLeafGeometry(Toplevel *tl, const struct wlr_box &box) {
    for (auto &ws : workspaces) {
        if (auto *fw = findFloating(&ws, tl)) {
            fw->x = box.x; fw->y = box.y; fw->width = box.width; fw->height = box.height; fw->positioned = true;
            return;
        }
        BspNode *leaf = findLeaf(ws.root.get(), tl);
        if (leaf) {
            leaf->x = box.x; leaf->y = box.y; leaf->width = box.width; leaf->height = box.height; leaf->positioned = true;
            return;
        }
    }
}

void LayoutManager::applyGeometries(const std::unordered_map<Toplevel*, struct wlr_box> &boxes) {
    for (auto &kv : boxes) {
        setLeafGeometry(kv.first, kv.second);
    }
}
