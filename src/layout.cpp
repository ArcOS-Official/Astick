#include "layout.h"
#include "toplevel.h"
#include <cmath>

TilingLayout::TilingLayout()
    : QObject(nullptr)
{}

void TilingLayout::arrange(struct wlr_output *output, const QList<Toplevel *> &toplevels)
{
    if (output == nullptr || toplevels.isEmpty()) return;

    int width = output->width;
    int height = output->height;
    int count = toplevels.size();

    int cols = std::ceil(std::sqrt(count));
    int rows = std::ceil((double)count / cols);

    int cell_w = width / cols;
    int cell_h = height / rows;

    for (int i = 0; i < count; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = col * cell_w;
        int y = row * cell_h;
        wlr_scene_node_set_position(&toplevels[i]->getSceneTree()->node, x, y);
        wlr_xdg_toplevel_set_size(toplevels[i]->get(), cell_w, cell_h);
    }
}